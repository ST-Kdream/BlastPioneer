#pragma once
#include <QString>

class Item
{
private:
	QString name;
	QString description;
	QString iconPath;
	int price;

public:
	Item(const QString& name, const QString& description, int price, const QString& iconPath = "");

	QString getName() const { return name; }
	QString getDescription() const { return description; }
	QString getIconPath() const { return iconPath; }
	int getPrice() const { return price; }
};
