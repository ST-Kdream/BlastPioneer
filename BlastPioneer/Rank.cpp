#include "Rank.h"

//构造函数
Rank::Rank()
{
	rankData.insert(0, "萌弹新手");
	rankData.insert(50, "轻爆学徒");
	rankData.insert(150, "破障炮手");
	rankData.insert(300, "爆破行者");
	rankData.insert(500, "爆弹宗师");
	rankData.insert(800, "核爆统领");
	rankData.insert(1500, "灭世弹神");
}

//返回最新段位名
QString Rank::updateRankName(const int EP) const
{
	QString newRankName=rankData[0];
	for (auto it = rankData.begin(); it != rankData.end(); ++it)
	{
		if (EP >= (it.key()))
		{
			newRankName = it.value();
		}
		else { break; }
	}
	return newRankName;
}
