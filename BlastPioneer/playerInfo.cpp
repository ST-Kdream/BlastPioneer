#include "playerInfo.h"

//构造函数
playerInfo::playerInfo()
{
	setDefaults();
}

bool playerInfo::loadFromJson(const QJsonObject& json)
{
	if (!json.contains("userName"))  //检查json文件是否包含用户名字段
	{
		return false;
	}

	userName = json["userName"].toString();
	rank = json["rank"].toString();
	EP = json["EP"].toInt(100);
	passedLevels = json["passedLevels"].toInt(1);
	coins = json["coins"].toInt(100);

	QString strCreateDate = json["createDate"].toString();
	QString strLastLogin = json["lastLogin"].toString();
	if (!strCreateDate.isEmpty())
	{
		createDate = QDateTime::fromString(strCreateDate, Qt::ISODate);
	}
	if (!strLastLogin.isEmpty())
	{
		lastLogin = QDateTime::fromString(strLastLogin, Qt::ISODate);
	}
}