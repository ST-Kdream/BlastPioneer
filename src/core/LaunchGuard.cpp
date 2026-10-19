#include "LaunchGuard.h"
#include "DirectoryLayout.h"
#include "FileManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

// ============================================================
// 崩溃保护与安全模式实现
//
// 这一份代码的难点不在算法（状态机只有三个状态），而在**它必须假设最坏情况**：
//   · 进程可能在下一行就死掉，所以每次打点都要立即落盘（没有"退出时统一保存"这回事）；
//   · 文件可能因为上次死在写一半而残缺，所以读不懂一律当 Clean（绝不拦住启动）；
//   · 写盘可能失败（磁盘满/权限），所以失败只记警告，不抛出、不中断。
// ============================================================

namespace
{
	//文件结构版本：将来改字段时用它做兼容（不认识的 schema 一律当 Clean）
	constexpr int kSchema = 1;

	//连续异常结束次数的上限：停在 9，避免界面上出现夸张的数字
	constexpr int kMaxFailures = 9;

	//"正在加载某个原生模组"这个阶段的固定前缀（崩溃定位靠它）
	const char* const kLoadingNativePrefix = "loading_native:";

	const QString kTag = QStringLiteral("launch");

	QString nowText()
	{
		return QDateTime::currentDateTime().toString(Qt::ISODate);
	}

	QJsonArray toJson(const QStringList& list)
	{
		QJsonArray array;
		for (const QString& item : list) { array.append(item); }
		return array;
	}

	QStringList fromJson(const QJsonValue& value)
	{
		QStringList list;
		const QJsonArray array = value.toArray();
		for (const QJsonValue& item : array)
		{
			const QString text = item.toString();
			if (!text.isEmpty()) { list.append(text); }
		}
		return list;
	}
}

LaunchGuard& LaunchGuard::instance()
{
	static LaunchGuard s_instance;
	return s_instance;
}

QString LaunchGuard::defaultStatePath()
{
	return DirectoryLayout::instance().dataDir() + QStringLiteral("/launch_state.json");
}

QString LaunchGuard::statePath() const
{
	QMutexLocker locker(&m_mutex);
	return m_statePath;
}

// ---------- 读上次记录 ----------
//
// 一条铁律：**任何读不懂的情况都当 Clean**（文件不存在、JSON 坏、schema 不认识、
// 权限不足）。这份文件只是"线索"，它坏掉绝不该变成"游戏打不开"的第二个原因。
void LaunchGuard::readLastRecord()
{
	m_lastExitRead = QStringLiteral("unknown");
	m_lastExit = QStringLiteral("unknown");
	m_lastStage.clear();
	m_lastNativeMods.clear();
	m_notes.clear();
	m_failures = 0;

	if (m_statePath.isEmpty()) { return; }

	const QByteArray raw = FileManager::instance().read(m_statePath);
	if (raw.isEmpty())
	{
		// 不存在 = 第一次启动，这是完全正常的分支，不需要记日志
		return;
	}

	QJsonParseError error;
	const QJsonDocument doc = QJsonDocument::fromJson(raw, &error);
	if (error.error != QJsonParseError::NoError || !doc.isObject())
	{
		FileManager::instance().log(kTag, QStringLiteral("启动记录读不出来（JSON 坏或被截断），按正常启动处理"));
		return;
	}

	const QJsonObject obj = doc.object();
	const int schema = obj.value(QStringLiteral("schema")).toInt(-1);
	if (schema != kSchema)
	{
		FileManager::instance().log(kTag, QStringLiteral("启动记录的 schema=%1 不认识，按正常启动处理").arg(schema));
		return;
	}

	m_lastStage = obj.value(QStringLiteral("stage")).toString();
	m_lastNativeMods = fromJson(obj.value(QStringLiteral("nativeMods")));
	m_failures = obj.value(QStringLiteral("consecutiveFailures")).toInt(0);

	const bool running = obj.value(QStringLiteral("running")).toBool(false);
	const bool reachedHome = obj.value(QStringLiteral("reachedHome")).toBool(false);

	if (running && !reachedHome)
	{
		// 上次没正常结束，而且还没进过主页 —— 这就是"异常结束"
		m_lastExit = QStringLiteral("crash");

		const QString stage = m_lastStage;
		if (stage.startsWith(QLatin1String(kLoadingNativePrefix)))
		{
			m_notes = QStringLiteral("上次崩在加载 %1 时")
				.arg(stage.mid(static_cast<int>(qstrlen(kLoadingNativePrefix))));
		}
		else if (!stage.isEmpty())
		{
			m_notes = QStringLiteral("上次异常结束（最后阶段：%1）").arg(stage);
		}
		else
		{
			m_notes = QStringLiteral("上次异常结束");
		}

		FileManager::instance().log(kTag, QStringLiteral("%1（连续第 %2 次）")
			.arg(m_notes).arg(m_failures + 1));
	}
	else if (running && reachedHome)
	{
		// 进过主页说明游戏本来能跑起来：被强杀/断电不该算模组的账
		m_lastExit = QStringLiteral("clean");
		m_notes = QStringLiteral("上次没有正常退出，但已经进过主页（按正常处理）");
	}
	else
	{
		m_lastExit = QStringLiteral("clean");
		m_notes = QStringLiteral("上次正常退出");
	}

	m_lastExitRead = m_lastExit;
}

// ---------- 写记录（原子：临时文件 + 替换） ----------
void LaunchGuard::writeRecord()
{
	if (m_statePath.isEmpty()) { return; }

	QJsonObject obj;
	obj.insert(QStringLiteral("schema"), kSchema);
	obj.insert(QStringLiteral("appVersion"),
		QCoreApplication::instance() ? QCoreApplication::applicationVersion() : QString());
	obj.insert(QStringLiteral("running"), m_running);
	obj.insert(QStringLiteral("lastExit"), m_lastExit);
	obj.insert(QStringLiteral("consecutiveFailures"), m_failures);
	obj.insert(QStringLiteral("stage"), m_stage);
	obj.insert(QStringLiteral("startedAt"), m_startedAt);
	obj.insert(QStringLiteral("nativeMods"), toJson(m_nativeMods));
	obj.insert(QStringLiteral("loadedOk"), toJson(m_loadedOk));
	obj.insert(QStringLiteral("reachedHome"), m_reachedHome);
	obj.insert(QStringLiteral("safeModeRequested"), m_manual);
	obj.insert(QStringLiteral("notes"), m_notes);

	// 目录可能还没建起来（首次启动、或调用方给了临时路径）：
	// 先建出来，否则 QSaveFile 连打开都做不到，等于一点线索都留不下
	const QString dir = QFileInfo(m_statePath).absolutePath();
	if (!dir.isEmpty()) { QDir().mkpath(dir); }

	if (!FileManager::instance().writeJson(m_statePath, QJsonDocument(obj)) && !m_writeWarned)
	{
		// 打点失败只是"丢线索"，不该拖垮游戏 —— 所以只记一次警告就继续
		m_writeWarned = true;
		FileManager::instance().log(kTag, QStringLiteral("启动记录写不进去：崩溃时的定位线索会缺失，游戏继续"));
	}
}

// ---------- 本次启动 ----------
LaunchGuard::State LaunchGuard::startup(bool manualSafeMode, const QString& statePath)
{
	QMutexLocker locker(&m_mutex);

	m_statePath = statePath.isEmpty() ? defaultStatePath() : statePath;

	// 1) 读上次结论
	readLastRecord();

	// 2) 判定：上次异常结束 -> 计数 +1；连续两次 -> 自动安全模式
	const bool crashedLastTime = (m_lastExit == QStringLiteral("crash"));
	m_failures = crashedLastTime ? qMin(m_failures + 1, kMaxFailures) : 0;

	m_manual = manualSafeMode;
	if (manualSafeMode)        { m_state = State::SafeMode; }
	else if (m_failures >= 2)  { m_state = State::SafeMode; }
	else if (m_failures == 1)  { m_state = State::Suspected; }
	else                       { m_state = State::Clean; }

	// 3) 写下本次启动记录（running=true + stage=startup），立即落盘
	m_startedAt = nowText();
	m_stage = QStringLiteral("startup");
	m_running = true;
	m_reachedHome = false;
	m_promptConsumed = false;
	m_nativeMods.clear();
	m_loadedOk.clear();
	writeRecord();

	const char* stateName = (m_state == State::SafeMode) ? "SafeMode"
		: (m_state == State::Suspected) ? "Suspected" : "Clean";
	FileManager::instance().log(kTag, QStringLiteral("启动判定：%1（连续异常 %2 次%3）")
		.arg(QLatin1String(stateName)).arg(m_failures)
		.arg(manualSafeMode ? QStringLiteral("，手动要求安全模式") : QString()));

	return m_state;
}

LaunchGuard::State LaunchGuard::state() const
{
	QMutexLocker locker(&m_mutex);
	return m_state;
}

bool LaunchGuard::blockNativeMods() const
{
	QMutexLocker locker(&m_mutex);
	return m_state == State::SafeMode;
}

bool LaunchGuard::manualSafeMode() const
{
	QMutexLocker locker(&m_mutex);
	return m_manual;
}

int LaunchGuard::consecutiveFailures() const
{
	QMutexLocker locker(&m_mutex);
	return m_failures;
}

QString LaunchGuard::lastExit() const
{
	QMutexLocker locker(&m_mutex);
	return m_lastExitRead;
}

QString LaunchGuard::lastStage() const
{
	QMutexLocker locker(&m_mutex);
	return m_lastStage;
}

QStringList LaunchGuard::lastNativeMods() const
{
	QMutexLocker locker(&m_mutex);
	return m_lastNativeMods;
}

QString LaunchGuard::notes() const
{
	QMutexLocker locker(&m_mutex);
	return m_notes;
}

// ---------- 打点 ----------

void LaunchGuard::markStage(const QString& stage)
{
	QMutexLocker locker(&m_mutex);
	m_stage = stage;
	writeRecord();
}

void LaunchGuard::setPlannedNativeMods(const QStringList& ids)
{
	QMutexLocker locker(&m_mutex);
	m_nativeMods = ids;
	writeRecord();
}

// **每个 LoadLibrary 之前**调用：崩溃发生在加载第 N 个模组时，
// 文件里留下的就是这一行 —— 这是"崩在哪个模组"的唯一线索
void LaunchGuard::markNativeModLoading(const QString& modId)
{
	QMutexLocker locker(&m_mutex);
	m_stage = QLatin1String(kLoadingNativePrefix) + modId;
	if (!m_nativeMods.contains(modId)) { m_nativeMods.append(modId); }
	writeRecord();
}

void LaunchGuard::markNativeModLoaded(const QString& modId)
{
	QMutexLocker locker(&m_mutex);
	if (!m_loadedOk.contains(modId)) { m_loadedOk.append(modId); }
	writeRecord();
}

// 健康信号：到达主页 -> 计数清零（"到达主页"之所以能当健康信号，就是因为
// 原生模组是在那之前加载的；能走到这里说明这一轮加载没有崩）
void LaunchGuard::markReachedHome()
{
	QMutexLocker locker(&m_mutex);
	if (m_reachedHome) { return; }

	m_reachedHome = true;
	m_failures = 0;
	m_stage = QStringLiteral("running");
	writeRecord();

	FileManager::instance().log(kTag, QStringLiteral("已进入主页：连续异常结束计数清零"));
}

// 正常退出：崩溃/强杀时这一句不会执行 —— 这正是整个机制成立的原因
void LaunchGuard::markCleanExit()
{
	QMutexLocker locker(&m_mutex);
	m_lastExit = QStringLiteral("clean");
	m_stage = QStringLiteral("exiting");
	m_running = false;
	writeRecord();

	FileManager::instance().log(kTag, QStringLiteral("正常退出：启动记录已收尾"));
}

// Suspected 的一次性提示（只在本次运行弹一次，避免反复打扰）
bool LaunchGuard::consumeCrashPrompt()
{
	QMutexLocker locker(&m_mutex);
	if (m_state != State::Suspected || m_promptConsumed) { return false; }
	m_promptConsumed = true;
	return true;
}

// 玩家选了"用安全模式启动"：记一笔（只影响下一次启动的判定线索，不改设置）
void LaunchGuard::noteSafeModeRequest()
{
	QMutexLocker locker(&m_mutex);
	m_manual = true;
	writeRecord();
	FileManager::instance().log(kTag, QStringLiteral("玩家选择了用安全模式启动"));
}

QString LaunchGuard::lastExitText() const
{
	QMutexLocker locker(&m_mutex);

	if (m_state == State::SafeMode)
	{
		return m_manual
			? QStringLiteral("安全模式：已手动禁用全部原生模组（只对本次运行有效）")
			: QStringLiteral("连续 %1 次异常结束，已自动进入安全模式：本次不加载任何原生模组").arg(m_failures);
	}
	if (m_state == State::Suspected)
	{
		return m_notes;
	}
	return QString();
}

void LaunchGuard::resetForTest()
{
	QMutexLocker locker(&m_mutex);

	m_statePath.clear();
	m_state = State::Clean;
	m_manual = false;
	m_reachedHome = false;
	m_promptConsumed = false;
	m_writeWarned = false;
	m_running = true;
	m_startedAt.clear();
	m_stage.clear();
	m_lastExitRead = QStringLiteral("unknown");
	m_lastExit = QStringLiteral("unknown");
	m_lastStage.clear();
	m_lastNativeMods.clear();
	m_notes.clear();
	m_nativeMods.clear();
	m_loadedOk.clear();
	m_failures = 0;
}
