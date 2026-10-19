#include "GameEngine.h"
#include "GameplayPatch.h"
#include "ItemEffects.h"

#include <QRandomGenerator>
#include <QDateTime>
#include <QtMath>

namespace bp
{

GameEngine::GameEngine(const GameConfig& config, QObject* parent)
	: QObject(parent), m_config(config)
{
}

// ---------------- 生命周期 ----------------

void GameEngine::resetGame()
{
	m_stepIndex = 0;
	m_input = InputState();
	m_bombs.clear();
	m_enemies.clear();
	m_drops.clear();
	m_explosionTimers.clear();
	m_pendingEvents.clear();
	m_finishedEmitted = false;
	m_playerGhost = false;

	m_level = 0;
	m_playerLives = m_config.playerMaxLives;
	m_playerMaxLives = m_config.playerMaxLives;
	m_playerBombRange = m_config.playerBombRange;
	m_playerMaxBombs = m_config.playerMaxBombs;

	setPhase(Phase::Ready);

	onReset();
	rebuildSnapshot();
}

void GameEngine::startLevel(int level)
{
	m_level = qMax(1, level);
	m_stepIndex = 0;
	m_finishedEmitted = false;

	m_bombs.clear();
	m_enemies.clear();
	m_drops.clear();
	m_explosionTimers.clear();
	m_pendingEvents.clear();
	m_playerGhost = false;

	m_playerLives = m_config.playerMaxLives;
	m_playerMaxLives = m_config.playerMaxLives;
	m_playerBombRange = m_config.playerBombRange;
	m_playerMaxBombs = m_config.playerMaxBombs;

	setPhase(Phase::Running);

	onLevelInit();
	rebuildSnapshot();
}

// ---------------- 帧协调 ----------------

bool GameEngine::tick(double frameTime)
{
	if (frameTime <= 0.0) { return isRunning(); }

	//记录本步时长：子类折算位移/探路时用它
	m_currentFrameTime = frameTime;

	//暂停时只推进"纯表现"的计时（爆炸痕消退、死亡动画），
	//规则完全冻结 —— 这样暂停画面不会僵在半帧状态
	if (m_phase == Phase::Paused)
	{
		updateExplosionTimers(frameTime);
		return true;
	}

	if (m_phase == Phase::Victory || m_phase == Phase::Defeat)
	{
		//结束后仍推进一小段，让"敌人死亡动画/爆炸痕"能播完
		updateExplosionTimers(frameTime);
		return false;
	}

	const FrameContext ctx{ frameTime, m_stepIndex };

	//先记录"上一逻辑帧"的实体位置，再推进规则：这样渲染插值有起点和终点
	capturePreviousPositions();
	advance(ctx);

	//爆炸痕消退：必须每一步都推进（之前只在暂停/结束时推进，
	//导致正常游戏里爆炸痕永远不消失 —— 就是"红格残留"那个 bug）
	updateExplosionTimers(frameTime);

	++m_stepIndex;
	rebuildSnapshot();

	if (m_onStateChanged) { m_onStateChanged(); }
	return true;
}

//推进爆炸痕计时（到点恢复为空地）
void GameEngine::updateExplosionTimers(double frameTime)
{
	for (int i = m_explosionTimers.size() - 1; i >= 0; --i)
	{
		m_explosionTimers[i].second -= frameTime;
		if (m_explosionTimers[i].second <= 0.0)
		{
			m_board.clearExplosion(m_explosionTimers[i].first);
			m_explosionTimers.removeAt(i);
		}
	}
}

// ---------------- 输入 ----------------

void GameEngine::setInput(const InputState& input)
{
	m_input = input;
}

// ---------------- 阶段 ----------------

void GameEngine::setPhase(Phase phase)
{
	if (m_phase == phase) { return; }
	m_phase = phase;
	if (m_onPhaseChanged) { m_onPhaseChanged(m_phase); }
}

void GameEngine::finishLevel(bool victory)
{
	if (m_finishedEmitted) { return; }
	m_finishedEmitted = true;

	setPhase(victory ? Phase::Victory : Phase::Defeat);
	if (m_onLevelFinished) { m_onLevelFinished(victory); }
}

// ---------------- 道具效果 ----------------

bool GameEngine::applyEffect(const QString& effectId)
{
	Q_UNUSED(effectId);
	//基类不实现任何效果；子类按需重写
	return false;
}

// ---------------- 资源载入的注入点 ----------------

void GameEngine::setEffectResolver(std::function<QString(const QString&)> resolver)
{
	m_effectResolver = std::move(resolver);
}

void GameEngine::setDropTable(const QList<QPair<QString, double>>& table)
{
	m_dropTable = table;
}

//把"效果ID 或 道具名"解析成最终的效果ID
//顺序：effect: 前缀 -> 注入的翻译器（可读 JSON）-> 内置静态映射 -> 原样返回
QString GameEngine::resolveEffectId(const QString& nameOrEffectId) const
{
	const QString raw = nameOrEffectId;
	if (raw.isEmpty()) { return QString(); }

	if (raw.startsWith(QStringLiteral("effect:"))) { return raw.mid(7); }

	if (m_effectResolver)
	{
		const QString mapped = m_effectResolver(raw);
		if (!mapped.isEmpty()) { return mapped; }
	}

	const QString builtin = effectIdForItemName(raw);
	return builtin.isEmpty() ? raw : builtin;
}

// ---------------- 共用工具 ----------------

void GameEngine::movePlayerBy(double dxCells, double dyCells)
{
	if (dxCells == 0.0 && dyCells == 0.0) { return; }

	const GridPos oldCell = m_playerPos.cell;

	//逐轴判定交给 moveEntityBy（多人模式下每个玩家走的是同一段代码）
	moveEntityBy(m_playerPos, dxCells, dyCells, m_playerGhost);

	const GridPos newCell = m_playerPos.cell;
	if (oldCell != newCell)
	{
		m_board.clearEntity(oldCell);
		m_board.markEntity(newCell, EntityKind::Player);
	}
}

//把任意一个"格子 + 格内偏移"位置逐轴移动
//为什么逐轴：斜向撞墙时应该"贴着墙滑过去"，而不是整段位移被否决（这是原有的手感）
//为什么不用棋盘实体标记：多人模式里玩家不写进格子标记（只有炸弹写），
//所以这段只负责几何，写标记的活留给各自的调用方
bool GameEngine::moveEntityBy(GridPosF& pos, double dxCells, double dyCells, bool ghost)
{
	if (dxCells == 0.0 && dyCells == 0.0) { return false; }

	const GridPos oldCell = pos.cell;

	//X 轴：先算目标连续列值，再取整到格子判断是否可走
	if (dxCells != 0.0)
	{
		const GridPosF probe = posFromRowCol(pos.row(), pos.col() + dxCells);
		if (ghost || m_board.walkable(probe.cell)) { pos = probe; }
	}

	//Y 轴：用 X 轴移动后的位置继续
	if (dyCells != 0.0)
	{
		const GridPosF probe = posFromRowCol(pos.row() + dyCells, pos.col());
		if (ghost || m_board.walkable(probe.cell)) { pos = probe; }
	}

	return oldCell != pos.cell;
}

bool GameEngine::tryPlaceBomb(const GridPos& cell, BombOwner owner)
{
	//玩家炸弹受"同时可放数量"限制；敌人不限制（由冷却控制）
	if (owner == BombOwner::Player)
	{
		int mine = 0;
		for (const Bomb& bomb : m_bombs)
		{
			if (bomb.owner == BombOwner::Player) { ++mine; }
		}
		if (mine >= m_playerMaxBombs) { return false; }
	}

	if (!canPlaceBombAt(cell)) { return false; }

	const double fuse = (owner == BombOwner::Player) ? m_config.playerBombFuse : m_config.enemyBombFuse;
	const int range = (owner == BombOwner::Player) ? m_playerBombRange : m_config.enemyBombRange;
	const int ownerId = (owner == BombOwner::Player) ? kPlayerOwnerId : 0;

	return addBomb(cell, owner, ownerId, fuse, range);
}

//能不能在这一格放炸弹：同一格不许有第二枚，且该格必须可通行
bool GameEngine::canPlaceBombAt(const GridPos& cell) const
{
	for (const Bomb& bomb : m_bombs)
	{
		if (bomb.pos.cell == cell) { return false; }
	}
	return m_board.walkable(cell);
}

//真正加上一枚炸弹
//把"判定"和"落地"拆成两个函数，是为了让多人模式能自己判"每人独立上限"，
//同时复用"同格不重复 + 写棋盘标记 + 抛事件"这段容易写错的代码
bool GameEngine::addBomb(const GridPos& cell, BombOwner owner, int ownerId, double fuse, int range)
{
	if (!canPlaceBombAt(cell)) { return false; }

	Bomb bomb;
	bomb.pos = GridPosF::centered(cell);
	bomb.fuse = fuse;
	bomb.fuseTotal = fuse;
	bomb.range = range;
	bomb.owner = owner;
	bomb.ownerId = ownerId;

	m_bombs.append(bomb);
	m_board.markEntity(cell, EntityKind::Bomb);

	emitEvent(GameEventType::BombPlaced, cell, 0, QString(), ownerId);
	return true;
}

//某编号归属者当前在场上的炸弹数（多人用来实现"每人一份放弹上限"）
int GameEngine::bombCountOf(int ownerId) const
{
	int count = 0;
	for (const Bomb& bomb : m_bombs)
	{
		if (bomb.ownerId == ownerId) { ++count; }
	}
	return count;
}

QList<GridPos> GameEngine::detonateBomb(const Bomb& bomb)
{
	const GridPos center = bomb.pos.cell;

	//炸弹所在格先清成空地，否则后续判定会被炸弹标记挡住
	m_board.clearEntity(center);

	QList<GridPos> cells = m_board.blastCells(center, bomb.range);

	emitEvent(GameEventType::BombExploded, center, bomb.range, QString(), bomb.ownerId);
	return cells;
}

void GameEngine::markExplosion(const GridPos& cell)
{
	if (!GameBoard::inBounds(cell)) { return; }
	if (m_board.tile(cell) == TileType::Wall) { return; }

	//只对"空地"留爆炸痕；砖块与实体标记不动
	if (m_board.tile(cell) == TileType::Empty)
	{
		m_board.setTile(cell, TileType::Explosion);
	}

	m_explosionTimers.append(qMakePair(cell, m_config.explosionPersistSeconds));
}

// ---------------- 砖块与掉落（单人/多人共用） ----------------

//砖块被破坏与爆炸痕
void GameEngine::destroyBrick(const GridPos& cell)
{
	const TileType t = m_board.tile(cell);

	if (t == TileType::Brick)
	{
		m_board.setTile(cell, TileType::Empty);

		//掉落在"掉落判定"里做，这里只负责把砖块清掉并留爆炸痕
		if (m_rng.generateDouble() < m_config.dropChance)
		{
			spawnDrop(cell);
		}
		else
		{
			markExplosion(cell);
		}
		return;
	}

	//空地：留爆炸痕（带存续时间，到点由 updateExplosionTimers 恢复）
	if (t == TileType::Empty)
	{
		markExplosion(cell);
	}
}

void GameEngine::spawnDrop(const GridPos& cell)
{
	DropItem drop;
	drop.pos = GridPosF::centered(cell);
	drop.name = rollDropName();
	drop.lifetime = 0.0;
	m_drops.append(drop);

	emitEvent(GameEventType::DropSpawned, cell, 1, drop.name);
}

//按权重抽一个掉落物名
//权重来源优先级：应用层注入的权重表（来自 itemInfo.json 的 dropWeight）> GameConfig 内置表 > 内置名单
QString GameEngine::rollDropName()
{
	const QList<QPair<QString, double>>& weights =
		m_dropTable.isEmpty() ? m_config.rareDropWeights : m_dropTable;

	if (weights.isEmpty())
	{
		const QStringList& fallback = m_config.droppableItems;
		if (fallback.isEmpty()) { return QString(); }
		return fallback.at(static_cast<int>(m_rng.bounded(fallback.size())));
	}

	double sum = 0.0;
	for (const QPair<QString, double>& entry : weights)
	{
		if (entry.second > 0.0) { sum += entry.second; }
	}
	if (sum <= 0.0) { return QString(); }

	double roll = m_rng.generateDouble() * sum;
	for (const QPair<QString, double>& entry : weights)
	{
		if (entry.second <= 0.0) { continue; }
		roll -= entry.second;
		if (roll <= 0.0) { return entry.first; }
	}

	return weights.last().first;
}

//掉落物超时消失（拾取逻辑在各模式里，因为"谁捡到"各模式不同）
void GameEngine::updateDropLifetimes(double frameTime)
{
	if (m_config.dropLifetimeSeconds <= 0.0) { return; }

	for (int i = m_drops.size() - 1; i >= 0; --i)
	{
		if (m_drops[i].dying) { continue; }

		m_drops[i].lifetime += frameTime;
		if (m_drops[i].lifetime >= m_config.dropLifetimeSeconds)
		{
			m_drops.removeAt(i);
		}
	}
}

bool GameEngine::chancePerSecond(double timesPerSecond, double frameTime)
{
	if (timesPerSecond <= 0.0) { return false; }
	const double p = timesPerSecond * frameTime;
	if (p >= 1.0) { return true; }
	//用引擎自己的随机源：固定种子时整局可复现
	return m_rng.generateDouble() < p;
}

//开局的随机源准备：固定种子优先；否则用"时间 ^ 关卡"生成一个并播种
void GameEngine::preparePlaySeed(int level)
{
	if (m_playSeedFixed)
	{
		m_rng.seed(m_rngSeed);
		return;
	}

	m_rngSeed = static_cast<quint32>(QDateTime::currentMSecsSinceEpoch()
		^ (static_cast<qint64>(level) * 2654435761LL));
	m_rng.seed(m_rngSeed);
}
bool GameEngine::poissonPerSecond(double timesPerSecond, double frameTime)
{
	//把"每秒期望次数"折算成"本步概率"，与帧率无关
	return chancePerSecond(timesPerSecond, frameTime);
}

void GameEngine::emitEvent(GameEventType type, const GridPos& cell, int amount,
						   const QString& text, int ownerId)
{
	GameEvent event;
	event.type = type;
	event.cell = cell;
	event.amount = amount;
	event.text = text;
	event.ownerId = ownerId;
	m_pendingEvents.append(event);
}

//记录上一逻辑帧的实体位置（渲染插值用）
void GameEngine::capturePreviousPositions()
{
	m_prevPlayerPos = m_playerPos;

	m_prevEnemyPositions.clear();
	for (const Enemy& enemy : m_enemies)
	{
		m_prevEnemyPositions.append(enemy.pos);
	}
}

// ---------------- 快照 ----------------

void GameEngine::rebuildSnapshot()
{
	m_snapshot.level = m_level;
	m_snapshot.phase = m_phase;
	m_snapshot.stepIndex = m_stepIndex;
	m_snapshot.playerPos = m_playerPos;
	m_snapshot.playerLives = m_playerLives;
	m_snapshot.playerMaxLives = m_playerMaxLives;
	m_snapshot.playerBombRange = m_playerBombRange;
	m_snapshot.playerMaxBombs = m_playerMaxBombs;
	m_snapshot.playerGhost = m_playerGhost;
	m_snapshot.bombs = m_bombs;
	m_snapshot.enemies = m_enemies;
	m_snapshot.drops = m_drops;

	//地形也进快照：视图要画墙/砖块/爆炸痕，但不该去碰棋盘对象
	m_board.copyTiles(m_snapshot.tiles);

	//渲染插值要用的"上一逻辑帧敌人位置"（视图只读快照，所以数据也放进快照）
	m_snapshot.previousEnemyPositions = m_prevEnemyPositions;

	//多人玩家列表（单人模式为空）；顺手记下"本机是第几个"，省得视图自己去搜
	m_snapshot.players = m_playerViews;
	m_snapshot.localPlayerIndex = -1;
	for (int i = 0; i < m_playerViews.size(); ++i)
	{
		if (m_playerViews[i].local) { m_snapshot.localPlayerIndex = i; break; }
	}

	//炸弹视图：附带归一化引信进度，渲染层据此画倒计时与配色
	m_snapshot.bombViews.clear();
	for (const Bomb& bomb : m_bombs)
	{
		GameSnapshot::BombView view;
		view.pos = bomb.pos;
		view.owner = bomb.owner;
		view.range = bomb.range;
		view.ownerId = bomb.ownerId;
		view.fuseTotal = bomb.fuseTotal;
		view.remainingSeconds = qMax(0.0, bomb.fuse);
		view.progress = (bomb.fuseTotal > 0.0) ? qBound(0.0, bomb.fuse / bomb.fuseTotal, 1.0) : 0.0;

		//只在"快炸了"的时候算波及格：这就是视图闪格用的预警数据
		//算一次就够，视图不必（也不该）自己伸手进棋盘查
		if (view.remainingSeconds > 0.0 && view.remainingSeconds <= m_config.warningSeconds)
		{
			view.warningCells = m_board.blastCells(bomb.pos.cell, bomb.range);
		}

		m_snapshot.bombViews.append(view);
	}

	//事件交付后清空
	m_snapshot.events = m_pendingEvents;
	m_pendingEvents.clear();
}

// ============================================================
// L3：模组接管点与写请求
//
// 这一段的共同原则：**引擎保留裁决权**。模组说"我要在这里放个敌人"，
// 引擎先看"这一格能不能站、数量有没有超上限、游戏是不是已经结束"，
// 任何一条不过就返回 false，世界一点都没变。模组无法绕过。
// ============================================================

Enemy* GameEngine::findEnemyById(int entityId)
{
	for (Enemy& enemy : m_enemies)
	{
		if (enemy.id == entityId) { return &enemy; }
	}
	return nullptr;
}

const Enemy* GameEngine::findEnemyById(int entityId) const
{
	for (const Enemy& enemy : m_enemies)
	{
		if (enemy.id == entityId) { return &enemy; }
	}
	return nullptr;
}

int GameEngine::aliveEnemyCount() const
{
	int alive = 0;
	for (const Enemy& enemy : m_enemies)
	{
		if (!enemy.dying) { ++alive; }
	}
	return alive;
}

//问一次模组的敌人 AI（没接管时返回 0，让默认 AI 照常跑）
int GameEngine::askEnemyThink(const Enemy& enemy, const FrameContext& ctx, int& direction, bool& placeBomb)
{
	if (!m_enemyThinkHook) { return 0; }
	return m_enemyThinkHook(enemy, ctx, direction, placeBomb);
}

//某个方向走一步：可走才走（与默认 AI 用同一套碰撞判定，模组拿不到"穿墙"的后门）
bool GameEngine::stepEnemyInDirection(Enemy& enemy, int direction, const FrameContext& ctx)
{
	double dx = 0.0;
	double dy = 0.0;
	const double step = m_config.enemySpeed * ctx.frameTime;

	switch (direction)
	{
	case 0: dy = -step; break;
	case 1: dy = step;  break;
	case 2: dx = -step; break;
	case 3: dx = step;  break;
	default: return false;          //-1 或其它值 = 原地不动
	}

	const GridPosF probe = posFromRowCol(enemy.pos.row() + dy, enemy.pos.col() + dx);
	if (!m_board.walkable(probe.cell)) { return false; }

	const GridPos oldCell = enemy.pos.cell;
	enemy.pos = probe;

	if (oldCell != probe.cell)
	{
		m_board.clearEntity(oldCell);
		m_board.markEntity(probe.cell, EntityKind::Enemy);
	}
	return true;
}

bool GameEngine::tryPlaceEnemyBomb(Enemy& enemy)
{
	if (!isRunning()) { return false; }
	if (enemy.bombCooldown > 0.0) { return false; }

	if (!tryPlaceBomb(enemy.pos.cell, BombOwner::Enemy)) { return false; }

	enemy.bombCooldown = m_config.enemyBombCooldown;
	return true;
}

int GameEngine::askWinCondition(QString* reason)
{
	if (!m_winConditionHook) { return -1; }
	return m_winConditionHook(reason);
}

//生成一个敌人：格子必须可通行、且没有别的东西占着
bool GameEngine::requestSpawnEnemy(const GridPos& cell, int hp)
{
	if (!isRunning()) { return false; }
	if (!GameBoard::inBounds(cell)) { return false; }
	if (!m_board.walkable(cell)) { return false; }

	//实体上限：防止一个生病态的模组把帧率拖垮（契约 §7.3 的配额）
	if (m_enemies.size() + m_drops.size() + m_bombs.size() >= 256) { return false; }

	Enemy enemy;
	enemy.pos = GridPosF::centered(cell);
	enemy.hp = qMax(1, hp);
	enemy.maxHp = enemy.hp;
	enemy.id = nextEntityId();

	m_enemies.append(enemy);
	m_board.markEntity(cell, EntityKind::Enemy);
	return true;
}

bool GameEngine::requestRemoveEntity(int entityId)
{
	if (!isRunning()) { return false; }

	for (int i = 0; i < m_enemies.size(); ++i)
	{
		if (m_enemies[i].id != entityId) { continue; }

		const GridPos cell = m_enemies[i].pos.cell;
		m_enemies.removeAt(i);

		//清格子标记要小心：可能还有另一个敌人/炸弹在同一格
		m_board.clearEntity(cell);
		for (const Enemy& other : m_enemies)
		{
			if (other.pos.cell == cell) { m_board.markEntity(cell, EntityKind::Enemy); break; }
		}
		for (const Bomb& bomb : m_bombs)
		{
			if (bomb.pos.cell == cell) { m_board.markEntity(cell, EntityKind::Bomb); break; }
		}
		return true;
	}
	return false;
}

bool GameEngine::requestDamageEntity(int entityId, int amount)
{
	if (!isRunning() || amount <= 0) { return false; }

	Enemy* enemy = findEnemyById(entityId);
	if (!enemy || enemy->dying) { return false; }

	enemy->hp -= amount;
	emitEvent(GameEventType::EnemyDamaged, enemy->pos.cell, amount);

	if (enemy->hp <= 0)
	{
		//与爆炸结算同一套语义：延迟销毁 + 死亡事件 + 按掉落表掉一件
		enemy->hp = 0;
		enemy->dying = true;
		enemy->deathTimer = 0.2;
		emitEvent(GameEventType::EnemyDied, enemy->pos.cell, 1);
		spawnDrop(enemy->pos.cell);
	}
	return true;
}

//玩家伤害：单人只有 0 号；多人按玩家 id 找（子类覆盖）
bool GameEngine::requestDamagePlayer(int playerIndex, int amount)
{
	Q_UNUSED(playerIndex);
	Q_UNUSED(amount);
	return false;
}

bool GameEngine::requestSpawnDrop(const GridPos& cell, const QString& itemName)
{
	if (!isRunning()) { return false; }
	if (!GameBoard::inBounds(cell)) { return false; }
	if (itemName.isEmpty()) { return false; }

	DropItem drop;
	drop.pos = GridPosF::centered(cell);
	drop.name = itemName;
	drop.lifetime = 0.0;

	m_drops.append(drop);
	emitEvent(GameEventType::DropSpawned, cell, 1, itemName);
	return true;
}

bool GameEngine::requestPlaceBomb(const GridPos& cell, BombOwner owner)
{
	if (!isRunning()) { return false; }
	return tryPlaceBomb(cell, owner);
}

//改数值：走与 mod 的 config/gameplay.json 同一张白名单（GameplayPatch）。
//为什么共用白名单：白名单就是"引擎允许被改什么"的契约，多一个入口就多一条要维护的规则
bool GameEngine::requestSetConfigNumber(const QString& key, double value)
{
	return applyGameplayPatch(m_config, key, value);
}

} // namespace bp
