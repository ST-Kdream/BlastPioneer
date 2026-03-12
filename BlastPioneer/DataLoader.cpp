#include "DataLoader.h"

//构造函数（无额外操作）
DataLoader::DataLoader(QObject*parent):QObject(parent){}

//预加载函数
void DataLoader::load()
{
	//加载玩家信息
	PlayerInfo playerInfo;
	QString filePath = QCoreApplication::applicationDirPath() + "/Data/playerData.json";
	QFile playerFile(filePath);
	if (playerFile.exists() && playerFile.open(QIODevice::ReadOnly))
	{
		QByteArray data = playerFile.readAll();
		playerFile.close();
		QJsonDocument doc = QJsonDocument::fromJson(data);
		if (doc.isObject())
		{
			playerInfo.loadFromJson(doc.object());
		}
		else
		{
			playerInfo.setDefaults();
		}
	}
	else
	{
		playerInfo.setDefaults();
	}

	//加载商店物品
	QList<Item> items;
	QFile itemFile(":/Data/itemInfo.json");
	if (itemFile.open(QIODevice::ReadOnly))
	{
		QByteArray data = itemFile.readAll();
		itemFile.close();
		QJsonDocument doc = QJsonDocument::fromJson(data);
		if (doc.isArray())
		{
			for (auto value : doc.array())
			{
				QJsonObject obj = value.toObject();
				items.append(Item(obj["name"].toString(),
					obj["description"].toString(),
					obj["price"].toInt(),
					obj["iconPath"].toString()));
			}
		}
	}

	emit finished(playerInfo, items);
}