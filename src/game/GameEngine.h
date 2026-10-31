#pragma once
#include "GameBoard.h"
#include "GameConfig.h"
#include "GameTypes.h"

#include <QObject>
#include <QList>
#include <QRandomGenerator>
#include <functional>

namespace bp
{

//一局游戏的只读快照：表现层只读它，绝不反向改引擎状态
struct GameSnapshot
{
	int    level = 1;
	Phase  phase = Phase::Ready;
	int    stepIndex = 0;

	GridPosF playerPos;
	int      playerLives = 0;
	int      playerMaxLives = 0;
	int      playerBombRange = 0;
	int      playerMaxBombs = 0;
	bool     playerGhost = false;      //电子幽灵态（可穿墙）

	QList<Bomb>     bombs;
	QList<Enemy>    enemies;
	QList<DropItem> drops;

	//整张地形的拷贝（墙 / 砖块 / 爆炸痕 / 空地）
	//为什么要拷一份：视图要画墙和砖块，如果让它去调 board()，就等于把棋盘引用
	//交到表现层手里，"视图只读快照"这条约定立刻破掉。
	//代价是每逻辑步 143 个枚举值的拷贝（约 143 字节 × 60 次/秒 ≈ 8.6 KB/s），可以忽略。
	TileType tiles[kRows][kCols] = {};

	//带边界检查的取格子（越界一律当墙，视图不必自己判断）
	TileType tileAt(const GridPos& p) const
	{
		if (!GameBoard::inBounds(p)) { return TileType::Wall; }
		return tiles[p.row][p.col];
	}

	//每帧事件（表现层用来触发粒子/音效/飘字）；引擎在交付后清空
	//也可以用来做"事件驱动"的测试：例如统计爆炸次数
	QList<GameEvent> events;
	int eventCount(GameEventType type) const
	{
		int count = 0;
		for (const GameEvent& event : events)
		{
			if (event.type == type) { ++count; }
		}
		return count;
	}

	//炸弹归属与引信进度（渲染层据此区分玩家/敌人炸弹的颜色与倒计时）
	//progress 已归一化到 0~1（1 = 刚放下，0 = 即将爆炸）
	struct BombView
	{
		GridPosF  pos;
		BombOwner owner = BombOwner::Player;
		double    progress = 0.0;
		double    fuseTotal = 1.5;         //总引信秒数
		double    remainingSeconds = 1.5;  //剩余秒数（视图直接显示，不必自己算）
		int       range = 2;
		int       ownerId = 0;             //放置者编号（多人按人配色）

		//即将爆炸时会被波及的格子（只在最后 warningSeconds 内填，其它时候为空）
		//为什么由引擎算好放进快照：视图原本要调 board().blastCells() —— 那等于
		//视图伸手进引擎内部查棋盘，"视图只读快照"这条约定就破了。
		//而且这里算一次、那一帧所有渲染都用同一份，比每帧重算更省
		QList<GridPos> warningCells;
	};
	QList<BombView> bombViews;

	//多人对局里的玩家列表（含本机玩家）。
	//单人模式故意保持为空 —— 视图会回落到 playerPos / playerLives 那套"单数字段"，
	//于是"单人是多人的特例"这件事不需要在规则里写分支，改动面也最小
	struct PlayerView
	{
		int      id = 0;
		QString  name;
		GridPosF pos;
		int      lives = 0;
		int      maxLives = 0;
		int      bombRange = 0;
		int      maxBombs = 0;
		bool     alive = true;
		bool     local = false;   //是否本机玩家（视图据此加描边/高亮）
		bool     ghost = false;   //幽灵态（可穿墙，视图半透明化）
	};
	QList<PlayerView> players;

	//本机玩家在 players 里的下标；-1 表示没有（单人模式）
	int localPlayerIndex = -1;

	//上一逻辑帧的敌人位置：渲染插值用
	//为什么需要插值：逻辑固定 60Hz，显示可能是 144Hz，不插值会看到"格子跳动"
	//为什么放进快照：视图只允许有一个数据来源。多一份"辅助数据接口"就等于
	//多一条没人维护的旁路（≤6 个 GridPosF 的拷贝，每逻辑步一次，代价可以忽略）
	QList<GridPosF> previousEnemyPositions;
};

// ============================================================
// 视图唯一需要的东西：一份只读快照
//
// 为什么不让视图直接拿 GameEngine*：
//   那等于把"能改游戏状态"的整张 API（setInput / applyEffect / startLevel……）
//   摊在视图面前。哪天在 paintEvent 里顺手调了其中一个，规则就被界面污染了，
//   而且这种 bug 极难查 —— 画面看着正常，玩法却变了。
//   收成一个接口之后，"视图能不能改游戏状态"在类型层面就没有答案：它拿不到那个方法。
// ============================================================
class IGameSnapshotSource
{
public:
	virtual ~IGameSnapshotSource() = default;
	virtual const GameSnapshot& snapshot() const = 0;
};

// ============================================================
// 游戏引擎基类：只依赖 QtCore，不出现任何界面/像素概念
//
// 子类只需实现三个函数：
//   onLevelInit()  —— 本局开始时的初始化（生成地图、摆玩家）
//   onReset()      —— 回到初始状态（清空实体与计时）
//   advance()      —— 每逻辑步的规则（移动、AI、炸弹……）
// 其余（帧协调、阶段切换、快照交付、事件队列）都在基类里固定不变
//
// 为什么这里不用 Q_OBJECT / 信号：
//   引擎是纯逻辑，用 std::function 回调就够了。不用 moc 的话，
//   测试工程可以直接编译这些 .cpp，不需要跑 moc、也不需要链接 moc 产物，
//   "引擎可脱离界面单独测试"这件事才真正干净。
// ============================================================
class GameEngine : public QObject, public IGameSnapshotSource
{
public:
	explicit GameEngine(const GameConfig& config, QObject* parent = nullptr);

	// ---------- 生命周期 ----------
	//开始一局：先 onReset 再 onLevelInit
	void startLevel(int level);
	//回到初始状态（不产生新地图）
	void resetGame();

	// ---------- 帧协调（由 GameLoop 调用，引擎自己不持有定时器） ----------
	//返回 false 表示引擎已结束（胜利/失败后仍会 tick 一小段用于动画）
	bool tick(double frameTime);

	// ---------- 输入 ----------
	void setInput(const InputState& input);
	const InputState& input() const { return m_input; }

	// ---------- 状态 ----------
	Phase phase() const { return m_phase; }
	void  setPhase(Phase phase);
	bool  isRunning() const { return m_phase == Phase::Running; }
	int   level() const { return m_level; }
	int   stepIndex() const { return m_stepIndex; }

	//替换整套数值配置（开局前调用）
	//用途：L1 数据 mod 的玩法覆盖就是在这里注入的 —— 引擎本身不认识 mod，
	//它只是"拿到一份配置"，至于是默认值还是被覆盖过的值，它不关心
	void setConfig(const GameConfig& config) { m_config = config; }

	//只读快照（表现层每帧读取）
	//实现 IGameSnapshotSource：视图只认这个接口，不认引擎的具体类型
	const GameSnapshot& snapshot() const override { return m_snapshot; }

	// ---------- 道具效果 ----------
	//由效果注册表调用；默认返回 false（子类按需实现）
	virtual bool applyEffect(const QString& effectId);

	// ---------- 资源载入的注入点（引擎不认识文件，也不需要认识） ----------
	//道具名 -> 效果ID 的翻译器。默认用内置静态映射（ItemEffects.cpp）。
	//应用层把它换成"读 itemInfo.json 的 effect 字段"，于是加道具/做 mod
	//只需要改数据，不用碰引擎 —— 这也是阶段五 mod 的前置条件
	void setEffectResolver(std::function<QString(const QString&)> resolver);
	//掉落权重表（道具名 -> 相对权重）。空表 = 使用 GameConfig::rareDropWeights。
	//应用层从 ItemDatabase 读出 JSON 里的 dropWeight 后灌进来
	void setDropTable(const QList<QPair<QString, double>>& table);

	//按"效果ID 或 道具名"解析出最终效果ID（解析不出来返回空串）
	QString resolveEffectId(const QString& nameOrEffectId) const;

	// ---------- L3：模组接管点（注入 std::function，与 setEffectResolver 同一个风格） ----------
	//为什么用注入而不是让模组继承 GameEngine：模组是 C ABI 的 DLL，它拿不到引擎对象；
	//宿主在应用层把 C 函数指针包成 std::function 接进来，引擎本身完全不认识 ABI。
	//
	//敌人思考：每个逻辑步、每个活着的敌人问一次。
	//  返回 1 = 模组给出了这一步的动作（direction 0上1下2左3右、-1 不动；placeBomb 是否放弹），
	//            走位与碰撞**仍由引擎负责** —— 所以模组的 AI 不可能把敌人送进墙里；
	//  返回 0 = 引擎用默认 AI。
	using EnemyThinkHook =
		std::function<int(const Enemy&, const FrameContext&, int& direction, bool& placeBomb)>;
	void setEnemyThinkHook(EnemyThinkHook hook) { m_enemyThinkHook = std::move(hook); }
	bool enemiesTakenOver() const { return static_cast<bool>(m_enemyThinkHook); }
	//问一次模组的敌人 AI：返回 1 = 模组给出了这一步的动作（没接管时返回 0）
	int askEnemyThink(const Enemy& enemy, const FrameContext& ctx, int& direction, bool& placeBomb);
	//把"模组说的方向"落实成一步移动（撞墙则不动）；敌人被接管时由子类调用
	bool stepEnemyInDirection(Enemy& enemy, int direction, const FrameContext& ctx);
	//放一枚敌人炸弹（走引擎原有的上限/冷却校验）
	bool tryPlaceEnemyBomb(Enemy& enemy);

	//胜负判定：每个逻辑步问一次。
	//  返回 -1 = 按引擎默认；0 = 失败；1 = 胜利。reason 可选（显示给玩家的一句话）
	using WinConditionHook = std::function<int(QString* reason)>;
	void setWinConditionHook(WinConditionHook hook) { m_winConditionHook = std::move(hook); }
	bool winTakenOver() const { return static_cast<bool>(m_winConditionHook); }
	//问一次模组的胜负判定：返回 -1 表示"模组没意见"
	int askWinCondition(QString* reason);
	//完全关掉引擎的默认胜负判定（模组声明"我来判"时用）。
	//为什么不是"钩子返回 -1 就交回默认"：一个 0 敌人的模式会立刻被判通关 —— 那是最坑人的默认行为
	void setDefaultWinDisabled(bool disabled) { m_defaultWinDisabled = disabled; }
	bool defaultWinDisabled() const { return m_defaultWinDisabled; }
	//本局结束的原因（给界面显示；空 = 用默认文案）
	const QString& finishReason() const { return m_finishReason; }
	void setFinishReason(const QString& reason) { m_finishReason = reason; }

	// ---------- L3：写请求（引擎保留裁决权，全部做校验） ----------
	//这是模组能碰到的**唯一写入口**：越界、写墙、超上限、游戏已结束一律返回 false。
	//注意它们与 protected 的 *Ref() 是两回事：后者给子类改状态，
	//前者给"外部的、不被信任的调用方"。
	bool requestSpawnEnemy(const GridPos& cell, int hp);
	bool requestRemoveEntity(int entityId);
	bool requestDamageEntity(int entityId, int amount);
	//玩家伤害由子类实现：单人只有 0 号，多人按玩家 id 找
	virtual bool requestDamagePlayer(int playerIndex, int amount);
	bool requestSpawnDrop(const GridPos& cell, const QString& itemName);
	bool requestPlaceBomb(const GridPos& cell, BombOwner owner);
	bool requestSetConfigNumber(const QString& key, double value);

	//按实体 id 找敌人（找不到返回 nullptr；只在同一局内有效）
	Enemy* findEnemyById(int entityId);
	const Enemy* findEnemyById(int entityId) const;
	//活着的敌人数量（不含 dying）
	int aliveEnemyCount() const;
	//分配一个新的实体 id（单调递增，同一局内唯一）
	int nextEntityId() { return ++m_lastEntityId; }

	// ---------- 对外只读查询（供渲染层/测试用） ----------
	const GameBoard& board() const { return m_board; }
	const GameConfig& config() const { return m_config; }
	const GridPosF& playerPos() const { return m_playerPos; }
	int playerLives() const { return m_playerLives; }
	int playerMaxLives() const { return m_playerMaxLives; }
	int playerBombRange() const { return m_playerBombRange; }
	int playerMaxBombs() const { return m_playerMaxBombs; }
	bool playerGhost() const { return m_playerGhost; }
	const QList<Bomb>& bombs() const { return m_bombs; }
	const QList<Enemy>& enemies() const { return m_enemies; }
	const QList<DropItem>& drops() const { return m_drops; }
	//某编号归属者当前在场上的炸弹数（多人用来实现"每人独立上限"，界面也可用来显示剩余额度）
	int bombCountOf(int ownerId) const;

	// ---------- 状态变化回调（替代信号，避免依赖 moc） ----------
	//表现层在这里接管：刷新画面 / 播放音效 / 结算奖励
	void setStateChangedCallback(std::function<void()> callback) { m_onStateChanged = std::move(callback); }
	void setPhaseChangedCallback(std::function<void(Phase)> callback) { m_onPhaseChanged = std::move(callback); }
	void setLevelFinishedCallback(std::function<void(bool)> callback) { m_onLevelFinished = std::move(callback); }

protected:
	// ---------- 子类必须实现 ----------
	virtual void onLevelInit() = 0;
	virtual void onReset() = 0;
	virtual void advance(const FrameContext& ctx) = 0;

	// ---------- 子类可用的共用工具 ----------
	//可写棋盘（子类用）；public 的 board() 只读，供渲染层与测试
	GameBoard& boardRef() { return m_board; }
	//可写配置（子类用）；public 的 config() 只读
	GameConfig& configRef() { return m_config; }
	GridPosF& playerPosRef() { return m_playerPos; }

	//可写实体列表（子类推进规则时用）；public 的是只读版本
	QList<Bomb>& bombsRef() { return m_bombs; }
	QList<Enemy>& enemiesRef() { return m_enemies; }
	QList<DropItem>& dropsRef() { return m_drops; }

	//本逻辑步的时长（由 tick 设置；探路/折算位移时用，避免把步长硬编码成 1/60）
	double currentFrameTime() const { return m_currentFrameTime; }

	//尝试按"格/秒"移动玩家：逐轴分别判定，保持原来的自由移动手感
	//dxCells/dyCells 是本次位移（单位：格）
	void movePlayerBy(double dxCells, double dyCells);

	//把任意一个"格子 + 格内偏移"位置逐轴移动（多人模式下每个玩家都走这里）
	//ghost = true 表示无视墙体（电子幽灵态）；返回 true 表示所在格子变了
	bool moveEntityBy(GridPosF& pos, double dxCells, double dyCells, bool ghost);

	// ---- 反卡死 ----
	//幽灵态（可穿墙）结束时人可能正站在墙/砖块里：那之后四面都可能被堵死，
	//表现就是"人卡在墙里动不了、只能重开"。这里把实体挤到最近的空地（BFS，最多 maxSteps 步）。
	//返回 true 表示位置被修正过。
	bool nudgeToWalkable(GridPosF& pos, int maxSteps = 4);
	//对玩家做上面这件事，并同步棋盘上的实体标记。每帧开头调一次（可走时是 O(1) 直接返回）
	bool unstickPlayer(int maxSteps = 4);

	//放置炸弹（会做"同格已有炸弹""超过上限"的检查）
	bool tryPlaceBomb(const GridPos& cell, BombOwner owner);

	//能否在这一格放炸弹：格内没有炸弹、且该格可通行
	bool canPlaceBombAt(const GridPos& cell) const;
	//真正加上一枚炸弹（写棋盘标记 + 抛 BombPlaced 事件），返回是否成功
	bool addBomb(const GridPos& cell, BombOwner owner, int ownerId, double fuse, int range);

	//引爆一枚炸弹：把归属格清成空地，并返回波及格
	QList<GridPos> detonateBomb(const Bomb& bomb);

	//把某格标记为爆炸痕（带存活时间，到点由 updateExplosions 恢复）
	void markExplosion(const GridPos& cell);

	//砖块被破坏：清掉砖块，并按 dropChance 决定是否掉道具（空地只留爆炸痕）
	//单人/多人共用同一份实现，避免两套"砖块怎么炸"各修一遍
	void destroyBrick(const GridPos& cell);
	//在指定格生成一个掉落物（名字由 rollDropName 按权重表抽）
	void spawnDrop(const GridPos& cell);
	//按权重表抽一个掉落物名（权重表见 setDropTable / GameConfig::rareDropWeights）
	QString rollDropName();
	//掉落物通用推进：限时消失 + 清掉已经消失的
	void updateDropLifetimes(double frameTime);

	//每秒一次的概率判定（把"每秒期望次数"折算成本步概率，与帧率无关）
	//用引擎自己的随机源，保证固定种子下可复现（不要用全局随机源，否则跨局不可复现）
	bool chancePerSecond(double timesPerSecond, double frameTime);
	//每秒一次的泊松近似判定（用于放弹这类"期望频率"）
	bool poissonPerSecond(double timesPerSecond, double frameTime);

	//设置本局随机种子（固定种子 = 可复现；测试与联机重演用）
	void setPlaySeed(quint32 seed) { m_rngSeed = seed; m_playSeedFixed = true; m_rng.seed(seed); }
	//取消固定种子：之后每局自动取时间相关种子
	void clearPlaySeed() { m_playSeedFixed = false; m_rng = QRandomGenerator::securelySeeded(); }
	bool hasFixedPlaySeed() const { return m_playSeedFixed; }
	QRandomGenerator& rng() { return m_rng; }

	//开局的随机源准备：没设固定种子时，用"时间 ^ 关卡"生成一个并播种
	void preparePlaySeed(int level);

	//抛出事件（表现层用）
	void emitEvent(GameEventType type, const GridPos& cell, int amount = 0,
				   const QString& text = QString(), int ownerId = 0);

	//状态读写（子类改这些字段）
	Phase  m_phase = Phase::Ready;
	int    m_level = 0;
	int    m_stepIndex = 0;
	GridPosF m_playerPos;
	int    m_playerLives = 3;
	int    m_playerMaxLives = 3;
	int    m_playerBombRange = 2;
	int    m_playerMaxBombs = 3;
	bool   m_playerGhost = false;
	QList<Bomb>     m_bombs;
	QList<Enemy>    m_enemies;
	QList<DropItem> m_drops;

	//多人模式下由子类填充；单人模式保持为空（视图回落到单数字段）
	QList<GameSnapshot::PlayerView> m_playerViews;

	//爆炸痕倒计时：(格子, 剩余秒数)
	QList<QPair<GridPos, double>> m_explosionTimers;

	//结束本局（会发出 levelFinished 并把阶段切到 Victory/Defeat）
	void finishLevel(bool victory);

	//把内部状态打包成快照（基类统一做，子类不用管）
	void rebuildSnapshot();

	//记录"上一逻辑帧"的实体位置（渲染插值用）；每个逻辑步开头调用一次
	void capturePreviousPositions();

private:
	//推进爆炸痕计时（每个逻辑步都会调用；暂停/结束时也继续，让表现播完）
	void updateExplosionTimers(double frameTime);

	GameConfig  m_config;
	GameBoard   m_board;
	InputState  m_input;
	GameSnapshot m_snapshot;
	QList<GameEvent> m_pendingEvents;
	bool m_finishedEmitted = false;
	double m_currentFrameTime = 1.0 / 60.0;

	//本局随机源：掉落、敌人转向、敌人放弹都用它（固定种子时全部可复现）
	QRandomGenerator m_rng{ QRandomGenerator::securelySeeded() };
	bool m_playSeedFixed = false;
	quint32 m_rngSeed = 0;

	//资源注入点（见 setEffectResolver / setDropTable）
	std::function<QString(const QString&)> m_effectResolver;
	QList<QPair<QString, double>> m_dropTable;

	//上一逻辑帧的实体位置（渲染插值用）
	QList<GridPosF> m_prevEnemyPositions;
	GridPosF m_prevPlayerPos;

	//L3：模组接管点与实体 id 分配
	EnemyThinkHook   m_enemyThinkHook;
	WinConditionHook m_winConditionHook;
	bool    m_defaultWinDisabled = false;
	int     m_lastEntityId = 0;
	QString m_finishReason;

	std::function<void()>      m_onStateChanged;
	std::function<void(Phase)> m_onPhaseChanged;
	std::function<void(bool)>  m_onLevelFinished;
};

} // namespace bp
