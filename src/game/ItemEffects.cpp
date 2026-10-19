#include "ItemEffects.h"

namespace bp
{

//道具名 -> 效果ID
//这份映射只是"过渡桥"：等 itemInfo.json 补上 effect 字段后，
//界面直接透传 effect 即可，这里可以删掉
QString effectIdForItemName(const QString& itemName)
{
	//基础道具
	if (itemName == QStringLiteral("生命药水")) { return EffectId::Heal; }
	if (itemName == QStringLiteral("迅猛之靴")) { return EffectId::SpeedUp; }
	if (itemName == QStringLiteral("改造扳手")) { return EffectId::BombRangeUp; }
	if (itemName == QStringLiteral("超级背包")) { return EffectId::BombCapacityUp; }

	//彩蛋道具
	if (itemName == QStringLiteral("解弦之眼（破损版）")) { return EffectId::EyeBroken; }
	if (itemName == QStringLiteral("解弦之眼")) { return EffectId::Eye; }
	if (itemName == QStringLiteral("远航星（破损版）")) { return EffectId::StarBroken; }
	if (itemName == QStringLiteral("远航星")) { return EffectId::Star; }

	return QString();
}

} // namespace bp
