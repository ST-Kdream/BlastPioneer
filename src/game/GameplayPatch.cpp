#include "GameplayPatch.h"

#include <functional>

namespace bp
{

namespace
{

//一条可覆盖的键：键名 + 是不是布尔开关 + 怎么写到 GameConfig 上
struct PatchEntry
{
	const char* key;
	bool        flag;
	std::function<void(GameConfig&, double)> apply;
};

//白名单表：**增删字段只改这里**（其它地方一律通过键名查表，不写 if 链）
//顺序按"玩家 -> 敌人 -> 地图 -> 掉落 -> 关卡 -> 奖励"排，方便读
const QList<PatchEntry>& patchTable()
{
	static const QList<PatchEntry> table =
	{
		// ---------- 玩家 ----------
		{ "playerSpeed",          false, [](GameConfig& c, double v) { c.playerSpeed = v; } },
		{ "playerMaxLives",       false, [](GameConfig& c, double v) { c.playerMaxLives = static_cast<int>(v); } },
		{ "playerBombRange",      false, [](GameConfig& c, double v) { c.playerBombRange = static_cast<int>(v); } },
		{ "playerMaxBombs",       false, [](GameConfig& c, double v) { c.playerMaxBombs = static_cast<int>(v); } },
		{ "playerBombFuse",       false, [](GameConfig& c, double v) { c.playerBombFuse = v; } },

		// ---------- 敌人 ----------
		{ "enemySpeed",           false, [](GameConfig& c, double v) { c.enemySpeed = v; } },
		{ "enemyBombFuse",        false, [](GameConfig& c, double v) { c.enemyBombFuse = v; } },
		{ "enemyBombRange",       false, [](GameConfig& c, double v) { c.enemyBombRange = static_cast<int>(v); } },
		{ "enemyBombCooldown",    false, [](GameConfig& c, double v) { c.enemyBombCooldown = v; } },
		{ "enemyBombsPerSecond",  false, [](GameConfig& c, double v) { c.enemyBombsPerSecond = v; } },
		{ "enemyTurnChancePerSecond", false, [](GameConfig& c, double v) { c.enemyTurnChancePerSecond = v; } },
		{ "enemyReversePenalty",  false, [](GameConfig& c, double v) { c.enemyReversePenalty = v; } },
		{ "enemyRetryProbeCount", false, [](GameConfig& c, double v) { c.enemyRetryProbeCount = static_cast<int>(v); } },

		// ---------- 地图 ----------
		{ "brickBaseCount",       false, [](GameConfig& c, double v) { c.brickBaseCount = static_cast<int>(v); } },
		{ "brickPerLevel",        false, [](GameConfig& c, double v) { c.brickPerLevel = static_cast<int>(v); } },
		{ "enemyCountPerLevel",   false, [](GameConfig& c, double v) { c.enemyCountPerLevel = static_cast<int>(v); } },
		{ "enemyHpBase",          false, [](GameConfig& c, double v) { c.enemyHpBase = static_cast<int>(v); } },
		{ "enemyHpLevelDiv",      false, [](GameConfig& c, double v) { c.enemyHpLevelDiv = static_cast<int>(v); } },

		// ---------- 掉落与表现计时 ----------
		{ "dropChance",           false, [](GameConfig& c, double v) { c.dropChance = v; } },
		{ "explosionPersistSeconds", false, [](GameConfig& c, double v) { c.explosionPersistSeconds = v; } },
		{ "dropLifetimeSeconds",  false, [](GameConfig& c, double v) { c.dropLifetimeSeconds = v; } },
		{ "warningSeconds",       false, [](GameConfig& c, double v) { c.warningSeconds = v; } },

		// ---------- 关卡 ----------
		{ "levelTimeoutSeconds",  false, [](GameConfig& c, double v) { c.levelTimeoutSeconds = v; } },

		// ---------- 奖励公式 ----------
		{ "rewardCoinsBase",      false, [](GameConfig& c, double v) { c.rewardCoinsBase = static_cast<int>(v); } },
		{ "rewardCoinsPerLevel",  false, [](GameConfig& c, double v) { c.rewardCoinsPerLevel = static_cast<int>(v); } },
		{ "rewardEpBase",         false, [](GameConfig& c, double v) { c.rewardEpBase = static_cast<int>(v); } },
		{ "rewardEpPerLevel",     false, [](GameConfig& c, double v) { c.rewardEpPerLevel = static_cast<int>(v); } },

		// ---------- 开关（布尔） ----------
		{ "multiSelfDamage",      true,  [](GameConfig& c, double v) { c.multiSelfDamage = (v != 0.0); } }
	};
	return table;
}

const PatchEntry* findEntry(const QString& key)
{
	for (const PatchEntry& entry : patchTable())
	{
		if (key == QLatin1String(entry.key)) { return &entry; }
	}
	return nullptr;
}

} // namespace

// ---------- 查询 ----------

QStringList gameplayPatchKeys()
{
	QStringList keys;
	for (const PatchEntry& entry : patchTable())
	{
		keys.append(QString::fromLatin1(entry.key));
	}
	return keys;
}

bool isGameplayFlagKey(const QString& key)
{
	const PatchEntry* entry = findEntry(key);
	return entry ? entry->flag : false;
}

bool isGameplayPatchKey(const QString& key)
{
	return findEntry(key) != nullptr;
}

// ---------- 应用 ----------

bool applyGameplayPatch(GameConfig& config, const QString& key, double value)
{
	const PatchEntry* entry = findEntry(key);
	if (!entry) { return false; }

	entry->apply(config, value);
	return true;
}

int applyGameplayPatches(GameConfig& config, const QList<GameplayPatch>& patches, QStringList* problems)
{
	int applied = 0;

	for (const GameplayPatch& patch : patches)
	{
		if (applyGameplayPatch(config, patch.key, patch.value))
		{
			++applied;
			continue;
		}

		//未知键：不静默 —— 作者写错了键名必须能看见
		if (problems)
		{
			const QString source = patch.source.isEmpty() ? QStringLiteral("（未注明来源）") : patch.source;
			problems->append(QStringLiteral("%1: 未知的玩法键「%2」，已忽略").arg(source, patch.key));
		}
	}

	return applied;
}

} // namespace bp
