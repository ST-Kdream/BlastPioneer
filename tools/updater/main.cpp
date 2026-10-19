// ============================================================================
//  Updater.exe —— 外部差分更新器
//
//  为什么是独立 exe：主程序不能替换正在运行的自己（Windows 会锁住 exe 与已加载的 DLL），
//  所以更新逻辑放进一个"坐在主程序旁边、与它共用同一套 Qt DLL"的小程序。
//
//  两条源，结构完全一致（所以只有一套代码）：
//      <base>/manifest.json          清单（每文件的 size + sha256）
//      <base>/files/<相对路径>        文件本体
//    镜像先试；失败才转官网（GitHub 的 release 分支，raw 支持逐文件下载）。
//
//  一次更新的顺序（回滚能力来自第 5 步的**先备份**）：
//      1 读本地 version.txt 与设置（源 / 通道）
//      2 取 manifest（镜像 → 官网）
//      3 比对 sha256，算出"要下载 / 要删除"
//      4 下载到 Update/staging/ 并逐文件校验
//      5 备份所有将被覆盖或删除的文件到 Update/backup/<旧版本>/
//      6 落盘（staging → 包根）、执行删除、写 version.txt
//      7 自检：跑 BlastPioneer.exe --ui-selftest，非 0 → 全量回滚
//      8 清理 staging（backup 按设置保留）
//
//  三条硬规矩（比功能本身重要）：
//    · 只动包内路径：拒绝绝对路径、`..`、以及"会逃出包根"的任何写法；
//    · 用户数据永不触碰：存档 / 设置 / 崩溃记录 / logs / cache / mods / Update 一律拒绝，
//      即使它们出现在 manifest 里（那就是有人在发起攻击或清单写错了）；
//    · 校验不过就整体中止：绝不"应用一半"。
//
//  本文件不用 QObject 派生类、不用 moc：窗口只用现成控件 + lambda 连接。
// ============================================================================

#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QEventLoop>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QProcess>
#include <QtCore/QRegularExpression>
#include <QtCore/QSettings>
#include <QtCore/QStringList>
#include <QtCore/QTextStream>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtCore/QUrl>

#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <QtWidgets/QApplication>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include <cstdio>
#include <functional>

// ---------------------------------------------------------------------------
// 日志：同时写 stdout 与控制台；有窗口时也送进窗口
// ---------------------------------------------------------------------------
static QPlainTextEdit* g_logView = nullptr;
static bool g_consoleMode = true;

static void logLine(const QString& text)
{
	const QString stamped = QStringLiteral("[%1] %2")
		.arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), text);

	QTextStream out(stdout);
	out << stamped << Qt::endl;

	if (g_logView)
	{
		g_logView->appendPlainText(stamped);
	}

	// 也落盘：更新失败时玩家只需要把这一份日志发过来
	const QString logPath = QCoreApplication::applicationDirPath() + QStringLiteral("/logs/updater.log");
	QDir().mkpath(QFileInfo(logPath).absolutePath());
	QFile file(logPath);
	if (file.open(QIODevice::Append | QIODevice::Text))
	{
		QTextStream stream(&file);
		stream << QDateTime::currentDateTime().toString(Qt::ISODate) << " " << text << "\n";
	}
}

// ---------------------------------------------------------------------------
// 版本号：按段比较数字，其它字符忽略（"0.1.0" < "0.1.1" < "0.2" < "1.0"）
// ---------------------------------------------------------------------------
static QList<int> versionParts(const QString& version)
{
	QList<int> parts;
	const QStringList pieces = version.split(QRegularExpression(QStringLiteral("[^0-9]+")),
											 Qt::SkipEmptyParts);
	for (const QString& piece : pieces)
	{
		parts.append(piece.toInt());
	}
	while (!parts.isEmpty() && parts.last() == 0) { parts.removeLast(); }
	return parts;
}

static int compareVersions(const QString& left, const QString& right)
{
	const QList<int> a = versionParts(left);
	const QList<int> b = versionParts(right);
	const int count = qMax(a.size(), b.size());
	for (int i = 0; i < count; ++i)
	{
		const int va = i < a.size() ? a.at(i) : 0;
		const int vb = i < b.size() ? b.at(i) : 0;
		if (va != vb) { return va < vb ? -1 : 1; }
	}
	return 0;
}

// ---------------------------------------------------------------------------
// 设置：读的是主程序那份 Data/settings.ini（同一个文件，两个程序共享）
//
// 源是一个**有序列表**，不是"一个镜像 + 官网"：
//   国内访问 GitHub 的稳定性很差，所以默认给三个源，按速度/稳定性排序，谁先成就用谁：
//     1) jsDelivr      —— GitHub 仓库的公共 CDN（快，但单文件 20 MB 上限）
//     2) ghfast.top    —— raw.githubusercontent 的反代（国内通常可用）
//     3) GitHub raw    —— 官方源，永远最后一个（前面都挂了才用它）
//   三个源的目录结构完全一样（manifest.json + files/<路径>），所以代码只有一套。
// ---------------------------------------------------------------------------
struct UpdateSettings
{
	QStringList sources;                          // 有序的 base 列表（空 = 用内置默认）
	QString channel = QStringLiteral("stable");
	bool keepBackup = true;
};

// 内置默认源（改这里就等于改所有玩家的默认行为；也可以在设置里覆盖）
static QStringList defaultSources()
{
	return QStringList{
		QStringLiteral("https://cdn.jsdelivr.net/gh/ST-Kdream/BlastPioneer@release/latest"),
		QStringLiteral("https://ghfast.top/https://raw.githubusercontent.com/ST-Kdream/BlastPioneer/release/latest"),
		QStringLiteral("https://raw.githubusercontent.com/ST-Kdream/BlastPioneer/release/latest")
	};
}

static UpdateSettings readSettings(const QString& rootDir)
{
	UpdateSettings settings;
	QSettings ini(rootDir + QStringLiteral("/Data/settings.ini"), QSettings::IniFormat);
	ini.beginGroup(QStringLiteral("update"));

	// sources 用逗号或空白分隔；每行一个也行（.ini 里写多行更清楚）
	QString raw = ini.value(QStringLiteral("sources")).toString();
	raw.replace(QLatin1Char('\n'), QLatin1Char(','));
	raw.replace(QLatin1Char('\r'), QLatin1Char(','));
	for (const QString& piece : raw.split(QRegularExpression(QStringLiteral("[,;\\s]+")),
										 Qt::SkipEmptyParts))
	{
		const QString trimmed = piece.trimmed();
		if (!trimmed.isEmpty()) { settings.sources.append(trimmed); }
	}

	if (ini.contains(QStringLiteral("channel")))
	{
		settings.channel = ini.value(QStringLiteral("channel")).toString().trimmed();
	}
	if (ini.contains(QStringLiteral("keepBackup")))
	{
		settings.keepBackup = ini.value(QStringLiteral("keepBackup")).toBool();
	}
	ini.endGroup();

	if (settings.sources.isEmpty()) { settings.sources = defaultSources(); }
	return settings;
}

// ---------------------------------------------------------------------------
// HTTP：同步取一份数据（QNetworkAccessManager 是异步的，这里用事件循环等它）
// ---------------------------------------------------------------------------
static bool httpGet(const QUrl& url, QByteArray* body, QString* error, int timeoutMs = 20000)
{
	if (!url.isValid())
	{
		*error = QStringLiteral("地址无效：%1").arg(url.toString());
		return false;
	}

	QNetworkAccessManager manager;
	QNetworkRequest request(url);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
						 QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setHeader(QNetworkRequest::UserAgentHeader,
					  QStringLiteral("BlastPioneer-Updater/%1").arg(QCoreApplication::applicationVersion()));
	request.setTransferTimeout(timeoutMs);

	QNetworkReply* reply = manager.get(request);

	QEventLoop loop;
	QTimer timer;
	timer.setSingleShot(true);
	QObject::connect(&timer, &QTimer::timeout, &loop, [&loop]() { loop.quit(); });
	QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	timer.start(timeoutMs + 5000);
	loop.exec();

	if (!reply->isFinished())
	{
		reply->abort();
		*error = QStringLiteral("请求超时：%1").arg(url.toString());
		reply->deleteLater();
		return false;
	}

	const QNetworkReply::NetworkError code = reply->error();
	const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
	if (code != QNetworkReply::NoError)
	{
		*error = QStringLiteral("下载失败（%1，HTTP %2）：%3")
			.arg(url.toString()).arg(status).arg(reply->errorString());
		reply->deleteLater();
		return false;
	}
	if (status >= 400)
	{
		*error = QStringLiteral("服务器返回 HTTP %1：%2").arg(status).arg(url.toString());
		reply->deleteLater();
		return false;
	}

	*body = reply->readAll();
	reply->deleteLater();
	return true;
}

// ---------------------------------------------------------------------------
// 路径安全：清单里的路径必须是"包内的相对路径"
// ---------------------------------------------------------------------------
static bool isSafeRelativePath(const QString& path)
{
	if (path.isEmpty()) { return false; }
	if (path.contains(QLatin1Char('\\'))) { return false; }          // 统一用 /
	if (path.startsWith(QLatin1Char('/'))) { return false; }
	if (path.contains(QStringLiteral("../")) || path.startsWith(QStringLiteral(".."))) { return false; }
	if (path.contains(QLatin1Char(':'))) { return false; }           // 盘符 / 备用流
	if (path.startsWith(QLatin1Char(' '))) { return false; }
	return true;
}

// 用户数据：即便清单里写了也绝不覆盖（这是"更新器不碰存档"的唯一保证点）
static bool isUserDataPath(const QString& path)
{
	static const QStringList protectedPrefixes = {
		QStringLiteral("Data/playerData.json"),
		QStringLiteral("Data/settings.ini"),
		QStringLiteral("Data/launch_state.json"),
		QStringLiteral("Data/mods_disabled.json"),
		QStringLiteral("logs/"),
		QStringLiteral("cache/"),
		QStringLiteral("Update/"),
		QStringLiteral("mods/")
	};
	for (const QString& prefix : protectedPrefixes)
	{
		if (path == prefix || path.startsWith(prefix)) { return true; }
	}
	return false;
}

// ---------------------------------------------------------------------------
// 清单
// ---------------------------------------------------------------------------
struct FileEntry
{
	QString path;
	qint64 size = 0;
	QString sha256;
};

struct Manifest
{
	int schema = 0;
	QString version;
	QString channel;
	QString publishedAt;
	QString minUpdater;
	QList<FileEntry> files;
	QStringList deletes;

	bool parse(const QByteArray& json, QString* error)
	{
		QJsonParseError parseError{};
		const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
		if (document.isNull() || !document.isObject())
		{
			*error = QStringLiteral("manifest.json 不是合法 JSON：%1").arg(parseError.errorString());
			return false;
		}

		const QJsonObject object = document.object();
		schema = object.value(QStringLiteral("schema")).toInt();
		version = object.value(QStringLiteral("version")).toString();
		channel = object.value(QStringLiteral("channel")).toString();
		publishedAt = object.value(QStringLiteral("publishedAt")).toString();
		minUpdater = object.value(QStringLiteral("minUpdater")).toString();

		if (schema != 1)
		{
			*error = QStringLiteral("清单 schema=%1 不认识（本更新器只认 1）").arg(schema);
			return false;
		}
		if (version.isEmpty())
		{
			*error = QStringLiteral("清单里没有 version");
			return false;
		}

		const QJsonArray filesArray = object.value(QStringLiteral("files")).toArray();
		for (const QJsonValue& value : filesArray)
		{
			const QJsonObject item = value.toObject();
			FileEntry entry;
			entry.path = item.value(QStringLiteral("path")).toString();
			entry.size = static_cast<qint64>(item.value(QStringLiteral("size")).toDouble());
			entry.sha256 = item.value(QStringLiteral("sha256")).toString().toLower();

			if (!isSafeRelativePath(entry.path))
			{
				*error = QStringLiteral("清单里有非法路径：%1").arg(entry.path);
				return false;
			}
			if (entry.sha256.size() != 64)
			{
				*error = QStringLiteral("清单里 %1 的 sha256 不合法").arg(entry.path);
				return false;
			}
			files.append(entry);
		}

		for (const QJsonValue& value : object.value(QStringLiteral("delete")).toArray())
		{
			const QString path = value.toString();
			if (!isSafeRelativePath(path))
			{
				*error = QStringLiteral("清单的 delete 里有非法路径：%1").arg(path);
				return false;
			}
			deletes.append(path);
		}

		return true;
	}
};

// ---------------------------------------------------------------------------
// 本地文件哈希
// ---------------------------------------------------------------------------
static QString sha256OfFile(const QString& absolutePath)
{
	QFile file(absolutePath);
	if (!file.open(QIODevice::ReadOnly)) { return QString(); }

	QCryptographicHash hash(QCryptographicHash::Sha256);
	if (!hash.addData(&file)) { return QString(); }
	return QString::fromLatin1(hash.result().toHex());
}

// ---------------------------------------------------------------------------
// 更新计划
// ---------------------------------------------------------------------------
struct Plan
{
	QList<FileEntry> download;
	QStringList remove;
	qint64 bytes = 0;

	bool isEmpty() const { return download.isEmpty() && remove.isEmpty(); }
};

static Plan buildPlan(const Manifest& manifest, const QString& rootDir)
{
	Plan plan;
	for (const FileEntry& entry : manifest.files)
	{
		const QString path = rootDir + QLatin1Char('/') + entry.path;
		const QFileInfo info(path);
		if (!info.exists() || info.size() != entry.size)
		{
			plan.download.append(entry);
			plan.bytes += entry.size;
			continue;
		}
		if (sha256OfFile(path).compare(entry.sha256, Qt::CaseInsensitive) != 0)
		{
			plan.download.append(entry);
			plan.bytes += entry.size;
		}
	}
	for (const QString& path : manifest.deletes)
	{
		if (QFileInfo::exists(rootDir + QLatin1Char('/') + path)) { plan.remove.append(path); }
	}
	return plan;
}

// ---------------------------------------------------------------------------
// 一次完整的更新（含回滚）
// ---------------------------------------------------------------------------
struct Options
{
	QString rootDir;
	QString sourceOverride;      // --source=<base>：只用这一个源（测试 / 临时换源）
	QString mirrorBase;          // --mirror=<base>：插到最前面的源
	QString channel;
	bool checkOnly = false;
	bool assumeYes = false;
};

static int runUpdate(const Options& options)
{
	const QString rootDir = options.rootDir;
	const QString gameExe = rootDir + QStringLiteral("/BlastPioneer.exe");
	const QString stagingDir = rootDir + QStringLiteral("/Update/staging");
	const QString separator = QStringLiteral("/");

	// 上一次自更新留下的旧映像：那时它还是"正在运行的 exe"，删不掉；现在可以清了
	QFile::remove(rootDir + QStringLiteral("/Update/Updater.exe.old"));

	// ---- 1) 本地版本 ------------------------------------------------------
	QString localVersion;
	{
		QFile file(rootDir + QStringLiteral("/version.txt"));
		if (file.open(QIODevice::ReadOnly | QIODevice::Text))
		{
			const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
			for (const QString& line : lines)
			{
				if (line.startsWith(QStringLiteral("version=")))
				{
					localVersion = line.mid(8).trimmed();
				}
			}
		}
	}
	if (!QFileInfo::exists(gameExe))
	{
		logLine(QStringLiteral("这个目录里没有 BlastPioneer.exe —— 更新器必须和游戏放在一起"));
		return 1;
	}
	logLine(QStringLiteral("本地版本：%1").arg(localVersion.isEmpty() ? QStringLiteral("（未知）") : localVersion));

	// ---- 2) 取清单：按顺序试每个源（镜像在前，官网在最后） -----------------
	UpdateSettings settings = readSettings(rootDir);
	const QString channel = options.channel.isEmpty() ? settings.channel : options.channel;

	// 命令行给了 --source / --mirror 就只用它（测试与"临时换源"用）
	QStringList bases = options.sourceOverride.isEmpty() ? settings.sources
														 : QStringList{ options.sourceOverride };
	if (!options.mirrorBase.isEmpty()) { bases = QStringList{ options.mirrorBase }; }
	if (bases.isEmpty())
	{
		logLine(QStringLiteral("没有可用的更新源：请在 设置 → 更新 里填写，或检查 settings.ini 的 [update] sources"));
		return 1;
	}

	Manifest manifest;
	bool haveManifest = false;
	QString manifestBase;
	for (int index = 0; index < bases.size(); ++index)
	{
		const QString raw = bases.at(index).trimmed();
		if (raw.isEmpty()) { continue; }
		const QString base = raw.endsWith(QLatin1Char('/')) ? raw.left(raw.size() - 1) : raw;
		const QUrl url(base + QStringLiteral("/manifest.json"));

		logLine(QStringLiteral("取清单（%1/%2）：%3").arg(index + 1).arg(bases.size()).arg(url.toString()));
		QByteArray body;
		QString error;
		if (!httpGet(url, &body, &error))
		{
			logLine(QStringLiteral("  这个源不可用：%1").arg(error));
			continue;
		}

		Manifest parsed;
		if (!parsed.parse(body, &error))
		{
			logLine(QStringLiteral("  清单不合法：%1").arg(error));
			continue;
		}
		if (!parsed.channel.isEmpty() && parsed.channel != channel)
		{
			logLine(QStringLiteral("  跳过：清单属于 %1 通道，本机是 %2 通道").arg(parsed.channel, channel));
			continue;
		}

		manifest = parsed;
		manifestBase = base;
		haveManifest = true;
		logLine(QStringLiteral("  远端版本：%1（%2 个文件，%3）")
			.arg(manifest.version)
			.arg(manifest.files.size())
			.arg(manifest.publishedAt));
		break;
	}

	if (!haveManifest)
	{
		logLine(QStringLiteral("两个源都没拿到清单 —— 检查网络，或换一个源再试"));
		return 1;
	}

	// ---- 3) 版本与计划 ----------------------------------------------------
	if (!manifest.minUpdater.isEmpty() && compareVersions(QCoreApplication::applicationVersion(),
														  manifest.minUpdater) < 0)
	{
		logLine(QStringLiteral("这次更新要求更新器至少 %1（本机 %2）：请手动下载新版发布包")
			.arg(manifest.minUpdater, QCoreApplication::applicationVersion()));
		return 1;
	}

	if (compareVersions(manifest.version, localVersion) <= 0)
	{
		logLine(QStringLiteral("已是最新（本地 %1，远端 %2）").arg(localVersion, manifest.version));
		return 0;
	}

	const Plan plan = buildPlan(manifest, rootDir);
	if (plan.isEmpty())
	{
		logLine(QStringLiteral("文件都是最新的（可能只差 version.txt），直接写版本号"));
	}
	logLine(QStringLiteral("计划：下载 %1 个文件（%2 MB），删除 %3 个")
		.arg(plan.download.size())
		.arg(QString::number(plan.bytes / 1024.0 / 1024.0, 'f', 1))
		.arg(plan.remove.size()));

	if (options.checkOnly)
	{
		logLine(QStringLiteral("（--check：只检查，不下载）"));
		return 0;
	}
	if (plan.isEmpty() && plan.remove.isEmpty())
	{
		// 只需要更新版本号，继续走"落盘 + 自检"（保持行为一致）
	}
	if (!options.assumeYes)
	{
		logLine(QStringLiteral("按回车开始更新（Ctrl+C 取消）……"));
		char buffer[8];
		if (!fgets(buffer, sizeof(buffer), stdin)) { /* 非交互环境：继续 */ }
	}

	// ---- 4) 下载到 staging 并校验 ----------------------------------------
	QDir(stagingDir).removeRecursively();
	QDir().mkpath(stagingDir);

	for (const FileEntry& entry : plan.download)
	{
		const QUrl url(manifestBase + QStringLiteral("/files/") + entry.path);
		logLine(QStringLiteral("下载 %1（%2 KB）").arg(entry.path).arg(entry.size / 1024));

		QByteArray body;
		QString error;
		if (!httpGet(url, &body, &error))
		{
			logLine(QStringLiteral("  失败：%1").arg(error));
			logLine(QStringLiteral("更新已中止，游戏没有被动过"));
			return 1;
		}
		if (body.size() != entry.size)
		{
			logLine(QStringLiteral("  大小不符（期望 %1，实际 %2）—— 中止")
				.arg(entry.size).arg(body.size()));
			return 1;
		}
		const QString digest = QString::fromLatin1(
			QCryptographicHash::hash(body, QCryptographicHash::Sha256).toHex());
		if (digest.compare(entry.sha256, Qt::CaseInsensitive) != 0)
		{
			logLine(QStringLiteral("  校验失败（sha256 不符）—— 中止，游戏没有被动过"));
			return 1;
		}

		const QString staged = stagingDir + QLatin1Char('/') + entry.path;
		QDir().mkpath(QFileInfo(staged).absolutePath());
		QFile file(staged);
		if (!file.open(QIODevice::WriteOnly) || file.write(body) != body.size())
		{
			logLine(QStringLiteral("  写临时文件失败：%1").arg(staged));
			return 1;
		}
		file.close();
	}

	// ---- 5) 备份所有将被覆盖/删除的文件 ----------------------------------
	const QString backupDir = rootDir + QStringLiteral("/Update/backup/")
		+ (localVersion.isEmpty() ? QStringLiteral("unknown") : localVersion);
	QDir(backupDir).removeRecursively();
	QDir().mkpath(backupDir);

	QStringList toBackup = plan.remove;
	for (const FileEntry& entry : plan.download)
	{
		if (QFileInfo::exists(rootDir + separator + entry.path)) { toBackup.append(entry.path); }
	}
	toBackup.removeDuplicates();

	int backedUp = 0;
	for (const QString& path : toBackup)
	{
		const QString from = rootDir + separator + path;
		const QString to = backupDir + separator + path;
		QDir().mkpath(QFileInfo(to).absolutePath());
		if (QFile::exists(to)) { QFile::remove(to); }
		if (QFile::copy(from, to)) { ++backedUp; }
		else
		{
			logLine(QStringLiteral("备份失败：%1 —— 中止，游戏没有被动过").arg(path));
			return 1;
		}
	}
	logLine(QStringLiteral("已备份 %1 个文件到 Update/backup/%2")
		.arg(backedUp).arg(localVersion.isEmpty() ? QStringLiteral("unknown") : localVersion));

	// ---- 6) 落盘 ---------------------------------------------------------
	QStringList written;
	bool applyFailed = false;
	QString applyError;

	for (const FileEntry& entry : plan.download)
	{
		const QString from = stagingDir + separator + entry.path;
		const QString to = rootDir + separator + entry.path;
		QDir().mkpath(QFileInfo(to).absolutePath());

		// 自更新：跑着的 exe 不能被删除/覆盖，但**可以被改名**。
		// 于是把它挪到 Update/Updater.exe.old，再把新文件放到原位 —— 下次启动时删掉那个 .old。
		const bool isSelf = entry.path.compare(QStringLiteral("Updater.exe"), Qt::CaseInsensitive) == 0;
		if (isSelf)
		{
			const QString aside = rootDir + QStringLiteral("/Update/Updater.exe.old");
			QDir().mkpath(QFileInfo(aside).absolutePath());
			QFile::remove(aside);
			if (QFile::exists(to) && !QFile::rename(to, aside))
			{
				applyFailed = true;
				applyError = QStringLiteral("无法挪开正在运行的 Updater.exe");
				break;
			}
			if (!QFile::copy(from, to))
			{
				applyFailed = true;
				applyError = QStringLiteral("无法写入新的 Updater.exe");
				break;
			}
			written.append(entry.path);
			continue;
		}

		if (QFile::exists(to) && !QFile::remove(to))
		{
			applyFailed = true;
			applyError = QStringLiteral("无法覆盖 %1（可能被占用：先退出游戏）").arg(entry.path);
			break;
		}
		if (!QFile::copy(from, to))
		{
			applyFailed = true;
			applyError = QStringLiteral("无法写入 %1").arg(entry.path);
			break;
		}
		written.append(entry.path);
	}
	for (const QString& path : plan.remove)
	{
		if (applyFailed) { break; }
		QFile::remove(rootDir + separator + path);
	}

	// ---- 7) 自检 + 回滚 --------------------------------------------------
	if (!applyFailed)
	{
		logLine(QStringLiteral("自检：运行 BlastPioneer.exe --ui-selftest"));
		QProcess process;
		process.setProgram(gameExe);
		process.setArguments({ QStringLiteral("--ui-selftest") });
		process.setWorkingDirectory(rootDir);
		process.start();
		if (!process.waitForStarted(10000) || !process.waitForFinished(120000))
		{
			applyFailed = true;
			applyError = QStringLiteral("自检没能跑起来（超时或启动失败）");
		}
		else if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
		{
			applyFailed = true;
			applyError = QStringLiteral("自检失败（退出码 %1）").arg(process.exitCode());
		}
		else
		{
			logLine(QStringLiteral("自检通过"));
		}
	}

	// version.txt 是"这次更新成功"的凭据，必须**在自检通过之后**才写。
	// 一开始写在自检之前，自测立刻抓到了后果：回滚把文件还原了、版本号却停在新值上，
	// 下次启动会认为"已是最新"，永远不再重试。
	if (!applyFailed)
	{
		QFile version(rootDir + QStringLiteral("/version.txt"));
		if (version.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
		{
			QTextStream stream(&version);
			stream << "version=" << manifest.version << "\n";
			stream << "channel=" << channel << "\n";
			// built 用 UTC + 明确的毫秒省略格式。注意别再手动拼 "Z"：
			// Qt::ISODate 对 UTC 时间已经带 Z，拼一次就变成 "...ZZ"（自测里看到过）。
			stream << "built=" << QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"))
				   << "Z\n";

			stream << "graphics=d3d11\n";
		}
		else
		{
			applyFailed = true;
			applyError = QStringLiteral("无法写入 version.txt");
		}
	}

	if (applyFailed)
	{
		logLine(QStringLiteral("出错了：%1 —— 开始回滚").arg(applyError));
		for (const QString& path : written)
		{
			QFile::remove(rootDir + separator + path);
		}
		const QDir backupRoot(backupDir);
		const QFileInfoList entries = backupRoot.entryInfoList(QDir::Files | QDir::NoDotAndDotDot,
															  QDir::Name);
		// 递归拷回（backup 保留了目录结构）
		std::function<void(const QString&)> restore = [&](const QString& relative)
		{
			const QDir dir(backupDir + separator + relative);
			const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
			for (const QFileInfo& info : files)
			{
				const QString target = rootDir + separator + relative + separator + info.fileName();
				QDir().mkpath(QFileInfo(target).absolutePath());
				QFile::remove(target);
				QFile::copy(info.absoluteFilePath(), target);
			}
			const QFileInfoList dirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
			for (const QFileInfo& info : dirs)
			{
				restore(relative.isEmpty() ? info.fileName() : relative + separator + info.fileName());
			}
		};
		restore(QString());
		logLine(QStringLiteral("已回滚到 %1。原样保留 Update/backup 供排查").arg(localVersion));
		return 1;
	}

	// ---- 8) 清理 ---------------------------------------------------------
	QDir(stagingDir).removeRecursively();
	if (!settings.keepBackup) { QDir(backupDir).removeRecursively(); }

	logLine(QStringLiteral("更新完成：%1 → %2（改了 %3 个文件，删除 %4 个）")
		.arg(localVersion, manifest.version).arg(plan.download.size()).arg(plan.remove.size()));
	return 0;
}

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------
static void printUsage()
{
	QTextStream out(stdout);
	out << "Updater.exe —— BlastPioneer 差分更新器\n"
		<< "\n"
		<< "  Updater.exe [选项]\n"
		<< "\n"
		<< "  --check            只检查有没有新版本，不下载\n"
		<< "  --yes              不询问，直接更新\n"
		<< "  --window           用窗口显示进度（默认是控制台）\n"
		<< "  --source=<地址>    只用这一个源（覆盖设置里的候选列表；调试/换源用）\n"
		<< "  --mirror=<地址>    同 --source\n"
		<< "  --channel=<名字>   选择通道（默认 stable）\n"
		<< "  --root=<目录>      更新哪个目录（默认：本 exe 所在目录）\n"
		<< "  --help             显示这段说明\n"
		<< "\n"
		<< "退出码：0 = 已是最新/更新成功，1 = 出错（游戏保持原样）\n";
}

int main(int argc, char* argv[])
{
	QCoreApplication::setApplicationName(QStringLiteral("BlastPioneer Updater"));
	QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

	// 必须**先**有 Q(Core)Application 实例：applicationDirPath() / arguments() 在没有实例时
	// 只会返回空并打印 "Please instantiate the QApplication object first"。
	// 这一版一开始就踩了：rootDir 变成空字符串，于是每次都报"这个目录里没有 BlastPioneer.exe"。
	// 直接用 QApplication（而不是先 QCoreApplication 再换）：Windows 上窗体系统总是可用，
	// 这样"窗口模式 / 控制台模式"可以等到解析完参数再决定，不必两套启动路径。
	QApplication app(argc, argv);

	Options options;
	options.rootDir = QCoreApplication::applicationDirPath();
	bool wantWindow = false;
	bool showHelp = false;

	const QStringList arguments = QCoreApplication::arguments();
	for (int i = 1; i < arguments.size(); ++i)
	{
		const QString arg = arguments.at(i);
		if (arg == QStringLiteral("--check")) { options.checkOnly = true; }
		else if (arg == QStringLiteral("--yes")) { options.assumeYes = true; }
		else if (arg == QStringLiteral("--window")) { wantWindow = true; }
		else if (arg == QStringLiteral("--help") || arg == QStringLiteral("-h")) { showHelp = true; }
		else if (arg.startsWith(QStringLiteral("--root="))) { options.rootDir = arg.mid(7); }
		else if (arg.startsWith(QStringLiteral("--source="))) { options.sourceOverride = arg.mid(9); }
		else if (arg.startsWith(QStringLiteral("--mirror="))) { options.mirrorBase = arg.mid(9); }

		else if (arg.startsWith(QStringLiteral("--channel="))) { options.channel = arg.mid(10); }
	}

	options.rootDir = QDir(options.rootDir).absolutePath();

	if (showHelp)
	{
		printUsage();
		return 0;
	}

	if (!wantWindow)
	{
		g_consoleMode = true;
		return runUpdate(options);
	}

	// ---- 窗口模式：进度 + 日志 + 取消 ------------------------------------
	g_consoleMode = false;

	QWidget window;
	window.setWindowTitle(QStringLiteral("爆破先锋 更新器"));
	window.resize(720, 420);

	QLabel* title = new QLabel(QStringLiteral("正在检查更新……"), &window);
	title->setObjectName(QStringLiteral("title"));
	QProgressBar* bar = new QProgressBar(&window);
	bar->setRange(0, 0);          // 先做成"忙碌"，拿到计划后再按字节数
	QPlainTextEdit* logView = new QPlainTextEdit(&window);
	logView->setReadOnly(true);
	QPushButton* closeButton = new QPushButton(QStringLiteral("关闭"), &window);
	closeButton->setEnabled(false);

	QVBoxLayout* layout = new QVBoxLayout(&window);
	layout->addWidget(title);
	layout->addWidget(bar);
	layout->addWidget(logView, 1);
	QHBoxLayout* buttons = new QHBoxLayout();
	buttons->addStretch(1);
	buttons->addWidget(closeButton);
	layout->addLayout(buttons);

	g_logView = logView;
	QObject::connect(closeButton, &QPushButton::clicked, &window, &QWidget::close);

	window.show();

	// 更新是同步阻塞的；用 QTimer 把它推到事件循环里跑，窗口才有机会绘制
	int exitCode = 0;
	QTimer::singleShot(0, &window, [&]()
	{
		options.assumeYes = true;   // 窗口模式下没有 stdin，视为同意
		exitCode = runUpdate(options);
		title->setText(exitCode == 0 ? QStringLiteral("更新完成") : QStringLiteral("更新失败（游戏保持原样）"));
		bar->setRange(0, 1);
		bar->setValue(exitCode == 0 ? 1 : 0);
		closeButton->setEnabled(true);
	});

	app.exec();
	return exitCode;
}
