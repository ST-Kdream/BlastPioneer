#include "MapGenerator.h"
#include "GameBoard.h"
#include <QRandomGenerator>

namespace bp
{

namespace
{
//这个格子是不是"某个玩家的出生格"（playerStart 或多人额外出生点）
bool isSpawnCell(const LevelSpec& spec, const GridPos& p)
{
	if (p == spec.playerStart) { return true; }
	for (const GridPos& start : spec.playerStarts)
	{
		if (p == start) { return true; }
	}
	return false;
}

//出生点周围一圈（3×3）也不放砖块：
//只保护"脚底那一格"仍然可能出现"人站在空地、四面被砖围死"，开局就要靠炸弹自救。
//Bomberman 类游戏的标准做法就是给每个出生点留一块空地。
bool isSpawnSafeZone(const LevelSpec& spec, const GridPos& p)
{
	const auto near = [&p](const GridPos& start)
	{
		return qAbs(p.row - start.row) <= 1 && qAbs(p.col - start.col) <= 1;
	};

	if (near(spec.playerStart)) { return true; }
	for (const GridPos& start : spec.playerStarts)
	{
		if (near(start)) { return true; }
	}
	return false;
}
} // namespace

GeneratedMap RandomMapGenerator::generate(GameBoard& board, const LevelSpec& spec)
{
	GeneratedMap result;

	//清空并按尺寸重铺边框墙
	board.fill(TileType::Empty);
	board.buildBorderWalls();

	//随机数发生器：固定种子 -> 同一关卡每次生成结果一致（便于测试与联机复现）
	QRandomGenerator rng(spec.seed);

	//内部随机砖块（范围严格限制在边框之内：row 1..kRows-2，col 1..kCols-2）
	int placed = 0;
	int attempts = 0;
	const int maxAttempts = spec.brickCount * 4 + 16;
	while (placed < spec.brickCount && attempts < maxAttempts)
	{
		++attempts;

		const int row = static_cast<int>(rng.bounded(1, kRows - 1));
		const int col = static_cast<int>(rng.bounded(1, kCols - 1));
		const GridPos p(row, col);

		//出生点（含多人额外出生点）与它们周围一圈跳过
		if (isSpawnSafeZone(spec, p)) { continue; }
		if (board.tile(p) != TileType::Empty) { continue; }

		board.setTile(p, TileType::Brick);
		++placed;
	}

	//兜一道：即使将来有人改了上面的判定，也绝不能把出生格留成砖块
	for (const GridPos& start : spec.playerStarts)
	{
		if (GameBoard::inBounds(start) && board.tile(start) == TileType::Brick)
		{
			board.setTile(start, TileType::Empty);
		}
	}
	if (GameBoard::inBounds(spec.playerStart) && board.tile(spec.playerStart) == TileType::Brick)
	{
		board.setTile(spec.playerStart, TileType::Empty);
	}

	//玩家出生格标记（必须先于敌人，避免敌人抢占出生点）
	board.markEntity(spec.playerStart, EntityKind::Player);

	//撒敌人：只落在空地上，且不落在任何玩家出生点
	for (int i = 0; i < spec.enemyCount; ++i)
	{
		int enemyAttempts = 0;
		while (enemyAttempts < 100)
		{
			++enemyAttempts;

			const int row = static_cast<int>(rng.bounded(1, kRows - 1));
			const int col = static_cast<int>(rng.bounded(1, kCols - 1));
			const GridPos p(row, col);

			if (isSpawnCell(spec, p)) { continue; }
			if (board.tile(p) != TileType::Empty) { continue; }

			EnemySpawn spawn;
			spawn.pos = p;
			spawn.hp = spec.enemyHp;
			result.enemies.append(spawn);

			board.markEntity(p, EntityKind::Enemy);
			break;
		}
	}

	result.ok = true;
	result.playerStart = spec.playerStart;
	result.hasPlayerStart = true;
	result.usedLayout = false;
	return result;
}

} // namespace bp
