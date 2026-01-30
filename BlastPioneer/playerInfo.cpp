#include "playerInfo.h"

//构造函数
playerInfo::playerInfo()
{
	setDefaults();
}

//从json读取数据
bool playerInfo::loadFromJson(const QJsonObject& json)
{
	if (!json.contains("userName"))  //检查json文件是否包含用户名字段
	{
		return false;
	}

	//基础信息读取
	userName = json["userName"].toString();
	rank = json["rank"].toString();
	EP = json["EP"].toInt(100);
	passedLevels = json["passedLevels"].toInt(1);
	coins = json["coins"].toInt(100);

	//时间读取
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

	//玩家背包读取
	if (json.contains("inventory") && json["inventory"].isObject())
	{
		QJsonObject inventoryObject = json["inventory"].toObject();
		inventory.clear();
		for (auto it = inventoryObject.begin(); it != inventoryObject.end(); ++it)
		{
			QString itemName = it.key();
			int itemCount = it.value().toInt(0);
			if (itemCount > 0)
			{
				inventory[itemName] = itemCount;
			}
		}
	}

	return true;
}

//向json保存数据
QJsonObject playerInfo::toJson() const
{
	QJsonObject json;

	//保存玩家基本信息
	json["userName"] = userName;
	json["rank"] = rank;
	json["EP"] = EP;
	json["passedLevels"] = passedLevels;
	json["coins"] = coins;

	//保存时间
	json["createDate"] = createDate.toString(Qt::ISODate);
	json["lastLogin"] = QDateTime::currentDateTime().toString(Qt::ISODate);

	//保存背包数据
	QJsonObject inventoryObject;
	for (auto it = inventory.begin(); it != inventory.end(); ++it)
	{
		if (it.value() > 0)
		{
			inventoryObject[it.key()] = it.value();
		}
	}
	json["inventory"] = inventoryObject;

	return json;
}

//设定玩家信息的默认值
void playerInfo::setDefaults()
{
	userName = "Bommer";
	rank = "萌弹新手";
	EP = 0;
	passedLevels = 0;
	coins = 100;
	createDate = QDateTime::currentDateTime();
	lastLogin = QDateTime::currentDateTime();

	inventory.clear();

	//新手礼包
	inventory["生命药水"] = 3;
	inventory["迅猛之靴"] = 1;
	inventory["改造扳手"] = 1;
}

//向背包添加物品
void playerInfo::addItem(const QString& itemName, int count)
{
	if (inventory.contains(itemName))
	{
		inventory[itemName] += count;
	}
	else
	{
		inventory[itemName] = count;
	}
}

//向背包减少物品
void playerInfo::removeItem(const QString& itemName, int count)
{
	if (!inventory.contains(itemName))
	{
		QMessageBox::critical(nullptr, "错误", "试图移除不存在的物品");
	}

	int newCount = inventory[itemName] = count;
	if (newCount == 0)
	{
		inventory.remove(itemName);
	}
	else
	{
		inventory[itemName] = newCount;
	}
}