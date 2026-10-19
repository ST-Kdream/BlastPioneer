#pragma once
#include <QHash>
#include <QPalette>
#include <QString>
#include <QStringList>
#include <QList>

// ============================================================
// 主题管理单例：界面外观的唯一入口
//
// 它管两个**正交**的旋钮（设置界面里就是两个下拉框）：
//   ① 基座风格 baseStyle —— 用哪套 Qt 控件风格渲染（QStyleFactory）
//        "system" = 跟随系统原生（保留原来观感）
//        "Fusion" = 跨 Windows 版本一致、且最容易被 QSS 精细控制
//        （可选项直接来自 QStyleFactory::keys()，所以以后 Qt 出新风格也不用改代码）
//   ② 配色主题 theme —— 决定调色板（QPalette）+ 全局样式表
//        "light" / "dark"：内置两套；**样式表放在 ui/<theme>.qss**（磁盘优先），
//        于是"改配色"是改一个文本文件，不是改 C++ 再重新编译。
//
// 为什么两套一起给（而不是只换 QSS）：
//   QSS 只覆盖"你写到的控件"；菜单、滚动条、对话框、tooltip 这些由**调色板**决定。
//   只换 QSS 会出现"列表变深色、但滚动条还是亮色"这种半截效果。
//
// 为什么"基座风格"要能切：
//   你反馈过 Windows 原生风格看着死板。Fusion 在 Windows 10/11 上表现一致、
//   QSS 支持也更完整，所以留成可选，用来对比哪套更合你的审美。
// ============================================================
class ThemeManager
{
public:
	//一个可选配色主题
	struct ThemeInfo
	{
		QString id;
		QString name;
		QString description;
	};

	static ThemeManager& instance();

	//内置主题表（加主题只改这里 + 放一个 ui/<id>.qss）
	static QList<ThemeInfo> themes();
	static QString defaultThemeId();       //"light"：与改造前观感一致，不制造意外
	//可选基座风格
	//**故意只留两个**：QStyleFactory 在 Windows 上会列出 windows11 / windowsvista / Windows 等，
	//它们看起来几乎一样（用户反馈"有很多类似的、重复的风格"），而 Fusion 才是真正不同、
	//又能被 QSS 精细控制的那一套。宁可给两个有意义的选项，也不给一串看不出差别的重复项。
	static QStringList baseStyles();
	static QString defaultBaseStyle();     //"system"

	//启动时调用：基座风格 + 调色板 + 样式表，一次都应用
	void applyFromSettings();
	//运行中换配色：**只换调色板与样式表，不动基座风格**
	//为什么不动风格：QApplication::setStyle() 会让所有已创建控件重新 polish，
	//十几个界面预创建之后这一下会明显卡顿（用户反馈过）；而基座风格本来就"看不出多大差别"，
	//所以它改成"保存下来、下次启动生效"。
	void applyThemeInstant(const QString& themeId);
	//把磁盘上的样式表重新读一遍并应用（改完 qss 不用重启）
	void reload();

	QString currentThemeId() const { return m_themeId; }
	QString currentBaseStyle() const { return m_baseStyle; }

	//某个主题的样式表实际来自哪里：磁盘 ui/<id>.qss / 内置兜底（只写日志，不往界面里塞路径）
	QString qssSource(const QString& themeId) const;

	//当前生效的令牌表（含 mod 覆盖后的结果）
	const QHash<QString, QString>& currentTokens() const { return m_tokens; }
	//令牌来源说明（内置 / ui/tokens.json / 哪些 mod 覆盖了它）—— 只进日志
	QString tokenSource() const { return m_tokenSource; }

private:
	ThemeManager() = default;
	ThemeManager(const ThemeManager&) = delete;
	ThemeManager& operator=(const ThemeManager&) = delete;

	//真正的应用流程：调色板 + 令牌 + 样式表；withBaseStyle 决定要不要动 QStyle
	void applyInternal(const QString& themeId, const QString& baseStyle, bool withBaseStyle);

	//读样式表：磁盘 ui/<theme>.qss 优先，读不到用代码内置的兜底
	QString loadQss(const QString& themeId, QString* source) const;
	//代码内置的兜底样式表（只保底：保证游戏内 HUD / 背包 / 提示条这些关键面板不糊）
	QString builtinQss(const QString& themeId) const;

	//令牌（L0）：内置 -> ui/tokens.json -> 各生效 mod 的 config/ui.json（按 priority）
	//返回问题清单（令牌名写错、类型不对等），并记录来源说明
	QHash<QString, QString> loadTokens(QStringList* problems);
	//各生效 mod 追加的样式表（ui/override.qss，按 priority），拼成一段
	QString modOverrideQss() const;

	void applyBaseStyle(const QString& baseStyle);
	void applyPalette(const QString& themeId);

	//启动时先记下"默认长什么样"，这样切回 light 能准确还原
	QPalette m_defaultPalette;
	QString  m_defaultStyleName;
	bool     m_defaultCaptured = false;

	QString m_themeId;
	QString m_baseStyle;

	//最近一次应用时用到的令牌与来源说明
	QHash<QString, QString> m_tokens;
	QString m_tokenSource;
};
