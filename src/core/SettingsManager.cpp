#include "SettingsManager.h"
#include "DirectoryLayout.h"
#include "../version.h"
#include <QDir>
#include <QRegularExpression>

//构造函数：配置文件固定放在 <root>/Data/settings.ini
SettingsManager::SettingsManager()
	: settings(DirectoryLayout::instance().settingsPath(), QSettings::IniFormat)
{
	//确保 Data 目录存在
	DirectoryLayout::instance().ensureDir(DirectoryLayout::instance().dataDir());
}

//单例访问（函数内静态，程序结束时自动析构）
SettingsManager& SettingsManager::instance()
{
	static SettingsManager s_instance;
	return s_instance;
}

//是否开启粒子特效（旧版本的开关，本版本启动界面强制显示，仅保留读写）
bool SettingsManager::showStartupEffect() const
{
	return settings.value("ui/showStartupEffect", true).toBool();
}

void SettingsManager::setShowStartupEffect(bool isShow)
{
	settings.setValue("ui/showStartupEffect", isShow);
}

//是否显示网格
bool SettingsManager::showGrid() const
{
	return settings.value("game/showGrid", true).toBool();
}

void SettingsManager::setShowGrid(bool isShow)
{
	settings.setValue("game/showGrid", isShow);
}

//特效质量
int SettingsManager::effectQuality() const
{
	return settings.value("graphics/effectQuality", 2).toInt();
}

void SettingsManager::setEffectQuality(int q)
{
	settings.setValue("graphics/effectQuality", q);
}

//帧率
int SettingsManager::frameRate() const
{
	return settings.value("graphics/frameRate", 60).toInt();
}

void SettingsManager::setFrameRate(int fps)
{
	settings.setValue("graphics/frameRate", fps);
}

//音量
int SettingsManager::volume() const
{
	return settings.value("audio/volume", 80).toInt();
}

void SettingsManager::setVolume(int vol)
{
	settings.setValue("audio/volume", vol);
}

//退出前是否确认
bool SettingsManager::confirmOnExit() const
{
	return settings.value("ui/confirmOnExit", true).toBool();
}

void SettingsManager::setConfirmOnExit(bool confirm)
{
	settings.setValue("ui/confirmOnExit", confirm);
}

// ---------- 界面外观 ----------

//基座风格：默认空串（= ThemeManager 的默认值"跟随系统原生"）
QString SettingsManager::uiBaseStyle() const
{
	return settings.value("ui/baseStyle", QString()).toString();
}

void SettingsManager::setUiBaseStyle(const QString& style)
{
	settings.setValue("ui/baseStyle", style);
}

//配色主题：默认空串（= ThemeManager 默认为 light，与改造前观感一致）
QString SettingsManager::uiTheme() const
{
	return settings.value("ui/theme", QString()).toString();
}

void SettingsManager::setUiTheme(const QString& theme)
{
	settings.setValue("ui/theme", theme);
}

//页面切换淡入：默认开（这是一个"便宜且明显"的观感提升）
bool SettingsManager::pageFade() const
{
	return settings.value("ui/pageFade", true).toBool();
}

void SettingsManager::setPageFade(bool enabled)
{
	settings.setValue("ui/pageFade", enabled);
}

//允许原生模组：默认关。界面上第一次打开时会弹一次风险说明
bool SettingsManager::allowNativeMods() const
{
	return settings.value("mods/allowNative", false).toBool();
}

QStringList SettingsManager::updateSources() const
{
	const QString raw = settings.value("update/sources").toString();
	QStringList result;
	for (const QString& piece : raw.split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts))
	{
		const QString trimmed = piece.trimmed();
		if (!trimmed.isEmpty()) { result.append(trimmed); }
	}
	return result;
}

void SettingsManager::setUpdateSources(const QStringList& sources)
{
	settings.setValue("update/sources", sources.join(","));
}

bool SettingsManager::updateKeepBackup() const
{
	return settings.value("update/keepBackup", true).toBool();
}

void SettingsManager::setUpdateKeepBackup(bool keep)
{
	settings.setValue("update/keepBackup", keep);
}

bool SettingsManager::updateUseMirror() const
{
	return settings.value("update/useMirror", true).toBool();
}

void SettingsManager::setUpdateUseMirror(bool use)
{
	settings.setValue("update/useMirror", use);
}

bool SettingsManager::updateTrackPrerelease() const
{
	// 默认跟随当前版本：beta 版默认追踪预发布，正式版默认不追
	const bool currentIsPrerelease = QStringLiteral(BP_VERSION_STRING).contains(QLatin1Char('-'));
	return settings.value("update/trackPrerelease", currentIsPrerelease).toBool();
}

void SettingsManager::setUpdateTrackPrerelease(bool track)
{
	settings.setValue("update/trackPrerelease", track);
}

void SettingsManager::setAllowNativeMods(bool allowed)
{
	settings.setValue("mods/allowNative", allowed);
}

//按屏幕 id 读取窗口几何
QByteArray SettingsManager::screenGeometry(const QString& screenId) const
{
	QString key = "windows/geometry/" + screenId;
	if (settings.contains(key))
	{
		return settings.value(key).toByteArray();
	}

	//兼容旧版本：整体几何键只作为首次读取时的默认值
	return settings.value("windows/geometry").toByteArray();
}

//按屏幕 id 保存窗口几何
void SettingsManager::setScreenGeometry(const QString& screenId, const QByteArray& geometry)
{
	settings.setValue("windows/geometry/" + screenId, geometry);
}

// ---------- 模组开关 ----------

//已启用的模组 id 列表；没设置过时返回空列表（表示"全部按作者建议的默认值"）
QStringList SettingsManager::enabledMods() const
{
	const QString raw = settings.value("mods/enabled").toString().trimmed();
	if (raw.isEmpty()) { return QStringList(); }

	QStringList ids;
	for (const QString& part : raw.split(QLatin1Char(','), Qt::SkipEmptyParts))
	{
		const QString id = part.trimmed();
		if (!id.isEmpty() && !ids.contains(id)) { ids.append(id); }
	}
	return ids;
}

void SettingsManager::setEnabledMods(const QStringList& ids)
{
	settings.setValue("mods/enabled", ids.join(QLatin1Char(',')));
}

//玩家是否动过模组开关（"有没有这条记录"比"列表里有没有某个 id"更重要：
//没记录 = 全新玩家，应该按作者建议的默认值走）
bool SettingsManager::hasModsRecord() const
{
	return settings.contains("mods/enabled");
}

void SettingsManager::setModEnabled(const QString& modId, bool enabled)
{
	QStringList ids = enabledMods();
	if (enabled)
	{
		if (!ids.contains(modId)) { ids.append(modId); }
	}
	else
	{
		ids.removeAll(modId);
	}
	setEnabledMods(ids);
}
