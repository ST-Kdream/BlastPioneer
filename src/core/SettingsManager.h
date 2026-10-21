#pragma once
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QByteArray>

//设置管理单例：读写 Data/settings.ini
//所有界面共享一份设置；窗口几何按屏幕 id 分键存储，不再互相覆盖
class SettingsManager
{
private:
	SettingsManager();
	SettingsManager(const SettingsManager&) = delete;
	SettingsManager& operator=(const SettingsManager&) = delete;

	QSettings settings;

public:
	//单例访问
	static SettingsManager& instance();

	//是否开启粒子特效（保留给启动界面读取）
	bool showStartupEffect() const;
	void setShowStartupEffect(bool isShow);

	//游戏时是否显示网格
	bool showGrid() const;
	void setShowGrid(bool isShow);

	//特效质量
	int effectQuality() const;           // 0低 1中 2高
	void setEffectQuality(int q);

	//帧率
	int frameRate() const;
	void setFrameRate(int fps);

	//音量
	int volume() const;
	void setVolume(int vol);

	//退出前是否弹确认框
	bool confirmOnExit() const;
	void setConfirmOnExit(bool confirm);

	//界面外观（两个正交旋钮，设置界面里就是两个下拉框）
	//  ui/baseStyle：用哪套 Qt 控件风格渲染（"system" = 跟随系统原生）
	//  ui/theme    ：配色主题（"light" / "dark"；样式表放在 ui/<theme>.qss）
	//空串表示"用 ThemeManager 的默认值"，于是以后加主题不需要迁移旧设置
	QString uiBaseStyle() const;
	void setUiBaseStyle(const QString& style);
	QString uiTheme() const;
	void setUiTheme(const QString& theme);

	//页面切换时是否淡入（游戏画面除外：它 60Hz 自绘，叠加透明度效果会白花钱）
	bool pageFade() const;
	void setPageFade(bool enabled);

	//是否允许原生模组（C ABI DLL）
	//默认**关闭**：原生模组能做任何事、崩溃会带走整个游戏 —— 总开关是唯一的防线（契约 §3.4）
	// ---- ??([update] ?;??? Updater.exe ????????) ----
	// ????????:??????(???????)?? = ???????????
	QStringList updateSources() const;
	void setUpdateSources(const QStringList& sources);
	bool updateKeepBackup() const;
	void setUpdateKeepBackup(bool keep);
	//???????:?????????????????
	bool updateUseMirror() const;
	void setUpdateUseMirror(bool use);
	//????????(alpha/beta/rc)
	bool updateTrackPrerelease() const;
	void setUpdateTrackPrerelease(bool track);

	bool allowNativeMods() const;
	void setAllowNativeMods(bool allowed);

	//已启用的模组 id 列表（键：mods/enabled，逗号分隔）
	//为什么放在设置里而不是写回 mod.json：那个文件属于模组作者，
	//玩家的开关是"用户数据"，不能被更新或作者改版覆盖
	QStringList enabledMods() const;
	void setEnabledMods(const QStringList& ids);
	//玩家是否动过模组开关
	//false = 全部按 mod.json 里作者建议的默认值；true = 以设置里的列表为准
	bool hasModsRecord() const;
	void setModEnabled(const QString& modId, bool enabled);

	//按屏幕 id 存取窗口几何（键：windows/geometry/<screenId>）
	//首次读取某屏幕时，若旧版本的整体几何键存在，则用它作为默认值
	QByteArray screenGeometry(const QString& screenId) const;
	void setScreenGeometry(const QString& screenId, const QByteArray& geometry);
};
