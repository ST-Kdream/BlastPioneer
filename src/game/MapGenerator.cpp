#include "MapGenerator.h"
#include "GameBoard.h"
#include <QRandomGenerator>

namespace bp
{

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

		//玩家出生点与已被占用的格子跳过
		if (p == spec.playerStart) { continue; }
		if (board.tile(p) != TileType::Empty) { continue; }

		board.setTile(p, TileType::Brick);
		++placed;
	}

	//玩家出生格标记（必须先于敌人，避免敌人抢占出生点）
	board.markEntity(spec.playerStart, EntityKind::Player);

	//撒敌人：只落在空地上，且不落在玩家出生点
	for (int i = 0; i < spec.enemyCount; ++i)
	{
		int enemyAttempts = 0;
		while (enemyAttempts < 100)
		{
			++enemyAttempts;

			const int row = static_cast<int>(rng.bounded(1, kRows - 1));
			const int col = static_cast<int>(rng.bounded(1, kCols - 1));
			const GridPos p(row, col);

			if (p == spec.playerStart) { continue; }
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
