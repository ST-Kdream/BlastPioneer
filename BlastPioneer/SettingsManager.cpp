#include "SettingsManager.h"

SettingsManager* SettingsManager::m_instance = nullptr;

//构造函数
SettingsManager::SettingsManager() :settings(QDir(QCoreApplication::applicationDirPath()).
    filePath("Data/settings.ini"),QSettings::IniFormat) { }

//单例模式设置
SettingsManager* SettingsManager::instance()
{
	if (!m_instance) { m_instance = new SettingsManager; }
	return m_instance;
}

//接口函数
bool SettingsManager::showStartupEffect() const
{
    return settings.value("ui/showStartupEffect", true).toBool();
}

void SettingsManager::setShowStartupEffect(bool isShow)
{
    settings.setValue("ui/showStartupEffect", isShow);
}

bool SettingsManager::showGrid() const
{
    return settings.value("game/showGrid", true).toBool();
}

void SettingsManager::setShowGrid(bool isShow)
{
    settings.setValue("game/showGrid", isShow);
}

int SettingsManager::effectQuality() const
{
    return settings.value("graphics/effectQuality", 2).toInt();
}

void SettingsManager::setEffectQuality(int q)
{
    settings.setValue("graphics/effectQuality", q);
}

int SettingsManager::frameRate() const
{
    return settings.value("graphics/frameRate", 60).toInt();
}

void SettingsManager::setFrameRate(int fps)
{
    settings.setValue("graphics/frameRate", fps);
}

int SettingsManager::volume() const
{
    return settings.value("audio/volume", 80).toInt();
}

void SettingsManager::setVolume(int vol)
{
    settings.setValue("audio/volume", vol);
}

void SettingsManager::saveWindowGeometry(const QByteArray& geometry)
{
    settings.setValue("windows/geometry", geometry);
}

QByteArray SettingsManager::loadWindowGeometry() const
{
    return settings.value("windows/geometry").toByteArray();
}