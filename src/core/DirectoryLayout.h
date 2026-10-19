#pragma once
#include <QString>

//目录布局单例：统一解析所有运行时路径
//解析优先级：--root=<目录> 命令行 > 环境变量 BP_ROOT > exe 所在目录
//Data/ 与 Assets/ 始终与 exe 同级，保证与旧版本、旧存档完全兼容
class DirectoryLayout
{
private:
	DirectoryLayout() = default;

	QString m_root;

public:
	//单例访问
	static DirectoryLayout& instance();

	//由 main() 在 QApplication 之后、其余初始化之前调用一次
	void resolve(const QString& rootOverride = QString());

	//根目录与各子目录
	QString root()      const { return m_root; }
	QString dataDir()   const { return m_root + "/Data"; }
	QString assetsDir() const { return m_root + "/Assets"; }
	QString uiDir()     const { return m_root + "/ui"; }
	QString modsDir()   const { return m_root + "/mods"; }
	QString cacheDir()  const { return m_root + "/cache"; }
	QString logsDir()   const { return m_root + "/logs"; }

	//常用文件的绝对路径
	QString playerDataPath() const { return dataDir() + "/playerData.json"; }
	QString settingsPath()   const { return dataDir() + "/settings.ini"; }
	QString itemInfoPath()   const { return dataDir() + "/itemInfo.json"; }
	//关卡目录：一关一个 JSON（Data/levels/level_01.json ...）
	//注意它属于"内容"，不进 ensureRuntimeDirs 的创建清单：
	//目录缺失时游戏会自动退回程序化关卡，而不是生成一个空目录假装有内容
	QString levelsDir()      const { return dataDir() + "/levels"; }
	QString globalQssPath()  const { return uiDir() + "/global.qss"; }
	QString gameLogPath()    const { return logsDir() + "/game.log"; }

	//目录不存在时按需创建（含多级），返回是否已存在/创建成功
	bool ensureDir(const QString& dir) const;

	//确保所有运行时目录存在（Data/Assets/mods/cache/logs）
	void ensureRuntimeDirs() const;
};
