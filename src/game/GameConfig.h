#pragma once
#include <QString>
#include <QStringList>

// ============================================================
// 游戏数值配置：原先散落在界面代码里的"魔法数字"全部集中在这里
// 单位说明：速度单位是"格/秒"，时间单位是"秒"
// 好处：① 改数值只改一处；② mod 以后只需改这里的字段；③ 与帧率完全解耦
//
// 注意：本类型属于引擎层（namespace bp），与 GameBoard / GameEngine 保持一致
// ============================================================
namespace bp
{

struct GameConfig
{
	// ---------- 玩家 ----------
	double playerSpeed = 3.5;          //格/秒（原 playerSpeed=3.5，配合原 1/frameRate 等价）
	int    playerMaxLives = 3;         //初始生命上限
	//爆炸威力 = 从中心向四个方向各延伸多少格（不包含中心格）
	//所以"一格宽"的十字总跨度 = 1 + 2*range：
	//  range = 1 -> 3 格宽     range = 2 -> 5 格宽（旧版用的 2，观感偏大）
	int    playerBombRange = 1;        //初始炸弹威力
	int    playerMaxBombs = 3;         //同时可放置的炸弹数
	double playerBombFuse = 1.5;       //玩家炸弹引信（秒）

	// ---------- 敌人 ----------
	double enemySpeed = 3.5;           //格/秒
	double enemyBombFuse = 3.0;        //敌人炸弹引信（秒）
	int    enemyBombRange = 1;         //敌人炸弹威力（口径同 playerBombRange）
	double enemyBombCooldown = 3.0;    //放弹后的冷却（秒）
	double enemyBombsPerSecond = 0.4;  //每秒期望放弹次数（原为"每帧 40%/可见帧率"，现与帧率解耦）

	//敌人转向模型（替代原来的"每帧 5% 纯随机换向"）：
	//纯随机会在窄缝里疯狂抖动、看起来像抽搐，这里改成"带惯性的游走"
	double enemyTurnChancePerSecond = 1.2;  //每秒期望主动换向次数（原值折算约 3.0，调低后更像游走）
	double enemyReversePenalty = 0.15;      //掉头（反向）的权重倍率，越低越不轻易掉头
	int    enemyRetryProbeCount = 8;        //撞墙后最多试探几次换向

	// ---------- 地图 ----------
	int brickBaseCount = 20;           //砖块基数（原 20 + level*5）
	int brickPerLevel = 5;
	int enemyCountPerLevel = 1;        //敌人数量 = level * 该值
	int enemyHpBase = 3;               //敌人生命 = base + level / hpDiv
	int enemyHpLevelDiv = 2;

	// ---------- 掉落 ----------
	double dropChance = 0.15;          //砖块被破坏后掉落的基础概率
	double explosionPersistSeconds = 0.3;  //爆炸痕存续时间（原代码从不恢复，这里修掉）
	double dropLifetimeSeconds = 0.0;      //掉落物存在时长（0 = 永久，与原行为一致）

	//爆炸前多少秒开始给"会被炸到的格子"发预警（视图据此闪格）
	//放在配置里而不是视图里：视图不该知道"几秒算倒数"，它只负责把引擎给的格子画出来
	double warningSeconds = 0.5;

	//可掉落道具名（空 = 使用 ItemDatabase 中 dropWeight>0 的道具）
	//保留这份默认列表是为了和当前手感一致：只有这 4 个会掉
	QStringList droppableItems = { QStringLiteral("生命药水"), QStringLiteral("迅猛之靴"),
								   QStringLiteral("改造扳手"), QStringLiteral("超级背包") };

	//彩蛋道具权重（道具名 -> 相对权重）：命中"掉落"后先按权重决定出哪个
	//默认把 4 个强力道具设为极低权重的彩蛋
	QList<QPair<QString, double>> rareDropWeights = {
		{ QStringLiteral("生命药水"), 40.0 },
		{ QStringLiteral("迅猛之靴"), 25.0 },
		{ QStringLiteral("改造扳手"), 20.0 },
		{ QStringLiteral("超级背包"), 13.0 },
		{ QStringLiteral("解弦之眼（破损版）"), 0.8 },
		{ QStringLiteral("远航星（破损版）"), 0.8 },
		{ QStringLiteral("解弦之眼"), 0.2 },
		{ QStringLiteral("远航星"), 0.2 }
	};

	// ---------- 关卡 ----------
	//单局限时（秒）；<= 0 表示不限时
	double levelTimeoutSeconds = 0.0;

	// ---------- 多人对战 ----------
	//自己的炸弹是否炸到自己。
	//旧多人实现里有一句"if (it.key() == bomb.ownerId) continue;"，也就是完全不炸自己；
	//为了不改动既有手感，默认保持关闭。想改成经典炸弹人规则，把它打开即可
	bool multiSelfDamage = false;

	// ---------- 奖励公式 ----------
	int rewardCoinsBase = 50;          //金币 = base + perLevel * level
	int rewardCoinsPerLevel = 5;
	int rewardEpBase = 0;              //经验 = base + perLevel * level
	int rewardEpPerLevel = 10;

	// ---------- 关卡公式 ----------
	int bricksForLevel(int level) const      { return brickBaseCount + brickPerLevel * level; }
	int enemiesForLevel(int level) const     { return level * enemyCountPerLevel; }
	int enemyHpForLevel(int level) const     { return enemyHpBase + level / (enemyHpLevelDiv > 0 ? enemyHpLevelDiv : 1); }
	int coinsForLevel(int level) const       { return rewardCoinsBase + rewardCoinsPerLevel * level; }
	int epForLevel(int level) const          { return rewardEpBase + rewardEpPerLevel * level; }
};

} // namespace bp
