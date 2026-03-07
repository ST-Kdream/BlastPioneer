#include "Item.h"

//构造函数
Item::Item(const QString& name, const QString& description, int price, const QString& iconPath)
{
	this->name = name;
	this->description = description;
	this->price = price;
	this->iconPath = iconPath;
	initUsageMap();
}

//初始化映射表
void Item::initUsageMap()
{
	if (!usageMap.isEmpty()) { return; }

	usageMap["生命药水"] = [](GameWindow* gameWin)->bool
		{
			if (!gameWin) { return false; }
			gameWin->addPlayerLives();
			return true;
		};

	usageMap["迅猛之靴"] = [](GameWindow* gameWin)->bool
		{
			if (!gameWin) { return false; }
			gameWin->increaseSpeed();
			return true;
		};

	usageMap["改造扳手"] = [](GameWindow* gameWin)->bool
		{
			if (!gameWin) { return false; }
			gameWin->upBombRange();
			return true;
		};

	usageMap["超级背包"] = [](GameWindow* gameWin)->bool
		{
			if (!gameWin) { return false; }
			gameWin->increaseMaxPlace();
			return true;
		};

	usageMap["解弦之眼（破损版）"] = [](GameWindow* gameWin)->bool
		{
			if (!gameWin) { return false; }
			gameWin->stringUnraveEye_broken();
			return true;
		};

	usageMap["远航星（破损版）"] = [](GameWindow* gameWin)->bool
		{
			if (!gameWin) { return false; }
			gameWin->bonVoyagingStar_broken();
			return true;
		};

	usageMap["解弦之眼"] = [](GameWindow* gameWin)->bool
		{
			if (!gameWin) { return false; }
			gameWin->stringUnraveEye();
			return true;
		};

	usageMap["远航星"] = [](GameWindow* gameWin)->bool
		{
			if (!gameWin) { return false; }
			gameWin->bonVoyagingStar();
			return true;
		};
}

//使用函数
bool Item::use(GameWindow* gameWin) const
{
	auto it = usageMap.find(name);
	if (it != usageMap.end()) { return it.value()(gameWin); }
	return false;
}