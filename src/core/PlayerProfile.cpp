#include "PlayerProfile.h"
#include "DirectoryLayout.h"
#include "FileManager.h"
#include <QJsonDocument>

//单例访问
PlayerProfile& PlayerProfile::instance()
{
	static PlayerProfile s_instance;
	return s_instance;
}

//加载存档：找不到或格式错误时回落到默认值
bool PlayerProfile::load()
{
	QString path = DirectoryLayout::instance().playerDataPath();

	//文件不存在：直接使用默认值（首次启动会进入注册/欢迎流程）
	if (!FileManager::instance().exists(path))
	{
		m_info.setDefaults();
		m_loadedFromFile = false;
		FileManager::instance().log("profile", "未找到存档文件，使用默认数据");
		return false;
	}

	bool ok = false;
	QJsonObject json = FileManager::instance().readJsonObject(path, QString(), &ok);
	if (!ok || !m_info.loadFromJson(json))
	{
		m_info.setDefaults();
		m_loadedFromFile = false;
		FileManager::instance().log("profile", QString("存档解析失败，使用默认数据: %1").arg(path));
		return false;
	}

	m_loadedFromFile = true;
	FileManager::instance().log("profile", QString("存档加载成功: %1").arg(m_info.getUserName()));
	return true;
}

//保存存档
bool PlayerProfile::save()
{
	QString path = DirectoryLayout::instance().playerDataPath();
	bool ok = FileManager::instance().writeJson(path, QJsonDocument(m_info.toJson()));
	if (!ok)
	{
		FileManager::instance().log("profile", QString("存档写入失败: %1").arg(path));
	}
	return ok;
}
