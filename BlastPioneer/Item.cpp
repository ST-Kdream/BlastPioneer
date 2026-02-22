#include "Item.h"

//构造函数
Item::Item(const QString& name, const QString& description, int price, const QString& iconPath)
{
	this->name = name;
	this->description = description;
	this->price = price;
	this->iconPath = iconPath;
}