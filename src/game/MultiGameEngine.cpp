#include "MultiGameEngine.h"
#include "ItemEffects.h"

#include <QtMath>

namespace bp
{

//没有指定对局种子时用的缺省值
//（沿用了旧多人实现写死的 42：即使某些调用方忘了传种子，两端也仍然一致）
static constexpr quint32 kDefaultMatchSeed = 42u;

MultiGameEngine::MultiGameEngine(const GameConfig& config, QObject* parent)
	: GameEngine(config, parent)
{
	m_generator = std::make_unique<RandomMapGenerator>();
}

//替换地图生成器（阶段 S6 接 JSON 关卡 / mod 时用）
void MultiGameEngine::setMapGenerator(std::unique_ptr<IMapGenerator> generator)
{
	if (generator) { m_generator = std::move(generator); }
}

// ---------- 参与者 ----------

void MultiGameEngine::clearPlayers()
{
	m_players.clear();
	m_playerViews.clear();
}

void MultiGameEngine::addPlayer(int id, const QString& name)
{
	if (findPlayer(id)) { return; }

	MultiPlayer player;
	player.id = id;
	player.name = name;
	player.maxLives = config().playerMaxLives;
	player.lives = player.maxLives;
	player.bombRange = config().playerBombRange;
	player.maxBombs = config().playerMaxBombs;

	m_players.append(player);
	rebuildPlayerViews();
}

int MultiGameEngine::aliveCount() const
{
	int count = 0;
	for (const MultiPlayer& player : m_players)
	{
		if (player.alive()) { ++count; }
	}
	return count;
}

QString MultiGameEngine::playerName(int id) const
{
	const MultiPlayer* player = findPlayer(id);
	return player ? player->name : QString();
}

MultiPlayer* MultiGameEngine::findPlayer(int id)
{
	for (MultiPlayer& player : m_players)
	{
		if (player.id == id) { return &player; }
	}
	return nullptr;
}

const MultiPlayer* MultiGameEngine::findPlayer(int id) const
{
	for (const MultiPlayer& player : m_players)
	{
		if (player.id == id) { return &player; }
	}
	return nullptr;
}

//出生点：默认四个角。第一位玩家的位置同时也是地图生成器的 playerStart，
//这样"玩家出生点不会长出砖块"这条规则对多人同样成立
void MultiGameEngine::setSpawnPoints(const QList<GridPos>& points)
{
	m_spawnPoints = points;
}

QList<GridPos> MultiGameEngine::effectiveSpawnPoints() const
{
	if (!m_spawnPoints.isEmpty()) { return m_spawnPoints; }

	QList<GridPos> points;
	points << GridPos(1, 1)
		   << GridPos(1, kCols - 2)
		   << GridPos(kRows - 2, 1)
		   << GridPos(kRows - 2, kCols - 2);
	return points;
}

// ---------- 生命周期 ----------

//随机种子：<0 表示"每局自动取时间相关种子"（只影响掉落这类随机表现）
//注意地图种子用的是同一个值，所以主机与客户端只要拿到同一个种子就必然同图
void MultiGameEngine::setRandomSeed(qint64 seed)
{
	if (seed < 0)
	{
		clearPlaySeed();
		m_matchSeed = 0;
		return;
	}

	const quint32 value = static_cast<quint32>(seed);
	setPlaySeed(value);
	m_matchSeed = value;
}

void MultiGameEngine::onReset()
{
	m_winnerId = -1;
	m_elapsedSeconds = 0.0;

	boardRef().fill(TileType::Empty);
	boardRef().buildBorderWalls();

	for (MultiPlayer& player : m_players)
	{
		player.lives = player.maxLives;
		player.ghost = false;
		player.ghostTimer = 0.0;
		player.speedFactor = 1.0;
		player.input = InputState();
	}

	rebuildPlayerViews();
}

void MultiGameEngine::onLevelInit()
{
	m_winnerId = -1;
	m_elapsedSeconds = 0.0;

	//开局先把每个人恢复成满状态：重开一局时界面不需要再搬一次数据
	for (MultiPlayer& player : m_players)
	{
		player.lives = player.maxLives;
		player.bombRange = config().playerBombRange;
		player.maxBombs = config().playerMaxBombs;
		player.ghost = false;
		player.ghostTimer = 0.0;
		player.speedFactor = 1.0;
		player.input = InputState();
	}

	//准备本局随机源（掉落用；地图用的是对局种子，见下）
	preparePlaySeed(1);

	const quint32 mapSeed = (m_matchSeed != 0) ? m_matchSeed : kDefaultMatchSeed;

	const QList<GridPos> points = effectiveSpawnPoints();

	if (m_generator)
	{
		LevelSpec spec;
		spec.level = 1;
		spec.brickCount = config().brickBaseCount + config().brickPerLevel;
		spec.enemyCount = 0;                       //多人对局没有电脑敌人
		spec.seed = mapSeed;
		if (!points.isEmpty()) { spec.playerStart = points.first(); }
		m_generator->generate(boardRef(), spec);
	}

	//按出生点摆人
	for (int i = 0; i < m_players.size() && !points.isEmpty(); ++i)
	{
		const GridPos cell = points.at(i % points.size());
		m_players[i].pos = GridPosF::centered(cell);
		//多人模式不把玩家写进格子标记（只有炸弹写），清掉生成器留下的出生点标记
		boardRef().clearEntity(cell);
	}

	rebuildPlayerViews();
}

bool MultiGameEngine::beginMatch()
{
	startLevel(1);
	return phase() == Phase::Running;
}

// ---------- 每一步 ----------

void MultiGameEngine::advance(const FrameContext& ctx)
{
	m_elapsedSeconds += ctx.frameTime;

	//客户端不跑规则：位置/炸弹都来自主机广播，这里只推进"表现类"计时
	if (!m_authoritative)
	{
		updateGhostTimers(ctx);
		rebuildPlayerViews();
		return;
	}

	updateGhostTimers(ctx);
	movePlayers(ctx);
	updateBombs(ctx);
	updateDrops(ctx);
	checkMatchEnd();

	rebuildPlayerViews();
}

//幽灵态倒计时
void MultiGameEngine::updateGhostTimers(const FrameContext& ctx)
{
	for (MultiPlayer& player : m_players)
	{
		if (player.ghostTimer <= 0.0) { continue; }

		player.ghostTimer -= ctx.frameTime;
		if (player.ghostTimer <= 0.0)
		{
			player.ghostTimer = 0.0;
			player.ghost = false;
		}
	}
}

//移动与放弹：每个玩家用自己那份输入推进（逐轴判定由基类的 moveEntityBy 负责）
void MultiGameEngine::movePlayers(const FrameContext& ctx)
{
	const double baseSpeed = config().playerSpeed;

	for (MultiPlayer& player : m_players)
	{
		if (!player.alive()) { continue; }

		const double step = baseSpeed * player.speedFactor * ctx.frameTime;
		const InputState& in = player.input;

		double dx = 0.0;
		double dy = 0.0;
		if (in.left) { dx -= step; }
		if (in.right) { dx += step; }
		if (in.up) { dy -= step; }
		if (in.down) { dy += step; }

		if (dx != 0.0 || dy != 0.0)
		{
			moveEntityBy(player.pos, dx, dy, player.ghost);
		}

		if (in.placeBomb)
		{
			tryPlaceBombFor(player.id);
		}
	}
}

void MultiGameEngine::submitRemoteInput(int playerId, const InputState& input)
{
	MultiPlayer* player = findPlayer(playerId);
	if (!player) { return; }
	player->input = input;
}

bool MultiGameEngine::tryPlaceBombFor(int playerId)
{
	if (!isRunning()) { return false; }

	MultiPlayer* player = findPlayer(playerId);
	if (!player || !player->alive()) { return false; }

	//每人一份上限：只数"自己的"炸弹（旧实现按 ownerId 数炸弹，这里只是搬进引擎）
	if (bombCountOf(playerId) >= player->maxBombs) { return false; }

	return addBomb(player->pos.cell, BombOwner::Player, playerId,
				   config().playerBombFuse, player->bombRange);
}

//炸弹：倒计时 -> 引爆 -> 结算
void MultiGameEngine::updateBombs(const FrameContext& ctx)
{
	for (int i = m_bombs.size() - 1; i >= 0; --i)
	{
		Bomb& bomb = m_bombs[i];
		bomb.fuse -= ctx.frameTime;

		if (bomb.fuse <= 0.0)
		{
			const Bomb exploding = bomb;
			m_bombs.removeAt(i);
			resolveBlast(exploding);
		}
	}
}

void MultiGameEngine::resolveBlast(const Bomb& bomb)
{
	//波及格由棋盘算（墙挡住、砖块后停止）
	const QList<GridPos> cells = detonateBomb(bomb);

	for (const GridPos& cell : cells)
	{
		//1) 伤害：默认不伤害自己（与旧多人实现一致，可用配置打开）
		for (MultiPlayer& player : m_players)
		{
			if (!player.alive()) { continue; }
			if (!config().multiSelfDamage && player.id == bomb.ownerId) { continue; }
			if (player.pos.cell != cell) { continue; }
			damagePlayerAt(player.id, 1, cell);
		}

		//2) 砖块（会被炸开，并可能掉道具）与爆炸痕
		destroyBrick(cell);
	}
}

void MultiGameEngine::damagePlayerAt(int playerId, int amount, const GridPos& cell)
{
	MultiPlayer* player = findPlayer(playerId);
	if (!player || amount <= 0) { return; }

	player->lives -= amount;
	if (player->lives < 0) { player->lives = 0; }

	//ownerId 填"被打的是谁"，界面据此做受击提示
	emitEvent(GameEventType::PlayerDamaged, cell, amount, player->name, playerId);
}

//掉落：谁站在那一格谁捡（一台机器上的多个玩家可能同时踩到，先到先得）
void MultiGameEngine::updateDrops(const FrameContext& ctx)
{
	for (int i = m_drops.size() - 1; i >= 0; --i)
	{
		const DropItem& drop = m_drops[i];
		if (drop.dying) { continue; }

		for (MultiPlayer& player : m_players)
		{
			if (!player.alive()) { continue; }
			if (player.pos.cell != drop.pos.cell) { continue; }

			emitEvent(GameEventType::DropCollected, drop.pos.cell, 1, drop.name, player.id);
			applyEffectToPlayer(player.id, drop.name);

			m_drops.removeAt(i);
			break;
		}
	}

	updateDropLifetimes(ctx.frameTime);
}

//胜负：只剩一人（或零人）时结束
//少于两人的对局不判胜负 —— 否则"一个人开局"会立刻判定自己赢
void MultiGameEngine::checkMatchEnd()
{
	if (!isRunning()) { return; }

	//模组接管了胜负（只在主机上才有：客户端不跑规则）
	if (winTakenOver())
	{
		QString reason;
		const int verdict = askWinCondition(&reason);
		if (verdict >= 0)
		{
			if (!reason.isEmpty()) { setFinishReason(reason); }
			//模组只说"谁赢了"没意义：多人里胜负要落到某个玩家 id 上，
			//所以"胜利"= 本机玩家赢，其余按"没能赢"处理（界面据此显示结论）
			declareWinner(verdict == 1 ? m_localPlayerId : -1);
			return;
		}
	}
	if (defaultWinDisabled()) { return; }

	if (m_players.size() < 2) { return; }

	const int alive = aliveCount();
	if (alive > 1) { return; }

	int winnerId = -1;
	if (alive == 1)
	{
		for (const MultiPlayer& player : m_players)
		{
			if (player.alive()) { winnerId = player.id; break; }
		}
	}

	declareWinner(winnerId);
}

void MultiGameEngine::declareWinner(int winnerId)
{
	if (!isRunning()) { return; }

	m_winnerId = winnerId;
	//本机是不是赢家由引擎判断：界面只需要弹一句"你赢了/XX 获胜"
	finishLevel(winnerId == m_localPlayerId);
}

//模组的写请求：按玩家 id 造成伤害（与爆炸结算走同一条路径）
bool MultiGameEngine::requestDamagePlayer(int playerIndex, int amount)
{
	if (amount <= 0 || !isRunning()) { return false; }
	if (!m_authoritative) { return false; }     //客户端不跑规则

	MultiPlayer* player = findPlayer(playerIndex);
	if (!player || !player->alive()) { return false; }

	damagePlayerAt(playerIndex, amount, player->pos.cell);
	return true;
}

// ---------- 客户端：套用主机广播的状态 ----------

void MultiGameEngine::applyRemotePlayers(const QList<int>& ids, const QList<GridPosF>& positions,
										 const QList<int>& lives)
{
	const int count = qMin(ids.size(), qMin(positions.size(), lives.size()));

	for (int i = 0; i < count; ++i)
	{
		MultiPlayer* player = findPlayer(ids.at(i));
		if (!player) { continue; }
		player->pos = positions.at(i);
		player->lives = lives.at(i);
	}

	rebuildPlayerViews();
	rebuildSnapshot();
}

void MultiGameEngine::applyRemoteBombs(const QList<Bomb>& bombs)
{
	//先清掉棋盘上的旧炸弹标记，再按新列表重打
	//（客户端不跑炸弹逻辑，但"警戒闪格"要用 board().blastCells()，
	// 所以棋盘上的炸弹标记必须和列表一致，否则闪的格子会是错的）
	for (const Bomb& bomb : m_bombs)
	{
		boardRef().clearEntity(bomb.pos.cell);
	}

	m_bombs = bombs;

	for (const Bomb& bomb : m_bombs)
	{
		boardRef().markEntity(bomb.pos.cell, EntityKind::Bomb);
	}

	rebuildSnapshot();
}

void MultiGameEngine::applyRemoteDrops(const QList<DropItem>& drops)
{
	m_drops = drops;
	rebuildSnapshot();
}

// ---------- 快照 ----------

void MultiGameEngine::rebuildPlayerViews()
{
	m_playerViews.clear();

	for (const MultiPlayer& player : m_players)
	{
		GameSnapshot::PlayerView view;
		view.id = player.id;
		view.name = player.name;
		view.pos = player.pos;
		view.lives = player.lives;
		view.maxLives = player.maxLives;
		view.bombRange = player.bombRange;
		view.maxBombs = player.maxBombs;
		view.alive = player.alive();
		view.local = (player.id == m_localPlayerId);
		view.ghost = player.ghost;
		m_playerViews.append(view);
	}
}

// ---------- 道具效果 ----------

//多人版效果表：和单人同一套效果ID，但"作用对象"必须显式指定
bool MultiGameEngine::applyEffect(const QString& effectId)
{
	return applyEffectToPlayer(m_localPlayerId, effectId);
}

bool MultiGameEngine::applyEffectToPlayer(int playerId, const QString& effectId)
{
	MultiPlayer* player = findPlayer(playerId);
	if (!player) { return false; }

	const QString id = resolveEffectId(effectId);
	if (id.isEmpty()) { return false; }

	const GridPos cell = player->pos.cell;

	// ---- 基础道具 ----
	if (id == EffectId::Heal)
	{
		player->lives = qMin(player->lives + 2, player->maxLives);
		emitEvent(GameEventType::ItemUsed, cell, 2, id, playerId);
		return true;
	}

	if (id == EffectId::SpeedUp)
	{
		player->speedFactor += 0.15;
		emitEvent(GameEventType::ItemUsed, cell, 0, id, playerId);
		return true;
	}

	if (id == EffectId::BombRangeUp)
	{
		++player->bombRange;
		emitEvent(GameEventType::ItemUsed, cell, 0, id, playerId);
		return true;
	}

	if (id == EffectId::BombCapacityUp)
	{
		++player->maxBombs;
		emitEvent(GameEventType::ItemUsed, cell, 0, id, playerId);
		return true;
	}

	// ---- 彩蛋道具：单人版是"对敌人"的，多人版改成"对其他玩家" ----
	if (id == EffectId::EyeBroken)
	{
		player->maxLives += 2;
		emitEvent(GameEventType::ItemUsed, cell, 0, id, playerId);
		return true;
	}

	if (id == EffectId::Eye)
	{
		player->maxLives += 2;
		player->lives = qMin(player->lives + 2, player->maxLives);

		//对其他玩家各造成 1 点伤害
		for (MultiPlayer& other : m_players)
		{
			if (other.id == playerId || !other.alive()) { continue; }
			damagePlayerAt(other.id, 1, other.pos.cell);
		}

		emitEvent(GameEventType::ItemUsed, cell, 1, id, playerId);
		return true;
	}

	if (id == EffectId::StarBroken)
	{
		player->lives = 1;
		player->ghost = true;
		player->ghostTimer = 8.0;
		emitEvent(GameEventType::ItemUsed, cell, 8, id, playerId);
		return true;
	}

	if (id == EffectId::Star)
	{
		player->ghost = true;
		player->ghostTimer = 15.0;
		emitEvent(GameEventType::ItemUsed, cell, 15, id, playerId);
		return true;
	}

	return false;
}

} // namespace bp
