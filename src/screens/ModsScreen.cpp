#include "ModsScreen.h"
#include "ModManager.h"
#include "ItemDatabase.h"
#include "LevelDatabase.h"
#include "DirectoryLayout.h"
#include "ThemeManager.h"
#include "NativeModHost.h"
#include "LaunchGuard.h"
#include "SettingsManager.h"

#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QDesktopServices>
#include <QUrl>

ModsScreen::ModsScreen(QWidget* parent) : ScaffoldScreen(parent)
{
	setPageTitle(QStringLiteral("模组"), QStringLiteral("把模组文件夹放进 mods 目录，再回到这里勾选启用"));

	m_summary = new QLabel(this);
	m_summary->setObjectName("modSummary");
	m_summary->setWordWrap(true);

	m_list = new QListWidget(this);
	m_list->setObjectName("modList");
	m_list->setToolTip(QStringLiteral("勾选即启用，立刻生效"));

	m_detail = new QLabel(this);
	m_detail->setObjectName("modDetail");
	m_detail->setWordWrap(true);

	m_hint = new QLabel(this);
	m_hint->setObjectName("modHint");
	m_hint->setWordWrap(true);
	m_hint->hide();

	contentLayout()->addWidget(m_summary);
	contentLayout()->addWidget(m_list, 1);
	contentLayout()->addWidget(m_detail);
	contentLayout()->addWidget(m_hint);

	connect(m_list, &QListWidget::itemChanged, this, &ModsScreen::onItemChanged);
	connect(m_list, &QListWidget::itemSelectionChanged, this, &ModsScreen::onSelectionChanged);

	QPushButton* openBtn = addFooterButton(QStringLiteral("打开模组文件夹"), QStringLiteral("ghostBtn"));
	connect(openBtn, &QPushButton::clicked, this, &ModsScreen::onOpenFolderClicked);

	QPushButton* reloadBtn = addFooterButton(QStringLiteral("重新扫描"), QStringLiteral("ghostBtn"));
	connect(reloadBtn, &QPushButton::clicked, this, &ModsScreen::onReloadClicked);

	addFooterStretch();
	addBackButton();
}

void ModsScreen::onEnter(const QVariantMap& args)
{
	Q_UNUSED(args);

	//每次进来都重扫：往 mods/ 里丢一个新模组之后不需要重启程序
	ModManager::instance().reload();
	reapplyTheme();
	if (m_hint) { m_hint->hide(); }
	refreshMods();
}

//重建列表
//
// 这里刻意**只显示玩家需要的信息**：名字、版本、能不能用、不能用时的原因。
// 接口版本号、priority、应用了几条覆盖、读了哪些文件 —— 那些都是开发细节，
// 一律写进 logs/game.log，不摆在玩家面前。
//
// 原生模组是唯一的例外：它值得多说一句 —— 它是以 DLL 形式完全接管进程的东西，
// 玩家必须能看出"这一条和别的模组不是一回事"（契约 §3.4 的信任模型）
void ModsScreen::refreshMods()
{
	ModManager& mods = ModManager::instance();
	NativeModHost& host = NativeModHost::instance();
	const bool safeMode = LaunchGuard::instance().blockNativeMods();
	const bool nativeAllowed = SettingsManager::instance().allowNativeMods();

	m_syncing = true;
	m_list->clear();

	const QList<ModManager::ModEntry> entries = mods.mods();
	for (const ModManager::ModEntry& entry : entries)
	{
		const bool isNative = entry.manifest.native;

		QListWidgetItem* item = new QListWidgetItem(
			QStringLiteral("%1    v%2")
				.arg(entry.manifest.name,
					 entry.manifest.version.isEmpty() ? QStringLiteral("?") : entry.manifest.version),
			m_list);

		if (isNative)
		{
			item->setText(item->text() + QStringLiteral("    [原生]"));
		}

		item->setData(Qt::UserRole, entry.manifest.id);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(entry.enabled ? Qt::Checked : Qt::Unchecked);

		//原生模组的状态只有宿主知道（它可能成功、可能被拒绝、也可能整轮被跳过）
		const NativeModHost::LoadedMod* record = nullptr;
		if (isNative)
		{
			for (const NativeModHost::LoadedMod& mod : host.mods())
			{
				if (mod.id == entry.manifest.id) { record = &mod; break; }
			}
		}

		QString problem;
		if (isNative && safeMode)
		{
			problem = QStringLiteral("因崩溃保护被跳过");
			item->setToolTip(QStringLiteral("安全模式：本次不会加载任何原生模组。\n"
											"勾选或取消都会在下次启动时生效。"));
		}
		else if (isNative && !nativeAllowed)
		{
			problem = QStringLiteral("需要在设置里打开「允许原生模组」");
			item->setToolTip(problem);
		}
		else if (isNative && record && !record->active)
		{
			problem = record->reason.isEmpty() ? QStringLiteral("未生效") : record->reason;
			item->setToolTip(problem);
		}
		else if (!entry.active)
		{
			problem = entry.blockedReason;
			item->setToolTip(problem);
		}
		else
		{
			item->setToolTip(isNative ? QStringLiteral("已启用（原生模组：与游戏同一个进程）")
									  : QStringLiteral("已启用"));
		}

		if (!problem.isEmpty())
		{
			item->setForeground(QColor(150, 150, 150));
			item->setText(item->text() + QStringLiteral("    —— ") + problem);
		}
	}

	m_syncing = false;

	int enabledCount = 0;
	int activeNative = 0;
	for (const ModManager::ModEntry& entry : entries)
	{
		if (entry.active) { ++enabledCount; }
		if (entry.manifest.native && entry.active) { ++activeNative; }
	}

	QString summary = QStringLiteral("已启用 %1 个，共找到 %2 个模组。")
		.arg(enabledCount).arg(entries.size());
	if (entries.isEmpty())
	{
		summary += QStringLiteral("\n还没有模组：把别人做好的模组文件夹放进 mods 目录即可"
								  "（本工程自带一个示例，在 mods/example_numbers）。");
	}

	//安全模式必须"说出来"：一个原生模组都没加载，玩家如果不知道原因，
	//会以为模组坏了或者游戏坏了
	if (safeMode)
	{
		summary += QStringLiteral("\n安全模式：原生模组已全部禁用（只对本次运行有效，重启后按正常方式加载）。");
	}
	else if (activeNative > 0)
	{
		summary += QStringLiteral("\n其中 %1 个是原生模组：它们与游戏同一个进程，游戏崩溃时会一起退出。")
			.arg(activeNative);
	}

	const QStringList problems = mods.problems();
	if (!problems.isEmpty())
	{
		//只报"有几个不能用"，具体原因在每一行的详情里
		summary += QStringLiteral("\n有 %1 个模组当前无法使用，选中它们可以看到原因。").arg(problems.size());
	}

	m_summary->setText(summary);
	refreshModDetail();
}

//详情：这个模组是干什么的、改了什么、能不能用
void ModsScreen::refreshModDetail()
{
	QListWidgetItem* item = m_list->currentItem();
	if (!item)
	{
		m_detail->setText(QStringLiteral("选中一个模组可以看到它的说明；左边的勾选框就是启用开关。"));
		return;
	}

	const QString id = item->data(Qt::UserRole).toString();

	for (const ModManager::ModEntry& entry : ModManager::instance().mods())
	{
		if (entry.manifest.id != id) { continue; }

		const bool isNative = entry.manifest.native;

		QStringList lines;
		if (!entry.manifest.description.isEmpty()) { lines.append(entry.manifest.description); }
		if (!entry.manifest.author.isEmpty()) { lines.append(QStringLiteral("作者：%1").arg(entry.manifest.author)); }

		//原生模组先说清楚它是什么：这不是"多了一个功能"，而是"多了一份能改一切的代码"
		if (isNative)
		{
			lines.append(QStringLiteral("原生模组：以 DLL 形式加载，能完全接管游戏进程；"
										"它崩溃时游戏会一起退出，而且关掉它需要重启游戏。"));
		}

		//"它改了什么"用玩家能理解的说法，而不是文件名
		QStringList parts;
		if (entry.config.hasGameplay) { parts.append(QStringLiteral("玩法数值")); }
		if (entry.config.hasItems) { parts.append(QStringLiteral("道具")); }
		if (entry.config.hasLevels) { parts.append(QStringLiteral("关卡")); }
		if (entry.ui.hasTokens) { parts.append(QStringLiteral("界面配色")); }
		if (entry.ui.hasQss) { parts.append(QStringLiteral("界面样式")); }
		if (isNative) { parts.append(QStringLiteral("原生逻辑")); }
		if (!parts.isEmpty())
		{
			lines.append(QStringLiteral("改动内容：%1").arg(parts.join(QStringLiteral("、"))));
		}

		//原生模组的状态以**宿主**为准（数据层说"生效"不等于 DLL 加载成功）
		if (isNative)
		{
			const bool safeMode = LaunchGuard::instance().blockNativeMods();
			const bool nativeAllowed = SettingsManager::instance().allowNativeMods();

			const NativeModHost::LoadedMod* record = nullptr;
			for (const NativeModHost::LoadedMod& mod : NativeModHost::instance().mods())
			{
				if (mod.id == id) { record = &mod; break; }
			}

			if (safeMode)
			{
				lines.append(QStringLiteral("当前状态：因崩溃保护被跳过（安全模式：本次不加载任何原生模组）"));
			}
			else if (!nativeAllowed)
			{
				lines.append(QStringLiteral("当前状态：需要在设置里打开「允许原生模组」"));
			}
			else if (record && !record->active && !record->reason.isEmpty())
			{
				lines.append(QStringLiteral("当前状态：%1").arg(record->reason));
			}

			//耗时统计：只报信息、不做决定（契约 §7.3），而且只在真的超过软预算时才提，
			//否则常态的 0.0x ms 只会变成噪音
			for (const NativeModHost::ModStats& stats : NativeModHost::instance().stats())
			{
				if (stats.id != id || stats.callbacks <= 0) { continue; }
				if (stats.slowCallbacks > 0)
				{
					lines.append(QStringLiteral("⚠ 上次运行中这个模组平均回调 %1 ms（最慢 %2 ms，"
												"超过 2 ms 的有 %3 次），可能造成卡顿。")
						.arg(stats.avgCallbackMs, 0, 'f', 1)
						.arg(stats.maxCallbackMs, 0, 'f', 1)
						.arg(stats.slowCallbacks));
				}
			}
		}
		else if (!entry.active && !entry.blockedReason.isEmpty())
		{
			lines.append(QStringLiteral("当前状态：%1").arg(entry.blockedReason));
		}

		for (const QString& warning : entry.warnings) { lines.append(QStringLiteral("⚠ %1").arg(warning)); }

		m_detail->setText(lines.join(QStringLiteral("\n")));
		return;
	}

	m_detail->setText(QStringLiteral("这个模组已经不在磁盘上了，点「重新扫描」刷新列表。"));
}

//勾选/取消：立刻写设置，并把所有受影响的库一起重载
void ModsScreen::onItemChanged(QListWidgetItem* item)
{
	if (m_syncing || !item) { return; }

	const QString id = item->data(Qt::UserRole).toString();
	const bool enabled = (item->checkState() == Qt::Checked);

	ModManager::instance().setEnabled(id, enabled);
	ModManager::instance().reload();

	//道具、关卡与界面外观都是在加载时并入覆盖的，所以三处都要重算一遍 ——
	//这样"勾一下就能看到变化"才是真的
	ItemDatabase::instance().reload();
	LevelDatabase::instance().reload();
	reapplyTheme();

	//原生模组是唯一的例外：DLL 加载过就不能卸载（契约 §3.2），
	//所以它的开关只能"下次启动生效"—— 那就必须如实告诉玩家，而不是假装立刻生效
	bool isNative = false;
	QString nativeName = id;
	for (const ModManager::ModEntry& entry : ModManager::instance().mods())
	{
		if (entry.manifest.id != id) { continue; }
		isNative = entry.manifest.native;
		nativeName = entry.manifest.name.isEmpty() ? id : entry.manifest.name;
		break;
	}
	if (isNative)
	{
		m_hint->setText(QStringLiteral("「%1」是原生模组：开关已经记下，但要重启游戏才会生效"
									   "（原生模组一旦加载就不能卸载）。")
			.arg(nativeName));
		m_hint->show();
	}
	else
	{
		m_hint->hide();
	}

	refreshMods();
}

void ModsScreen::onSelectionChanged()
{
	refreshModDetail();
}

void ModsScreen::onReloadClicked()
{
	ModManager::instance().reload();
	ItemDatabase::instance().reload();
	LevelDatabase::instance().reload();
	reapplyTheme();
	if (m_hint) { m_hint->hide(); }
	refreshMods();
}

//在系统文件管理器里打开 mods 目录
//模组是"用户自己往里放文件夹"的东西，让人手动去找 exe 旁边的 mods/ 是最容易劝退的一步
void ModsScreen::onOpenFolderClicked()
{
	const QString dir = DirectoryLayout::instance().modsDir();

	//目录不存在时先建出来（空目录也能打开，比"打不开"友好）
	DirectoryLayout::instance().ensureDir(dir);
	QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

//模组可能改了配色或追加了样式表：重新应用一次（只换配色，不动基座风格）
void ModsScreen::reapplyTheme()
{
	ThemeManager::instance().reload();
}
