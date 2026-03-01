#pragma once
#include <QSettings>
#include <QString>

class SettingsManager
{
private:
	SettingsManager();
	~SettingsManager() = default;
	static SettingsManager* m_instance;
	QSettings settings;

public:
	static SettingsManager* instance();  //设置单例访问

    //是否开启粒子特效
    bool showStartupEffect() const;
    void setShowStartupEffect(bool isShow);
    //特效质量
    int effectQuality() const;           // 0低 1中 2高
    void setEffectQuality(int q);
    //帧率
    int frameRate() const;
    void setFrameRate(int fps);
    //音量
    int volume() const;
    void setVolume(int vol);
    // 窗口位置与大小
    void saveWindowGeometry(const QByteArray& geometry);
    QByteArray loadWindowGeometry() const;
};