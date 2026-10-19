#pragma once
#include "GameTypes.h"
#include <QList>
#include <memory>

namespace bp
{

class GameBoard;

//关卡生成参数
struct LevelSpec
{
	int      level = 1;
	int      brickCount = 0;
	int      enemyCount = 0;
	int      enemyHp = 3;
	quint32  seed = 0;             //随机种子（相同种子必须产出相同地图）
	GridPos  playerStart = GridPos(1, 1);
};

//生成出的敌人初始数据
struct EnemySpawn
{
	GridPos pos;
	int     hp = 3;
};

//地图生成结果
struct GeneratedMap
{
	QList<EnemySpawn> enemies;
	bool ok = false;

	//生成器建议的玩家出生点
	//为什么放在结果里而不是只读 spec：手写地图（关卡 JSON 的 'P' 字符）里
	//出生点是画在地图上的，只有生成器知道它在哪
	GridPos playerStart = GridPos(1, 1);
	bool    hasPlayerStart = false;

	//本张地图是否来自"手写布局"（false = 程序化/随机生成）
	//用于日志与测试确认"到底走了哪条路"
	bool    usedLayout = false;
};

// ============================================================
// 地图生成器接口：把"地图从哪来"和"规则怎么跑"解耦
// 以后要换成手写关卡 JSON / mod 自定义地图，只需实现这个接口
// ============================================================
class IMapGenerator
{
public:
	virtual ~IMapGenerator() = default;

	//把地图铺进 board，并返回敌人初始位置
	virtual GeneratedMap generate(GameBoard& board, const LevelSpec& spec) = 0;

	//生成器名字（日志用）
	virtual QString name() const = 0;
};

// ============================================================
// 默认生成器：随机铺砖块 + 随机撒敌人
// 行为与旧实现保持一致（同样的种子得到同样的地图），只是搬了位置
// ============================================================
class RandomMapGenerator : public IMapGenerator
{
public:
	GeneratedMap generate(GameBoard& board, const LevelSpec& spec) override;
	QString name() const override { return QStringLiteral("RandomMapGenerator"); }
};

// ============================================================
// 手写地图生成器：见 LevelData.h 的 JsonMapGenerator
// （关卡数据化之后就落在 game/LevelData.* 里，与关卡定义放在一起）
// ============================================================

} // namespace bp
