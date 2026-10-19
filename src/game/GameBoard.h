#pragma once
#include "GameTypes.h"
#include <QList>

namespace bp
{

// ============================================================
// 棋盘：只负责"格子状态"与几何查询，不含任何游戏规则流程
// 坐标一律用 GridPos（行/列具名），不再出现 map[x][y] 这种易错写法
// ============================================================
class GameBoard
{
public:
	GameBoard();

	// ---------- 尺寸 ----------
	static constexpr int rows() { return kRows; }
	static constexpr int cols() { return kCols; }
	static bool inBounds(const GridPos& p)
	{
		return p.row >= 0 && p.row < kRows && p.col >= 0 && p.col < kCols;
	}

	// ---------- 格子读写 ----------
	TileType tile(const GridPos& p) const;
	void     setTile(const GridPos& p, TileType type);

	//是否可通行（越界、墙、砖块都不可走）
	bool walkable(const GridPos& p) const;
	//是否可破坏
	bool destructible(const GridPos& p) const;

	// ---------- 实体标记 ----------
	//清理实体标记：只清玩家/敌人/炸弹标记，绝不碰墙与砖块
	void clearEntity(const GridPos& p);
	//写入实体标记：墙与砖块上不写入；已放炸弹的格保留炸弹标记
	void markEntity(const GridPos& p, EntityKind kind);
	//把指定格恢复为空地（仅用于爆炸痕消退）
	void clearExplosion(const GridPos& p);

	// ---------- 地图铺设 ----------
	void fill(TileType type);
	void buildBorderWalls();

	//把整张地形拷进调用方的数组（视图快照用）
	//提供它的目的：让"表现层需要地形"不必通过暴露棋盘内部结构来实现
	void copyTiles(TileType (&out)[kRows][kCols]) const;

	// ---------- 几何：爆炸波及格 ----------
	//从 center 起沿四方向扩散，遇墙停止、遇砖块后停止（砖块本身包含在内）
	QList<GridPos> blastCells(const GridPos& center, int range) const;

private:
	TileType m_tiles[kRows][kCols];
};

} // namespace bp
