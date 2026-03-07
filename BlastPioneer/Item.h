#pragma once
#include "PlayerInfo.h"
#include "GameWindow.h"
#include <QString>
#include <QMap>
#include <functional>

class Item
{
private:
	QString name;
	QString description;
	QString iconPath;
	int price;

	static QMap<QString, std::function<bool(GameWindow*)>> usageMap;
	static void initUsageMap();

public:
	//2个构造函数
	Item() = default;
	Item(const QString& name, const QString& description, int price,const QString& iconPath = "");

	//接口函数
	QString getName() const { return name; }
	QString getDescription() const { return description; }
	QString getIconPath() const { return iconPath; }
	int getPrice() const { return price; }

	//使用道具
	bool use(GameWindow* gameWin = nullptr) const;
};
