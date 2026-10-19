#pragma once
#include <QString>
#include <QHash>

//图片资源标识：所有界面只认这个枚举，不再各自拼图片路径
//以后换成压缩包 / 图集时，只需修改 FigCatalog 与 FigManager，界面代码零改动
enum class Fig
{
	Player,
	Enemy,
	Bomb,
	LiveBottle,
	QuickBoot,
	BombUpper,
	SuperBag,
	StringUnravelEye,
	StringUnravelEyeBroken,
	BonVoyagingStar,
	BonVoyagingStarBroken
};

//图片目录：标识 -> 相对于 Assets 的路径
namespace FigCatalog
{
	//返回相对路径，如 "picture/player.png"；未知标识返回空串
	QString relativePath(Fig id);

	//按道具名取图标标识（列表图标也走 FigManager 缓存，不再每次读盘 + 缩放）
	//返回 false 表示这个名字没有对应图标
	bool itemFig(const QString& itemName, Fig& outFig);
}
