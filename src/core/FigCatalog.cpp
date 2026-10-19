#include "FigCatalog.h"

//图片标识与文件名的对应表
QString FigCatalog::relativePath(Fig id)
{
	switch (id)
	{
	case Fig::Player:                 return "picture/player.png";
	case Fig::Enemy:                  return "picture/enemy.png";
	case Fig::Bomb:                   return "picture/bomb.png";
	case Fig::LiveBottle:             return "picture/liveBottle.png";
	case Fig::QuickBoot:              return "picture/quickBoots.png";
	case Fig::BombUpper:              return "picture/bombUpper.png";
	case Fig::SuperBag:               return "picture/superBag.png";
	case Fig::StringUnravelEye:       return "picture/stringUnravelEye.png";
	case Fig::StringUnravelEyeBroken: return "picture/stringUnravelEye_broken.png";
	case Fig::BonVoyagingStar:        return "picture/bonVoyagingStar.png";
	case Fig::BonVoyagingStarBroken:  return "picture/bonVoyagingStar_broken.png";
	default:                          return QString();
	}
}

//按道具名取图标标识
//名字来自 Data/itemInfo.json。做这份映射是为了让"列表图标"也能复用 FigManager 的缓存，
//而不是像原来那样每个道具都 QPixmap(路径) + scaled() 现场读盘
bool FigCatalog::itemFig(const QString& itemName, Fig& outFig)
{
	if (itemName == "生命药水")           { outFig = Fig::LiveBottle;             return true; }
	if (itemName == "迅猛之靴")           { outFig = Fig::QuickBoot;              return true; }
	if (itemName == "改造扳手")           { outFig = Fig::BombUpper;              return true; }
	if (itemName == "超级背包")           { outFig = Fig::SuperBag;               return true; }
	if (itemName == "解弦之眼（破损版）") { outFig = Fig::StringUnravelEyeBroken;  return true; }
	if (itemName == "远航星（破损版）")   { outFig = Fig::BonVoyagingStarBroken;   return true; }
	if (itemName == "解弦之眼")           { outFig = Fig::StringUnravelEye;       return true; }
	if (itemName == "远航星")             { outFig = Fig::BonVoyagingStar;        return true; }
	return false;
}
