#pragma once
#include "GameEngine.h"
#include "MapGenerator.h"

#include <QRandomGenerator>
#include <memory>

namespace bp
{

// ============================================================
// 单人闯关引擎
//
// 规则来源：原来的 GameWindow / SingleGameScreen，逐条搬到这里。
// 视图层（GameCanvas）只读 snapshot() 绘制，不再参与任何规则计算。
//
// 与旧实现的差异（已在阶段三说明）：
//   1. 逻辑固定 60Hz 步长，速度单位改成"格/秒" -> 改帧率不再改速度
//   2. 敌人放弹用"每秒期望次数"代替按帧率硬编码的概率
//   3. 敌人转向带惯性、撞墙立刻换向（旧实现撞墙只清零速度，会被推到墙角）
//   4. 爆炸痕带存续时间，到点自动恢复为空地（旧实现从不恢复）
//   5. 敌人/掉落物延迟销毁（dying + deathTimer），供表现层播动画
// ============================================================
class SingleGameEngine : public GameEngine
{
public:
	explicit SingleGameEngine(const GameConfig& config, QObject* parent = nullptr);

	//替换地图生成器；传 nullptr 表示"回到默认的随机生成器"
	//（界面在"这一关没有 JSON 定义"时会这么用，于是关卡数据化不改变老行为）
	void setMapGenerator(std::unique_ptr<IMapGenerator> generator);

	//开始一局（基类会调用 onReset + onLevelInit）
	//返回 false 表示初始化失败（例如尺寸无关的外部原因）
	bool beginLevel(int level);

	//本局已进行的秒数（限时玩法用）
	double elapsedSeconds() const { return m_elapsedSeconds; }

	//固定本局随机种子（测试/联机重演用）
	//默认（-1）表示每局自动取一个与时间相关的种子，掉落与敌人行为因此每局不同
	void setRandomSeed(qint64 seed);

	//道具效果（彩蛋道具也走这里；效果ID与道具名都接受）
	bool applyEffect(const QString& effectId) override;

	//模组写请求：单人只有 0 号玩家（越界如实拒绝，而不是悄悄改别人）
	bool requestDamagePlayer(int playerIndex, int amount) override;

protected:
	void onReset() override;
	void onLevelInit() override;
	void advance(const FrameContext& ctx) override;

private:
	// ---------- 子系统（每一步按顺序调用） ----------
	void handlePlayerInput(const FrameContext& ctx);
	void updateEnemies(const FrameContext& ctx);
	void updateBombs(const FrameContext& ctx);
	void updateDrops(const FrameContext& ctx);
	void updateDyingEntities(const FrameContext& ctx);
	//返回 true 表示本局已经结束
	bool checkLevelEnd();

	// ---------- 爆炸结算 ----------
	void resolveBlast(const Bomb& bomb);
	void applyBlastToEnemies(const GridPos& cell);
	void applyBlastToPlayer(const GridPos& cell, BombOwner source);

	// ---------- 敌人 ----------
	void pickEnemyDirection(Enemy& enemy, double frameTime);
	bool tryProbeDirection(const Enemy& enemy, int dir, GridPos& outCell, double& outX, double& outY);
	void applyEnemyDirection(Enemy& enemy, int dir);

	// ---------- 掉落 ----------
	//（"生成掉落物 / 抽名字 / 砖块被破坏"已上移到基类，单人多人共用同一份）

	// ---------- 伤害 ----------
	void damagePlayer(int amount);

	// ---------- 坐标换算 ----------
	//把"格/秒"折算成本步的位移（格）
	double cellsPerStep(double cellsPerSecond, double frameTime) const;

	std::unique_ptr<IMapGenerator> m_generator;

	//本局计时
	double m_elapsedSeconds = 0.0;

	//玩家出生点
	GridPos m_playerStart{ 1, 1 };

	//局内可变数值（道具效果会改这些，不污染 GameConfig 的"初始值"）
	double m_speedFactor = 1.0;        //移动速度倍率
	double m_enemySpeedFactor = 1.0;   //敌人速度倍率
	double m_ghostTimer = 0.0;         //电子幽灵态剩余秒数
};

} // namespace bp
