#pragma once
#include <QString>
#include <QStringList>

// ============================================================
// 道具效果标识
//
// 设计意图：
//   引擎只认"效果ID"，不认道具名。itemInfo.json 里给每个道具写一个 effect 字段，
//   这样"道具叫什么/卖多少钱/能不能买"是数据，"效果做什么"是引擎 —— 两者解耦。
//   以后加彩蛋道具：改 JSON + 在效果表里加一条，不用动引擎分派逻辑。
//
// 现阶段 itemInfo.json 还没有 effect 字段，所以 applyEffect 同时接受道具名
// （见 SingleGameEngine::applyEffect），保证界面可以立刻接上。
// ============================================================
namespace bp
{
	namespace EffectId
	{
		//基础道具
		inline const QString Heal = QStringLiteral("heal");                 //生命药水：回复生命
		inline const QString SpeedUp = QStringLiteral("speed_up");          //迅猛之靴：自身加速
		inline const QString BombRangeUp = QStringLiteral("bomb_range_up"); //改造扳手：爆炸范围+1
		inline const QString BombCapacityUp = QStringLiteral("bomb_capacity_up"); //超级背包：可放炸弹+1

		//彩蛋道具
		inline const QString EyeBroken = QStringLiteral("eye_broken");      //解弦之眼（破损版）
		inline const QString Eye = QStringLiteral("eye");                   //解弦之眼
		inline const QString StarBroken = QStringLiteral("star_broken");    //远航星（破损版）
		inline const QString Star = QStringLiteral("star");                 //远航星
	}

	//把道具名翻译成效果ID（itemInfo.json 里有 effect 字段时优先用那个）
	//返回空串表示没有对应效果
	QString effectIdForItemName(const QString& itemName);
}
