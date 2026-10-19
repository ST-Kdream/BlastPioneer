#include "GameBoard.h"
#include <cstring>

namespace bp
{

GameBoard::GameBoard()
{
	fill(TileType::Empty);
}

// ---------- 格子读写 ----------

TileType GameBoard::tile(const GridPos& p) const
{
	if (!inBounds(p)) { return TileType::Wall; }
	return m_tiles[p.row][p.col];
}

void GameBoard::setTile(const GridPos& p, TileType type)
{
	if (!inBounds(p)) { return; }
	m_tiles[p.row][p.col] = type;
}

// ---------- 可通行 ----------

bool GameBoard::walkable(const GridPos& p) const
{
	if (!inBounds(p)) { return false; }
	const TileType t = m_tiles[p.row][p.col];
	return t != TileType::Wall && t != TileType::Brick;
}

bool GameBoard::destructible(const GridPos& p) const
{
	return inBounds(p) && m_tiles[p.row][p.col] == TileType::Brick;
}

// ---------- 实体标记 ----------

void GameBoard::clearEntity(const GridPos& p)
{
	if (!inBounds(p)) { return; }

	TileType& t = m_tiles[p.row][p.col];
	if (t == TileType::Player || t == TileType::Enemy || t == TileType::Bomb)
	{
		t = TileType::Empty;
	}
}

void GameBoard::markEntity(const GridPos& p, EntityKind kind)
{
	if (!inBounds(p)) { return; }

	TileType& t = m_tiles[p.row][p.col];
	//墙、砖块、以及已被炸弹占用的格都不覆盖
	if (t == TileType::Wall || t == TileType::Brick || t == TileType::Bomb) { return; }

	switch (kind)
	{
	case EntityKind::Player: t = TileType::Player; break;
	case EntityKind::Enemy:  t = TileType::Enemy;  break;
	case EntityKind::Bomb:   t = TileType::Bomb;   break;
	}
}

void GameBoard::clearExplosion(const GridPos& p)
{
	if (!inBounds(p)) { return; }
	if (m_tiles[p.row][p.col] == TileType::Explosion)
	{
		m_tiles[p.row][p.col] = TileType::Empty;
	}
}

// ---------- 地图铺设 ----------

void GameBoard::fill(TileType type)
{
	for (int r = 0; r < kRows; ++r)
	{
		for (int c = 0; c < kCols; ++c)
		{
			m_tiles[r][c] = type;
		}
	}
}

void GameBoard::buildBorderWalls()
{
	for (int r = 0; r < kRows; ++r)
	{
		m_tiles[r][0] = TileType::Wall;
		m_tiles[r][kCols - 1] = TileType::Wall;
	}
	for (int c = 0; c < kCols; ++c)
	{
		m_tiles[0][c] = TileType::Wall;
		m_tiles[kRows - 1][c] = TileType::Wall;
	}
}

//把整张地形拷出去（视图快照用；143 个枚举值，一次 memcpy 的事）
void GameBoard::copyTiles(TileType (&out)[kRows][kCols]) const
{
	std::memcpy(out, m_tiles, sizeof(m_tiles));
}

// ---------- 爆炸波及格 ----------

QList<GridPos> GameBoard::blastCells(const GridPos& center, int range) const
{
	QList<GridPos> cells;
	cells.append(center);

	if (range <= 0) { return cells; }

	//四方向：上、下、左、右
	const int dirs[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };

	for (int d = 0; d < 4; ++d)
	{
		for (int step = 1; step <= range; ++step)
		{
			GridPos p(center.row + dirs[d][0] * step, center.col + dirs[d][1] * step);

			//越界直接停止该方向
			if (!inBounds(p)) { break; }

			const TileType t = tile(p);
			//外墙挡住，砖块被炸（砖块本身也算波及格）
			if (t == TileType::Wall) { break; }

			cells.append(p);

			if (t == TileType::Brick) { break; }
		}
	}

	return cells;
}

} // namespace bp
