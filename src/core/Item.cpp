#include "Item.h"

//构造函数
Item::Item(const QString& name, const QString& description, int price, const QString& iconPath,
		   const QString& effect, double dropWeight)
{
	this->name = name;
	this->description = description;
	this->price = price;
	this->iconPath = iconPath;
	this->effect = effect;
	this->dropWeight = dropWeight;
}

//说明：道具"效果"不在这里实现
//  阶段三起，效果统一由 bp::GameEngine::applyEffect(effectId) 负责，
//  这里只保存"效果ID"（itemInfo.json 的 effect 字段）。
//  阶段二起，映射的优先级是：
//    itemInfo.json 的 effect 字段  >  ItemEffects.cpp 的内置静态映射（兼容老数据）
//  所以以后加道具 = 改 JSON + 在引擎效果表里加一条，不用动数据层。
