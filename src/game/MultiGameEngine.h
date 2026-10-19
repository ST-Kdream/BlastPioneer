#pragma once
#include "GameEngine.h"
#include "MapGenerator.h"

#include <memory>

namespace bp
{

// ============================================================
// 多人对局里的一个玩家（引擎侧状态）
//
// 坐标同样是"逻辑格子 + 格内偏移"，引擎里没有像素。
// 位置/生命/炸弹上限都由主机权威计算，客户端只是把主机广播的状态套进来。
// ============================================================
struct MultiPlayer
{
	int      id = 0;
	QString  name;
	GridPosF pos;
	int      lives = 3;
	int      maxLives = 3;
	int      bombRange = 1;
	int      maxBombs = 3;

	//最近一次收到的输入（主机用它推进这一位玩家；客户端不用）
	InputState input;

	//局内可变数值（道具效果改这些，不污染 GameConfig 的初始值）
	double   speedFactor = 1.0;
	bool     ghost = false;       //幽灵态（可穿墙）
	double   ghostTimer = 0.0;    //幽灵态剩余秒数

	bool alive() const { return lives > 0; }
};

// ============================================================
// 多人对战引擎
//
// 规则来源：原来的 MultiGameScreen（窗口里那份 host 逻辑），逐条搬到这里。
// 与旧实现的差异（都是"搬进引擎"过程中顺手修掉的）：
//   1. 逻辑固定 60Hz、速度用"格/秒" —— 改渲染帧率不再改移动速度
//   2. 地图用 GameBoard + RandomMapGenerator（和单人同一套棋盘/生成器），
//      不再各写一份 int map[ROWS][COLS]
//   3. 对局种子由大厅广播，主机与客户端的地图必然一致（旧实现靠写死 seed=42）
//   4. 砖块会被炸开、会掉道具（旧实现炸弹只清子弹不破砖、没有道具）
//   5. 胜负判定留在引擎里，界面只负责弹个结果框
//
// 网络不进引擎：收发 UDP 是 MultiGameScreen 的事，引擎只暴露
//   submitRemoteInput() / applyRemotePlayers() / applyRemoteBombs() 这几扇门
// —— 于是"多人规则"可以像单人一样脱机单测（tests.pro 里就是这么测的）
// ============================================================
class MultiGameEngine : public GameEngine
{
public:
	explicit MultiGameEngine(const GameConfig& config, QObject* parent = nullptr);

	//替换地图生成器（接 JSON 关卡 / mod 地图时用）
	void setMapGenerator(std::unique_ptr<IMapGenerator> generator);

	// ---------- 参与者（界面从 NetSession 灌进来） ----------
	void clearPlayers();
	void addPlayer(int id, const QString& name);
	void setLocalPlayerId(int id) { m_localPlayerId = id; }
	int  localPlayerId() const { return m_localPlayerId; }
	const QList<MultiPlayer>& players() const { return m_players; }
	int  aliveCount() const;
	QString playerName(int id) const;

	//出生点（默认四个角落；自制关卡/测试可以自己指定）
	void setSpawnPoints(const QList<GridPos>& points);
	QList<GridPos> effectiveSpawnPoints() const;

	//是否为"权威端"：true = 跑规则（主机），false = 只镜像主机状态（客户端）
	void setAuthoritative(bool authoritative) { m_authoritative = authoritative; }
	bool isAuthoritative() const { return m_authoritative; }

	//开始对局（重新生成地图、按出生点摆人、清空炸弹与掉落）
	bool beginMatch();

	//对局种子：主机与所有客户端必须一致，否则两边地图不一样
	//界面从 NetSession 拿到大厅广播的种子后传进来；<0 表示"用默认种子"
	void setRandomSeed(qint64 seed);
	quint32 matchSeed() const { return m_matchSeed; }

	// ---------- 比赛计时（界面显示/统计用） ----------
	double elapsedSeconds() const { return m_elapsedSeconds; }

	// ---------- 输入（主机侧） ----------
	//客户端发来的输入、以及主机自己的输入，都从这里进来
	void submitRemoteInput(int playerId, const InputState& input);
	//某位玩家请求放炸弹（输入里的 placeBomb 也会触发同一段逻辑）
	bool tryPlaceBombFor(int playerId);

	// ---------- 客户端：直接套用主机广播的状态（不跑规则） ----------
	void applyRemotePlayers(const QList<int>& ids, const QList<GridPosF>& positions, const QList<int>& lives);
	void applyRemoteBombs(const QList<Bomb>& bombs);
	void applyRemoteDrops(const QList<DropItem>& drops);

	// ---------- 胜负 ----------
	//主机判定出结果后调用；客户端收到 MSG_GAME_OVER 时也调用它
	//winnerId = -1 表示平局（例如同归于尽）
	void declareWinner(int winnerId);
	int  winnerId() const { return m_winnerId; }

	// ---------- 道具效果 ----------
	//多人里"用道具的人"必须显式指定，所以主入口是带 playerId 的那个
	bool applyEffectToPlayer(int playerId, const QString& effectId);
	//基类接口：作用于本机玩家
	bool applyEffect(const QString& effectId) override;

	//模组写请求：按玩家 id 造成伤害（走与爆炸同一条路径：生命、事件、结算都一致）
	bool requestDamagePlayer(int playerIndex, int amount) override;

protected:
	void onReset() override;
	void onLevelInit() override;
	void advance(const FrameContext& ctx) override;

private:
	MultiPlayer*       findPlayer(int id);
	const MultiPlayer* findPlayer(int id) const;

	void rebuildPlayerViews();
	void updateGhostTimers(const FrameContext& ctx);
	void movePlayers(const FrameContext& ctx);
	void updateBombs(const FrameContext& ctx);
	void resolveBlast(const Bomb& bomb);
	void damagePlayerAt(int playerId, int amount, const GridPos& cell);
	void updateDrops(const FrameContext& ctx);
	void checkMatchEnd();

	std::unique_ptr<IMapGenerator> m_generator;

	QList<MultiPlayer> m_players;
	QList<GridPos>     m_spawnPoints;

	int     m_localPlayerId = 0;
	int     m_winnerId = -1;
	bool    m_authoritative = true;
	quint32 m_matchSeed = 0;
	double  m_elapsedSeconds = 0.0;
};

} // namespace bp
