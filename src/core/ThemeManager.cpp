#include "ThemeManager.h"
#include "DirectoryLayout.h"
#include "SettingsManager.h"
#include "FileManager.h"
#include "ModManager.h"
#include "UiTokens.h"

#include <QApplication>
#include <QFileInfo>
#include <QStyle>
#include <QStyleFactory>

//单例访问
ThemeManager& ThemeManager::instance()
{
	static ThemeManager s_instance;
	return s_instance;
}

// ---------- 内置主题表 ----------

QList<ThemeManager::ThemeInfo> ThemeManager::themes()
{
	QList<ThemeInfo> list;
	list.append({ QStringLiteral("light"), QStringLiteral("浅色"),
				  QStringLiteral("与改造前观感一致；界面用浅底深字") });
	list.append({ QStringLiteral("dark"), QStringLiteral("深色"),
				  QStringLiteral("深底亮字，与游戏画面（深色棋盘）更协调") });
	return list;
}

QString ThemeManager::defaultThemeId()
{
	return QStringLiteral("light");
}

//可选基座风格：只有"跟随系统"与 Fusion
//Qt 在 Windows 上还会列出 windows11 / windowsvista / Windows，它们外观几乎一样，
//列出来只会让人以为它们是不同东西（用户反馈"很多类似的、重复的风格"），所以不放进来
QStringList ThemeManager::baseStyles()
{
	QStringList list;
	list.append(QStringLiteral("system"));
	list.append(QStringLiteral("Fusion"));
	return list;
}

QString ThemeManager::defaultBaseStyle()
{
	return QStringLiteral("system");
}

// ---------- 应用 ----------

void ThemeManager::applyFromSettings()
{
	SettingsManager& settings = SettingsManager::instance();
	applyInternal(settings.uiTheme(), settings.uiBaseStyle(), true);
}

//运行中换配色：只动调色板与样式表
//（setStyle 会让所有已创建控件重新 polish —— 十几个界面预创建之后那一下明显卡顿）
void ThemeManager::applyThemeInstant(const QString& themeId)
{
	applyInternal(themeId, m_baseStyle, false);
}

void ThemeManager::applyInternal(const QString& themeId, const QString& baseStyle, bool withBaseStyle)
{
	QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance());
	if (!app) { return; }

	//第一次调用时记下"默认长什么样"：切回 light 就是回到它
	if (!m_defaultCaptured)
	{
		m_defaultPalette = app->palette();
		m_defaultStyleName = app->style() ? app->style()->objectName() : QString();
		m_defaultCaptured = true;
	}

	m_themeId = themeId.isEmpty() ? defaultThemeId() : themeId;
	m_baseStyle = baseStyle.isEmpty() ? defaultBaseStyle() : baseStyle;

	//1) 基座风格（只有启动时动它）→ 2) 调色板 → 3) 全局样式表
	//顺序有意义：调色板要在风格之后设置，样式表最后（它优先级最高，但只作用于写到的控件）
	if (withBaseStyle)
	{
		applyBaseStyle(m_baseStyle);
	}
	applyPalette(m_themeId);

	//样式表 = 令牌替换（L0）→ 主题 qss（L1）→ 各 mod 追加的 override.qss
	QStringList tokenProblems;
	m_tokens = loadTokens(&tokenProblems);

	QString source;
	QString qss = loadQss(m_themeId, &source);

	//令牌替换：qss 里只写 @primary 这类名字；未定义的令牌保持原样并报出来
	QStringList missing;
	qss = UiTokens::substitute(qss, m_tokens, &missing);

	const QString overrides = modOverrideQss();
	int overrideCount = 0;
	for (const ModManager::UiOverride& overrideEntry : ModManager::instance().uiOverrides())
	{
		if (overrideEntry.hasQss && !overrideEntry.qss.isEmpty()) { ++overrideCount; }
	}

	if (!overrides.isEmpty())
	{
		//mod 的样式表放在最后：Qt 的规则是"同优先级下后写的胜出"，
		//所以追加天然就是"只覆盖它关心的部分"，不需要复制整份主题
		qss += QStringLiteral("\n/* ---- mod overrides ---- */\n") + overrides;
	}

	app->setStyleSheet(qss);

	FileManager::instance().log("theme", QStringLiteral("已应用界面主题：%1 + 基座 %2（样式表：%3；令牌：%4；mod 追加样式表：%5 份）")
		.arg(m_themeId, m_baseStyle, source, m_tokenSource)
		.arg(overrideCount));

	for (const QString& name : missing)
	{
		FileManager::instance().log("theme", QStringLiteral("样式表里用到未定义的令牌 @%1（已原样保留）").arg(name));
	}
	for (const QString& problem : tokenProblems)
	{
		FileManager::instance().log("theme", problem);
	}
}

//令牌（L0）：内置默认 -> ui/tokens.json -> 各生效 mod 的 config/ui.json（按 priority 顺序）
QHash<QString, QString> ThemeManager::loadTokens(QStringList* problems)
{
	QHash<QString, QString> tokens = UiTokens::builtin();
	QStringList sources;
	sources.append(QStringLiteral("内置默认"));

	//① 主题自带：ui/tokens.json（与 Assets/Data 一样由构建同步到运行目录）
	const QString diskPath = DirectoryLayout::instance().uiDir() + "/tokens.json";
	if (QFileInfo::exists(diskPath))
	{
		bool ok = false;
		const QJsonObject obj = FileManager::instance().readJsonObject(diskPath, QString(), &ok);
		if (ok)
		{
			UiTokens::merge(tokens, obj, QStringLiteral("ui/tokens.json"), problems);
			sources.append(QStringLiteral("ui/tokens.json"));
		}
		else if (problems)
		{
			problems->append(QStringLiteral("ui/tokens.json 解析失败，已忽略（继续用内置令牌）"));
		}
	}

	//② 各 mod 的令牌覆盖 —— 这就是"让 mod 换肤"的最短路径：改一组颜色，不用碰布局
	for (const ModManager::UiOverride& overrideEntry : ModManager::instance().uiOverrides())
	{
		if (!overrideEntry.hasTokens) { continue; }

		UiTokens::merge(tokens, overrideEntry.tokens,
						QStringLiteral("%1/config/ui.json").arg(overrideEntry.modId), problems);
		sources.append(QStringLiteral("%1 覆盖").arg(overrideEntry.modId));
	}

	m_tokenSource = sources.join(QStringLiteral(" + "));
	return tokens;
}

//各生效 mod 追加的样式表（ui/override.qss），按 priority 顺序拼起来
QString ThemeManager::modOverrideQss() const
{
	QStringList parts;
	for (const ModManager::UiOverride& overrideEntry : ModManager::instance().uiOverrides())
	{
		if (!overrideEntry.hasQss || overrideEntry.qss.isEmpty()) { continue; }

		parts.append(QStringLiteral("/* mod: %1 (%2) */\n%3")
			.arg(overrideEntry.modId, overrideEntry.qssPath, overrideEntry.qss));
	}
	return parts.join(QStringLiteral("\n"));
}

void ThemeManager::reload()
{
	//重读磁盘上的 qss 与令牌：不动基座风格（理由见头文件）
	applyInternal(m_themeId, m_baseStyle, false);
}

//基座风格："system" = 回到启动时那套原生风格，其余交给 QStyleFactory
void ThemeManager::applyBaseStyle(const QString& baseStyle)
{
	QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance());
	if (!app) { return; }

	const QString wanted = (baseStyle == QStringLiteral("system")) ? m_defaultStyleName : baseStyle;
	if (wanted.isEmpty()) { return; }

	//已经是这个风格就不折腾（setStyle 会重建所有控件样式，代价不小）
	if (app->style() && app->style()->objectName().compare(wanted, Qt::CaseInsensitive) == 0) { return; }

	QStyle* style = QStyleFactory::create(wanted);
	if (!style)
	{
		FileManager::instance().log("theme", QStringLiteral("找不到界面风格: %1（保持不变）").arg(wanted));
		return;
	}

	app->setStyle(style);
}

//调色板：**每个主题都给一套明确的颜色**，不依赖系统默认
//
// 为什么不能用 app->palette() 当"浅色"：
//   那是**启动时系统给的调色板**。如果玩家的 Windows 是深色模式，它就是一套深色调色板 ——
//   于是"浅色主题"里所有**没有在 qss 里写颜色**的控件（例如 QPushButton）会拿到浅色文字，
//   落在浅色背景上几乎看不见。真实反馈：朋友打包运行后在关卡选择页"看不到关卡"，
//   日志里 8 关都加载正常 —— 其实按钮在那里，只是白字压在白底上。
// 所以两个主题都按 tokens 的浅色/深色各给一套，系统深色模式不再影响游戏内配色。
void ThemeManager::applyPalette(const QString& themeId)
{
	QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance());
	if (!app) { return; }

	QPalette palette;

	if (themeId != QStringLiteral("dark"))
	{
		//浅色：与 ui/tokens.json 的浅色取值保持一致
		palette.setColor(QPalette::Window,          QColor(0xF5, 0xF6, 0xF8));
		palette.setColor(QPalette::WindowText,      QColor(0x22, 0x26, 0x2B));
		palette.setColor(QPalette::Base,            QColor(0xFF, 0xFF, 0xFF));
		palette.setColor(QPalette::AlternateBase,   QColor(0xF5, 0xF6, 0xF8));
		palette.setColor(QPalette::Text,            QColor(0x22, 0x26, 0x2B));
		palette.setColor(QPalette::Button,          QColor(0xFF, 0xFF, 0xFF));
		palette.setColor(QPalette::ButtonText,      QColor(0x22, 0x26, 0x2B));
		palette.setColor(QPalette::BrightText,      QColor(0xE5, 0x48, 0x4D));
		palette.setColor(QPalette::ToolTipBase,     QColor(0xFF, 0xFF, 0xFF));
		palette.setColor(QPalette::ToolTipText,     QColor(0x22, 0x26, 0x2B));
		palette.setColor(QPalette::Highlight,       QColor(0x2F, 0x8B, 0xEF));
		palette.setColor(QPalette::HighlightedText, QColor(0xFF, 0xFF, 0xFF));
		//禁用态给一个"能读出来但不抢眼"的灰：玩家要能看出"这一关还没解锁"
		palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x5C, 0x63, 0x6D));
		palette.setColor(QPalette::Disabled, QPalette::Text,       QColor(0x5C, 0x63, 0x6D));
		palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x97, 0x9D, 0xA6));

		app->setPalette(palette);
		return;
	}

	palette.setColor(QPalette::Window,          QColor(0x23, 0x26, 0x2B));
	palette.setColor(QPalette::WindowText,      QColor(0xEC, 0xEF, 0xF3));
	palette.setColor(QPalette::Base,            QColor(0x1B, 0x1D, 0x21));
	palette.setColor(QPalette::AlternateBase,   QColor(0x23, 0x26, 0x2B));
	palette.setColor(QPalette::Text,            QColor(0xEC, 0xEF, 0xF3));
	palette.setColor(QPalette::Button,          QColor(0x2E, 0x33, 0x3B));
	palette.setColor(QPalette::ButtonText,      QColor(0xEC, 0xEF, 0xF3));
	palette.setColor(QPalette::BrightText,      QColor(0xFF, 0x6B, 0x6B));
	palette.setColor(QPalette::ToolTipBase,     QColor(0x2E, 0x33, 0x3B));
	palette.setColor(QPalette::ToolTipText,     QColor(0xF2, 0xF4, 0xF7));
	palette.setColor(QPalette::Highlight,       QColor(0x3D, 0x7E, 0xFF));
	palette.setColor(QPalette::HighlightedText, QColor(0xFF, 0xFF, 0xFF));
	palette.setColor(QPalette::Link,            QColor(0x6E, 0xA8, 0xFF));
	palette.setColor(QPalette::PlaceholderText, QColor(0x9A, 0xA0, 0xA6));
	//禁用态：太暗会看不清"为什么不能点"，所以给一个仍可读的灰
	palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x8A, 0x90, 0x99));
	palette.setColor(QPalette::Disabled, QPalette::Text,       QColor(0x8A, 0x90, 0x99));
	palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x8A, 0x90, 0x99));

	app->setPalette(palette);
}

// ---------- 样式表 ----------

QString ThemeManager::loadQss(const QString& themeId, QString* source) const
{
	auto setSource = [source](const QString& text)
	{
		if (source) { *source = text; }
	};

	//① 磁盘：exe 旁边的 ui/<theme>.qss（与 Assets/Data 一样由构建同步过去）
	//   放在磁盘上的理由：改配色不用重新编译，以后还能被 mod / 更新包覆盖
	const QString diskPath = DirectoryLayout::instance().uiDir() + "/" + themeId + ".qss";
	if (QFileInfo::exists(diskPath))
	{
		const QString text = FileManager::instance().readText(diskPath);
		if (!text.isEmpty())
		{
			setSource(QStringLiteral("磁盘 %1").arg(diskPath));
			return text;
		}
	}

	//② 兜底：代码内置的那几条关键规则（保证缺文件时关键面板也不会糊成一团）
	setSource(QStringLiteral("内置兜底（未找到 %1）").arg(diskPath));
	return builtinQss(themeId);
}

QString ThemeManager::qssSource(const QString& themeId) const
{
	QString source;
	loadQss(themeId, &source);
	return source;
}

//内置兜底：**故意写得很短**——它只保证 HUD / 背包这些关键面板可读。
//完整的配色在 ui/<theme>.qss 里（那里才是"设计"该待的地方）。
//它也走令牌（@名字）：替换在 loadQss 之后统一做，所以缺文件时外观仍然受
//ui/tokens.json 与 mod 的令牌覆盖影响，行为一致。
QString ThemeManager::builtinQss(const QString& themeId) const
{
	const bool dark = (themeId == QStringLiteral("dark"));

	const QString panel     = dark ? QStringLiteral("#23262B") : QStringLiteral("@bg");
	const QString panelLine = dark ? QStringLiteral("#3A3F48") : QStringLiteral("@line");
	const QString ink       = dark ? QStringLiteral("#ECF0F5") : QStringLiteral("@ink");
	const QString listBg    = dark ? QStringLiteral("#1B1D21") : QStringLiteral("@panel");

	//游戏内 HUD：两套主题下都是深色（它贴在深色画布上，见 GameHud.cpp 的说明）
	const QString hudCss = QStringLiteral(R"(
#gameHud { background-color: @hudBg; border-bottom: 1px solid @hudLine; }
#hudValue { color: @hudInk; font-size: 14px; font-weight: bold; }
#hudHint  { color: @hudInkWeak; font-size: 13px; }
)");

	//背包有两种角色：
	//  #bagPanel   —— 从玩家信息进的正式页面（不透明，背后什么都没有）
	//  #bagOverlay —— 游戏内按 B 打开的覆盖层（半透明遮罩：压暗画面但保留可见，
	//                 这样既知道"游戏还在那儿"，列表与按钮也仍然清楚）
	const QString bagCss = QStringLiteral(R"(
#bagPanel { background-color: %1; }
#bagOverlay { background-color: @scrim; }
#bagTitle { color: %2; font-size: 20px; font-weight: bold; }
#itemList { background-color: %3; border: 1px solid %4; border-radius: @radius; color: %2; padding: 4px; }
#itemList::item { padding: 8px 10px; border-radius: @radiusSm; }
#itemList::item:selected { background-color: @primary; color: @onPrimary; }
#useBtn, #backBtn { color: @onPrimary; font-size: 16px; padding: 10px 16px; border: none; border-radius: @radius; }
#useBtn { background-color: @primary; }
#useBtn:hover { background-color: @primaryHi; }
#backBtn { background-color: @accent; }
#backBtn:hover { background-color: @accentHi; }
#primaryBtn, #startBtn, #okBtn, #accentBtn, #cancelBtn {
	color: @onPrimary; border: none; border-radius: @radius; padding: 10px 18px; font-size: 16px;
}
#primaryBtn, #startBtn, #okBtn { background-color: @primary; }
#accentBtn, #cancelBtn { background-color: @accent; }
#pageRoot { background-color: @bg; }
#pageTitle { color: @ink; font-size: @fontTitle; font-weight: bold; }
#pageSubtitle, #modSummary, #modDetail { color: @inkWeak; font-size: @fontSmall; }
)").arg(panel, ink, listBg, panelLine);

	return hudCss + bagCss;
}
