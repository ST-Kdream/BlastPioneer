#include "SingleGameEngine.h"
#include "ItemEffects.h"

#include <QDateTime>
#include <QtMath>

namespace bp
{

SingleGameEngine::SingleGameEngine(const GameConfig& config, QObject* parent)
	: GameEngine(config, parent)
{
	//默认使用随机地图生成器
	m_generator = std::make_unique<RandomMapGenerator>();
}

//替换地图生成器；传 nullptr = 恢复默认（界面对"没有 JSON 定义的关卡"就用这个）
void SingleGameEngine::setMapGenerator(std::unique_ptr<IMapGenerator> generator)
{
	m_generator = generator ? std::move(generator) : std::make_unique<RandomMapGenerator>();
}

//固定本局随机种子：seed < 0 表示恢复"每局自动取时间相关种子"
void SingleGameEngine::setRandomSeed(qint64 seed)
{
	if (seed < 0)
	{
		clearPlaySeed();
		return;
	}
	setPlaySeed(static_cast<quint32>(seed));
}

//把"格/秒"折算成本步位移（格）
double SingleGameEngine::cellsPerStep(double cellsPerSecond, double frameTime) const
{
	return cellsPerSecond * frameTime;
}

// ---------- 生命周期 ----------

void SingleGameEngine::onReset()
{
	m_elapsedSeconds = 0.0;
	m_speedFactor = 1.0;
	m_enemySpeedFactor = 1.0;
	m_ghostTimer = 0.0;
	boardRef().fill(TileType::Empty);
	boardRef().buildBorderWalls();
}

void SingleGameEngine::onLevelInit()
{
	m_elapsedSeconds = 0.0;
	m_speedFactor = 1.0;
	m_enemySpeedFactor = 1.0;
	m_ghostTimer = 0.0;

	//准备本局随机源：固定种子时全部可复现（掉落/敌人转向/敌人放弹都用它）
	preparePlaySeed(level());

	//出生点：默认取配置里的 (1,1)，生成器可以覆盖它
	//（手写关卡 JSON 里的 'P' 只有生成器知道在哪一格）
	GridPos start = m_playerStart;

	if (m_generator)
	{
		LevelSpec spec;
		spec.level = level();
		spec.brickCount = config().bricksForLevel(level());
		spec.enemyCount = config().enemiesForLevel(level());
		spec.enemyHp = config().enemyHpForLevel(level());
		spec.seed = static_cast<quint32>(level()) * 1000u + 7u;
		spec.playerStart = m_playerStart;

		const GeneratedMap generated = m_generator->generate(boardRef(), spec);

		if (generated.hasPlayerStart) { start = generated.playerStart; }

		m_enemies.clear();
		for (const EnemySpawn& spawn : generated.enemies)
		{
			Enemy enemy;
			enemy.pos = GridPosF::centered(spawn.pos);
			enemy.hp = spawn.hp;
			enemy.maxHp = qMax(1, spawn.hp);
			enemy.dirX = 0.0;
			enemy.dirY = 0.0;
			enemy.bombCooldown = 0.0;
			enemy.id = nextEntityId();   //稳定的实体 id：模组要靠它指名道姓地操作
			m_enemies.append(enemy);
		}
	}

	//玩家回到出生点
	playerPosRef() = GridPosF::centered(start);

	//兜一道：生成器（尤其是模组换进来的生成器）万一没保护出生格，
	//先把它清成空地；仍然不可走就 BFS 挤到最近空地，绝不让玩家开局站在墙里
	if (boardRef().tile(start) == TileType::Brick)
	{
		boardRef().setTile(start, TileType::Empty);
	}
	if (!boardRef().walkable(start))
	{
		unstickPlayer(8);
	}

	//兜一道：确保玩家出生格是玩家标记
	boardRef().markEntity(playerPos().cell, EntityKind::Player);
}

bool SingleGameEngine::beginLevel(int level)
{
	startLevel(level);
	return phase() == Phase::Running;
}

// ---------- 每一步 ----------

void SingleGameEngine::advance(const FrameContext& ctx)
{
	m_elapsedSeconds += ctx.frameTime;

	//反卡死：站在墙/砖块里就先挤出来（幽灵态结束时最容易发生；可走时是一次 O(1) 判断）
	unstickPlayer();

	//电子幽灵态倒计时（加速类道具改的是倍率，倒计时只是"限时状态"）
	if (m_ghostTimer > 0.0)
	{
		m_ghostTimer -= ctx.frameTime;
		if (m_ghostTimer <= 0.0)
		{
			m_ghostTimer = 0.0;
			m_playerGhost = false;
			//刚好停在墙里：立刻挤到最近的空地，别让玩家"卡死到只能重开"
			unstickPlayer();
		}
	}

	//顺序与旧实现一致：移动 -> 敌人 -> 炸弹 -> 掉落 -> 爆炸痕 -> 清理死物
	handlePlayerInput(ctx);
	updateEnemies(ctx);
	updateBombs(ctx);
	updateDrops(ctx);
	updateDyingEntities(ctx);

	checkLevelEnd();
}

//玩家输入：逐轴移动（保持原来的自由移动手感）
void SingleGameEngine::handlePlayerInput(const FrameContext& ctx)
{
	if (!isRunning()) { return; }

	const InputState& in = input();
	const double step = cellsPerStep(config().playerSpeed * m_speedFactor, ctx.frameTime);

	double dx = 0.0;
	double dy = 0.0;
	if (in.left) { dx -= step; }
	if (in.right) { dx += step; }
	if (in.up) { dy -= step; }
	if (in.down) { dy += step; }

	if (dx != 0.0 || dy != 0.0)
	{
		movePlayerBy(dx, dy);
	}

	//放炸弹
	if (in.placeBomb)
	{
		tryPlaceBomb(playerPos().cell, BombOwner::Player);
	}
}

//敌人：撞墙立刻换向、带惯性游走
void SingleGameEngine::applyEnemyDirection(Enemy& enemy, int dir)
{
	enemy.dirX = 0.0;
	enemy.dirY = 0.0;

	switch (dir)
	{
	case 0: enemy.dirY = -1.0; break;   //上
	case 1: enemy.dirY = 1.0;  break;   //下
	case 2: enemy.dirX = -1.0; break;   //左
	case 3: enemy.dirX = 1.0;  break;   //右
	default: break;
	}
}

//试探某方向一步是否可走；可走则输出目标格与目标连续坐标
bool SingleGameEngine::tryProbeDirection(const Enemy& enemy, int dir, GridPos& outCell, double& outX, double& outY)
{
	//用当前步长试探（不要硬编码 1/60，否则改变逻辑步长时探路会失准）
	const double step = cellsPerStep(config().enemySpeed * m_enemySpeedFactor, currentFrameTime());
	double dx = 0.0;
	double dy = 0.0;

	switch (dir)
	{
	case 0: dy = -step; break;
	case 1: dy = step;  break;
	case 2: dx = -step; break;
	case 3: dx = step;  break;
	default: return false;
	}

	const double tx = enemy.pos.col() + dx;
	const double ty = enemy.pos.row() + dy;
	const GridPosF probe = posFromRowCol(ty, tx);

	if (!board().walkable(probe.cell)) { return false; }

	outCell = probe.cell;
	outX = tx;
	outY = ty;
	return true;
}

//选择方向：优先不回头，撞墙时立刻换向
void SingleGameEngine::pickEnemyDirection(Enemy& enemy, double frameTime)
{
	Q_UNUSED(frameTime);
	//当前朝向的反向（用于惩罚掉头）
	int reverseDir = -1;
	if (enemy.dirY < 0.0) { reverseDir = 1; }
	else if (enemy.dirY > 0.0) { reverseDir = 0; }
	else if (enemy.dirX < 0.0) { reverseDir = 3; }
	else if (enemy.dirX > 0.0) { reverseDir = 2; }

	//四方向的权重：反方向乘惩罚系数，其余等权
	double sum = 0.0;
	double w[4] = { 1.0, 1.0, 1.0, 1.0 };
	for (int i = 0; i < 4; ++i)
	{
		if (i == reverseDir) { w[i] = config().enemyReversePenalty; }
		sum += w[i];
	}

	//按权重掷一次，命中不可走就依次尝试其余方向（保证不会"掷到墙就站着不动"）
	double roll = rng().generateDouble() * sum;
	int chosen = -1;
	double acc = 0.0;
	for (int i = 0; i < 4; ++i)
	{
		acc += w[i];
		if (roll <= acc) { chosen = i; break; }
	}
	if (chosen < 0) { chosen = 3; }

	GridPos cell;
	double tx = 0.0;
	double ty = 0.0;
	if (tryProbeDirection(enemy, chosen, cell, tx, ty))
	{
		applyEnemyDirection(enemy, chosen);
		return;
	}

	//顺次尝试其它方向
	for (int k = 1; k < 4; ++k)
	{
		const int dir = (chosen + k) % 4;
		if (tryProbeDirection(enemy, dir, cell, tx, ty))
		{
			applyEnemyDirection(enemy, dir);
			return;
		}
	}

	//四面都被堵住：原地不动
	enemy.dirX = 0.0;
	enemy.dirY = 0.0;
}

void SingleGameEngine::updateEnemies(const FrameContext& ctx)
{
	const double step = cellsPerStep(config().enemySpeed * m_enemySpeedFactor, ctx.frameTime);

	//注意：这里用下标遍历而不是范围 for —— 循环中会 append 新炸弹（放弹），
	//范围 for 在容器扩容后迭代器会失效
	for (int i = 0; i < m_enemies.size(); ++i)
	{
		Enemy& enemy = m_enemies[i];
		if (enemy.dying) { continue; }

		//0) 模组接管了敌人 AI：先问它"这一步怎么走"。
		//   走位与碰撞仍由引擎落实（模组的 AI 不可能把敌人送进墙里），
		//   但"往哪走 / 要不要放弹"完全由模组决定 —— 这就是"接管"的边界
		if (enemiesTakenOver())
		{
			int direction = -1;
			bool placeBomb = false;
			const int acted = askEnemyThink(enemy, ctx, direction, placeBomb);
			if (acted != 0)
			{
				stepEnemyInDirection(enemy, direction, ctx);
				if (placeBomb) { tryPlaceEnemyBomb(enemy); }
				continue;   //这一步按模组说的办完，不再跑默认 AI
			}
		}

		//主动换向：每秒期望若干次，与帧率无关
		if (chancePerSecond(config().enemyTurnChancePerSecond, ctx.frameTime))
		{
			pickEnemyDirection(enemy, ctx.frameTime);
		}

		//没有方向时先选一个
		if (enemy.dirX == 0.0 && enemy.dirY == 0.0)
		{
			pickEnemyDirection(enemy, ctx.frameTime);
		}

		//沿当前方向走一步
		const double tx = enemy.pos.col() + enemy.dirX * step;
		const double ty = enemy.pos.row() + enemy.dirY * step;
		const GridPosF probe = posFromRowCol(ty, tx);

		if (board().walkable(probe.cell))
		{
			const GridPos oldCell = enemy.pos.cell;
			enemy.pos = probe;

			if (oldCell != probe.cell)
			{
				boardRef().clearEntity(oldCell);
				boardRef().markEntity(probe.cell, EntityKind::Enemy);
			}
		}
		else
		{
			//撞墙：立刻换一个能走的方向（旧实现只清零速度，会被推到墙角）
			pickEnemyDirection(enemy, ctx.frameTime);
		}

		//放弹：冷却结束后按"每秒期望次数"判定
		if (enemy.bombCooldown > 0.0)
		{
			enemy.bombCooldown -= ctx.frameTime;
		}
		else if (chancePerSecond(config().enemyBombsPerSecond, ctx.frameTime))
		{
			if (tryPlaceBomb(enemy.pos.cell, BombOwner::Enemy))
			{
				enemy.bombCooldown = config().enemyBombCooldown;
			}
		}
	}
}

//炸弹：倒计时 -> 引爆 -> 结算
void SingleGameEngine::updateBombs(const FrameContext& ctx)
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

//一次爆炸的完整结算
void SingleGameEngine::resolveBlast(const Bomb& bomb)
{
	//先让棋盘算出波及格（其中已按"墙挡住、砖块后停止"处理）
	const QList<GridPos> cells = detonateBomb(bomb);

	for (const GridPos& cell : cells)
	{
		//1) 伤害：玩家炸弹伤敌人，敌人炸弹伤玩家
		if (bomb.owner == BombOwner::Player)
		{
			applyBlastToEnemies(cell);
		}
		else
		{
			applyBlastToPlayer(cell, bomb.owner);
		}

		//2) 砖块与爆炸痕
		destroyBrick(cell);
	}
}

//对某一格的敌人造成伤害
void SingleGameEngine::applyBlastToEnemies(const GridPos& cell)
{
	for (Enemy& enemy : m_enemies)
	{
		if (enemy.dying) { continue; }
		if (enemy.pos.cell != cell) { continue; }

		--enemy.hp;
		emitEvent(GameEventType::EnemyDamaged, cell, 1);

		if (enemy.hp <= 0)
		{
			//延迟销毁：标记后由 updateDyingEntities 计时移除，表现层可据此播死亡动画
			enemy.dying = true;
			enemy.deathTimer = 0.2;
			emitEvent(GameEventType::EnemyDied, cell, 1);
			spawnDrop(cell);
		}
	}
}

//对某一格的玩家造成伤害
void SingleGameEngine::applyBlastToPlayer(const GridPos& cell, BombOwner source)
{
	if (source == BombOwner::Player) { return; }   //自己的炸弹不伤自己（与旧实现一致）
	if (playerGhost()) { return; }                 //电子幽灵态免疫

	if (playerPos().cell != cell) { return; }

	damagePlayer(1);
}

//砖块被破坏与爆炸痕：实现已上移到 GameEngine::destroyBrick（单人/多人共用）

//伤害玩家
void SingleGameEngine::damagePlayer(int amount)
{
	if (amount <= 0) { return; }

	m_playerLives -= amount;
	if (m_playerLives < 0) { m_playerLives = 0; }

	emitEvent(GameEventType::PlayerDamaged, playerPos().cell, amount);

	if (m_playerLives <= 0)
	{
		finishLevel(false);
	}
}

//模组的写请求：给 0 号玩家造成伤害（走与爆炸结算同一条路径，事件与结算都一致）
bool SingleGameEngine::requestDamagePlayer(int playerIndex, int amount)
{
	if (playerIndex != 0 || amount <= 0) { return false; }
	if (!isRunning()) { return false; }

	damagePlayer(amount);
	return true;
}

//掉落物生成与权重抽取已上移到基类（GameEngine::spawnDrop / rollDropName）

//掉落：拾取（本模式只有"本机玩家"一个拾取者）
void SingleGameEngine::updateDrops(const FrameContext& ctx)
{
	const GridPos playerCell = playerPos().cell;

	for (int i = m_drops.size() - 1; i >= 0; --i)
	{
		DropItem& drop = m_drops[i];

		if (drop.dying) { continue; }

		if (drop.pos.cell == playerCell)
		{
			emitEvent(GameEventType::DropCollected, drop.pos.cell, 1, drop.name);

			//交给效果表处理具体效果（默认只做加法类效果）
			applyEffect(drop.name);

			m_drops.removeAt(i);
		}
	}

	//限时消失（默认 dropLifetimeSeconds = 0，也就是永久存在，与原行为一致）
	updateDropLifetimes(ctx.frameTime);
}

//清理"播完死亡动画"的实体
void SingleGameEngine::updateDyingEntities(const FrameContext& ctx)
{
	for (int i = m_enemies.size() - 1; i >= 0; --i)
	{
		if (!m_enemies[i].dying) { continue; }

		m_enemies[i].deathTimer -= ctx.frameTime;
		if (m_enemies[i].deathTimer <= 0.0)
		{
			m_enemies.removeAt(i);
		}
	}

	for (int i = m_drops.size() - 1; i >= 0; --i)
	{
		if (m_drops[i].dying)
		{
			m_drops.removeAt(i);
		}
	}
}

//胜负判定
//关键：必须确认"本关确实有敌人"才用"敌人清空"作为胜利条件。
//否则当关卡配成 0 敌人（例如测试里为了隔离玩家移动而关掉敌人）时，
//会在第一步就判定通关，之后所有规则都被冷冻（玩家动不了、炸弹不炸）
bool SingleGameEngine::checkLevelEnd()
{
	if (!isRunning()) { return false; }

	//限时玩法（默认关闭）：超时即失败
	if (config().levelTimeoutSeconds > 0.0 && m_elapsedSeconds >= config().levelTimeoutSeconds)
	{
		finishLevel(false);
		return true;
	}

	//0) 模组接管了胜负：先问它（返回 -1 表示"模组没意见，按默认来"）
	if (winTakenOver())
	{
		QString reason;
		const int verdict = askWinCondition(&reason);
		if (verdict >= 0)
		{
			if (!reason.isEmpty()) { setFinishReason(reason); }
			finishLevel(verdict == 1);
			return true;
		}
	}

	//模组声明"我来判胜负"时，引擎的默认判定完全关掉
	//（否则一个 0 敌人的模式会在第一步就自动通关）
	if (defaultWinDisabled()) { return false; }

	//本关是否应该存在敌人
	const bool levelHasEnemies = (config().enemiesForLevel(level()) > 0);
	if (!levelHasEnemies) { return false; }

	//所有敌人都进入"死亡动画"阶段即视为清空
	int alive = 0;
	for (const Enemy& enemy : m_enemies)
	{
		if (!enemy.dying) { ++alive; }
	}

	if (alive == 0)
	{
		finishLevel(true);
		return true;
	}

	return false;
}

// ---------- 道具效果 ----------
//接受"效果ID"或"道具名"，方便界面直接透传掉落物名字
bool SingleGameEngine::applyEffect(const QString& effectId)
{
	//统一解析：effect: 前缀 / 应用层注入的翻译器（itemInfo.json 的 effect 字段）/ 内置映射
	const QString id = resolveEffectId(effectId);
	if (id.isEmpty()) { return false; }

	// ---- 基础道具 ----
	if (id == EffectId::Heal)
	{
		m_playerLives = qMin(m_playerLives + 2, m_playerMaxLives);
		emitEvent(GameEventType::ItemUsed, playerPos().cell, 2, id);
		return true;
	}

	if (id == EffectId::SpeedUp)
	{
		m_speedFactor += 0.15;
		emitEvent(GameEventType::ItemUsed, playerPos().cell, 0, id);
		return true;
	}

	if (id == EffectId::BombRangeUp)
	{
		++m_playerBombRange;
		emitEvent(GameEventType::ItemUsed, playerPos().cell, 0, id);
		return true;
	}

	if (id == EffectId::BombCapacityUp)
	{
		++m_playerMaxBombs;
		emitEvent(GameEventType::ItemUsed, playerPos().cell, 0, id);
		return true;
	}

	// ---- 彩蛋道具（数值与原实现一致）----
	if (id == EffectId::EyeBroken)
	{
		m_playerMaxLives += 2;
		m_enemySpeedFactor *= 0.95;
		const QString extra = rollDropName();
		if (!extra.isEmpty())
		{
			//随机再获得一个掉落物（与原实现一致：直接进背包由界面/存档处理）
			emitEvent(GameEventType::ItemUsed, playerPos().cell, 1, extra);
		}
		return true;
	}

	if (id == EffectId::Eye)
	{
		m_playerMaxLives += 2;
		m_playerLives = qMin(m_playerLives + 2, m_playerMaxLives);

		//对所有敌人造成 1 点伤害
		for (Enemy& enemy : m_enemies)
		{
			if (enemy.dying) { continue; }
			--enemy.hp;
			if (enemy.hp <= 0)
			{
				enemy.dying = true;
				enemy.deathTimer = 0.2;
				emitEvent(GameEventType::EnemyDied, enemy.pos.cell, 1);
				spawnDrop(enemy.pos.cell);
			}
		}

		m_enemySpeedFactor *= 0.85;
		const QString extra = rollDropName();
		if (!extra.isEmpty())
		{
			emitEvent(GameEventType::ItemUsed, playerPos().cell, 1, extra);
		}
		return true;
	}

	if (id == EffectId::StarBroken)
	{
		m_playerLives = 1;
		for (Enemy& enemy : m_enemies)
		{
			if (!enemy.dying) { enemy.hp = 1; }
		}
		m_playerGhost = true;
		m_ghostTimer = 8.0;
		emitEvent(GameEventType::ItemUsed, playerPos().cell, 8, id);
		return true;
	}

	if (id == EffectId::Star)
	{
		for (Enemy& enemy : m_enemies)
		{
			if (!enemy.dying) { enemy.hp = 1; }
		}
		m_playerGhost = true;
		m_ghostTimer = 15.0;
		emitEvent(GameEventType::ItemUsed, playerPos().cell, 15, id);
		return true;
	}

	return false;
}

} // namespace bp
