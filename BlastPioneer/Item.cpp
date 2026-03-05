#include "Item.h"

//构造函数
Item::Item(const QString& name, const QString& description, int price, const QString& iconPath)
{
	this->name = name;
	this->description = description;
	this->price = price;
	this->iconPath = iconPath;
}

//使用函数
bool Item::use(GameWindow* gameWin) const
{
	if (name == "生命药水")
	{
		if (gameWin)
		{
			gameWin->addPlayerLives();
			return true;
		}
		return false;
	}
	else if (name == "迅猛之靴")
	{
		if (gameWin)
		{
			gameWin->increaseSpeed();
			return true;
		}
		return false;
	}
	else if (name == "改造扳手")
	{
		if (gameWin)
		{
			gameWin->upBombRange();
			return true;
		}
		return false;
	}
	else
	{
		return false;
	}
}