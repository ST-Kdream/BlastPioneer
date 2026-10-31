// ============================================================
// Engine-core tests (console program: no UI, no timers).
//
// This file lives in tests\ next to tests.pro; the engine sources come from
// ..\game (see tests.pro for how the paths are built).
//
// RUN:
//   run_tests.bat        (qmake + nmake + run; sets up MSVC itself)
//
// The build output goes to <repo>\x64\tests (shadow build), so nothing is
// written into the project folder except the sources.
//
// Exit code 0 = all passed, 1 = something failed.
// Keep this file ASCII-only.
// ============================================================
#include "GameBoard.h"
#include "GameConfig.h"
#include "GameEngine.h"
#include "GameLoop.h"
#include "GameTypes.h"
#include "LevelData.h"
#include "MapGenerator.h"
#include "SingleGameEngine.h"
#include "MultiGameEngine.h"
#include "DemoGameEngine.h"
#include "ItemEffects.h"
#include "ModApi.h"
#include "GameplayPatch.h"
#include "UiTokens.h"
#include "NetworkProtocol.h"
#include "NetSession.h"
#include "LaunchGuard.h"
#include "FileManager.h"
#include "DirectoryLayout.h"

// 原生模组 C ABI：这里只做**编译期与数值**层面的契约检查，
// 所以先关掉导出（否则测试程序会凭空声明一个 dllexport 符号）
#define BP_MOD_NO_EXPORT 1
#include "bp_mod_api.h"

#include <QCoreApplication>
#include <QString>
#include <QByteArray>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPointF>
#include <QTemporaryDir>
#include <stdio.h>
#include <math.h>

using namespace bp;

// ---------- tiny assertion helpers ----------
static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond, message)                                               \
	do {                                                                   \
		if (cond) { ++g_passed; }                                          \
		else {                                                             \
			++g_failed;                                                    \
			std::printf("[FAIL] %s:%d  %s\n", __FILE__, __LINE__, message); \
		}                                                                  \
	} while (0)

#define CHECK_EQ_INT(actual, expected, message)                            \
	do {                                                                   \
		const long long a_ = static_cast<long long>(actual);               \
		const long long e_ = static_cast<long long>(expected);             \
		if (a_ == e_) { ++g_passed; }                                       \
		else {                                                             \
			++g_failed;                                                    \
			std::printf("[FAIL] %s:%d  %s (actual=%lld expected=%lld)\n",  \
				__FILE__, __LINE__, message, a_, e_);                      \
		}                                                                  \
	} while (0)

#define CHECK_NEAR(actual, expected, eps, message)                          \
	do {                                                                    \
		const double a_ = static_cast<double>(actual);                      \
		const double e_ = static_cast<double>(expected);                    \
		if (std::fabs(a_ - e_) <= static_cast<double>(eps)) { ++g_passed; }  \
		else {                                                              \
			++g_failed;                                                     \
			std::printf("[FAIL] %s:%d  %s (actual=%.6f expected=%.6f)\n",   \
				__FILE__, __LINE__, message, a_, e_);                       \
		}                                                                   \
	} while (0)

// ============================================================
// 输入分发测试放在这个 include 之后，因为它要用上面的 CHECK 宏
// （顺序有讲究：宏必须先定义）
// ============================================================
#include "test_key_dispatch.h"

// ============================================================
// 1. logical grid coordinates
// ============================================================
static void testGridPos(){	GridPosF a = posFromRowCol(3.25, 4.75);
	CHECK_EQ_INT(a.cell.row, 3, "posFromRowCol positive row");
	CHECK_EQ_INT(a.cell.col, 4, "posFromRowCol positive col");
	CHECK_NEAR(a.offsetY, 0.25, 0.0001, "posFromRowCol row offset");
	CHECK_NEAR(a.offsetX, 0.75, 0.0001, "posFromRowCol col offset");

	GridPosF b = posFromRowCol(-1.5, -0.5);
	CHECK_EQ_INT(b.cell.row, -2, "posFromRowCol negative row floors down");
	CHECK_EQ_INT(b.cell.col, -1, "posFromRowCol negative col floors down");
	CHECK_NEAR(b.offsetY, 0.5, 0.0001, "posFromRowCol negative row offset");

	GridPosF c = posFromRowCol(5.5, 6.5);
	CHECK_NEAR(c.row(), 5.5, 0.0001, "continuous row round-trip");
	CHECK_NEAR(c.col(), 6.5, 0.0001, "continuous col round-trip");

	GridPosF justBelow = posFromRowCol(2.999, 7.999);
	CHECK_EQ_INT(justBelow.cell.row, 2, "2.999 belongs to row 2");
	CHECK_EQ_INT(justBelow.cell.col, 7, "7.999 belongs to col 7");
	CHECK_NEAR(justBelow.offsetY, 0.999, 0.001, "2.999 row offset");

	GridPosF exact = posFromRowCol(5.0, 6.0);
	CHECK_EQ_INT(exact.cell.row, 5, "5.0 belongs to row 5");
	CHECK_NEAR(exact.offsetY, 0.0, 0.000001, "5.0 row offset is 0");

	GridPosF center = GridPosF::centered(GridPos(2, 3));
	CHECK_NEAR(center.row(), 2.5, 0.0001, "centered continuous row");
	CHECK_NEAR(center.col(), 3.5, 0.0001, "centered continuous col");
}

// ============================================================
// 2. board + entity markers
// ============================================================
static void testBoardBasics()
{
	GameBoard board;
	board.fill(TileType::Empty);
	board.buildBorderWalls();

	int wallGaps = 0;
	for (int c = 0; c < kCols; ++c)
	{
		if (board.tile(GridPos(0, c)) != TileType::Wall) { ++wallGaps; }
		if (board.tile(GridPos(kRows - 1, c)) != TileType::Wall) { ++wallGaps; }
	}
	for (int r = 0; r < kRows; ++r)
	{
		if (board.tile(GridPos(r, 0)) != TileType::Wall) { ++wallGaps; }
		if (board.tile(GridPos(r, kCols - 1)) != TileType::Wall) { ++wallGaps; }
	}
	CHECK_EQ_INT(wallGaps, 0, "border walls have no gaps");

	CHECK(!board.walkable(GridPos(-1, 5)), "out of bounds row -1 not walkable");
	CHECK(!board.walkable(GridPos(5, -1)), "out of bounds col -1 not walkable");
	CHECK(!board.walkable(GridPos(kRows, 5)), "out of bounds row high not walkable");
	CHECK(!board.walkable(GridPos(5, kCols)), "out of bounds col high not walkable");

	// REGRESSION: clearing an entity must never erase walls or bricks
	const GridPos inner(1, 1);
	board.setTile(inner, TileType::Brick);
	board.clearEntity(GridPos(0, 0));
	CHECK(board.tile(GridPos(0, 0)) == TileType::Wall, "clearEntity must not erase border wall");
	board.clearEntity(inner);
	CHECK(board.tile(inner) == TileType::Brick, "clearEntity must not erase brick");

	board.markEntity(GridPos(0, 0), EntityKind::Player);
	CHECK(board.tile(GridPos(0, 0)) == TileType::Wall, "must not mark player on wall");
	board.markEntity(inner, EntityKind::Enemy);
	CHECK(board.tile(inner) == TileType::Brick, "must not mark enemy on brick");

	const GridPos free1(2, 2);
	board.markEntity(free1, EntityKind::Player);
	CHECK(board.tile(free1) == TileType::Player, "player marker written");
	board.clearEntity(free1);
	CHECK(board.tile(free1) == TileType::Empty, "player marker cleared");

	const GridPos bombCell(3, 3);
	board.markEntity(bombCell, EntityKind::Bomb);
	board.markEntity(bombCell, EntityKind::Enemy);
	CHECK(board.tile(bombCell) == TileType::Bomb, "bomb cell not overwritten by enemy marker");

	const GridPos expCell(4, 4);
	board.setTile(expCell, TileType::Explosion);
	board.clearExplosion(expCell);
	CHECK(board.tile(expCell) == TileType::Empty, "explosion mark restored to empty");
	board.clearExplosion(inner);
	CHECK(board.tile(inner) == TileType::Brick, "clearing explosion must not touch brick");
}

// ============================================================
// 3. blast cells
// ============================================================
static void testBlastCells()
{
	GameBoard board;
	board.fill(TileType::Empty);
	board.buildBorderWalls();

	CHECK_EQ_INT(board.blastCells(GridPos(5, 5), 2).size(), 1 + 4 * 2, "blast size on open floor, range 2");

	GameBoard board2;
	board2.fill(TileType::Empty);
	board2.buildBorderWalls();
	board2.setTile(GridPos(5, 6), TileType::Brick);
	const QList<GridPos> cells2 = board2.blastCells(GridPos(5, 5), 3);

	int rightCount = 0;
	bool brickIncluded = false;
	for (const GridPos& p : cells2)
	{
		if (p.row == 5 && p.col > 5)
		{
			++rightCount;
			if (p.col == 6) { brickIncluded = true; }
		}
	}
	CHECK(brickIncluded, "brick itself is inside the blast");
	CHECK_EQ_INT(rightCount, 1, "blast stops after the brick");

	GameBoard board3;
	board3.fill(TileType::Wall);
	CHECK_EQ_INT(board3.blastCells(GridPos(5, 5), 3).size(), 1, "all walls: only the center cell");

	GameBoard board4;
	board4.fill(TileType::Empty);
	for (const GridPos& p : board4.blastCells(GridPos(1, 1), 5))
	{
		CHECK(GameBoard::inBounds(p), "blast cells stay in bounds");
	}
	CHECK_EQ_INT(board4.blastCells(GridPos(3, 3), 0).size(), 1, "range 0 blasts only the center");
}

// ============================================================
// 4. map generation
// ============================================================
static void testMapGeneration()
{
	RandomMapGenerator generator;

	LevelSpec spec;
	spec.level = 3;
	spec.brickCount = 35;
	spec.enemyCount = 3;
	spec.enemyHp = 4;
	spec.seed = 12345;

	// Multiplayer passes every corner as a spawn point. The generator must keep
	// all of them (and a one-cell ring around each) free of bricks.
	spec.playerStart = bp::GridPos(1, 1);
	spec.playerStarts = {
		bp::GridPos(1, kCols - 2),
		bp::GridPos(kRows - 2, 1),
		bp::GridPos(kRows - 2, kCols - 2)
	};

	GameBoard boardA;
	GameBoard boardB;
	const GeneratedMap mapA = generator.generate(boardA, spec);
	const GeneratedMap mapB = generator.generate(boardB, spec);

	CHECK(mapA.ok && mapB.ok, "generation succeeded");
	CHECK_EQ_INT(mapA.enemies.size(), 3, "enemy count matches spec");

	bool same = true;
	for (int r = 0; r < kRows && same; ++r)
	{
		for (int c = 0; c < kCols; ++c)
		{
			if (boardA.tile(GridPos(r, c)) != boardB.tile(GridPos(r, c))) { same = false; break; }
		}
	}
	CHECK(same, "same seed produces identical map");

	for (int i = 0; i < mapA.enemies.size() && i < mapB.enemies.size(); ++i)
	{
		CHECK(mapA.enemies[i].pos == mapB.enemies[i].pos, "same seed same enemy position");
	}

	int wallGaps = 0;
	for (int c = 0; c < kCols; ++c)
	{
		if (boardA.tile(GridPos(0, c)) != TileType::Wall) { ++wallGaps; }
		if (boardA.tile(GridPos(kRows - 1, c)) != TileType::Wall) { ++wallGaps; }
	}
	for (int r = 0; r < kRows; ++r)
	{
		if (boardA.tile(GridPos(r, 0)) != TileType::Wall) { ++wallGaps; }
		if (boardA.tile(GridPos(r, kCols - 1)) != TileType::Wall) { ++wallGaps; }
	}
	CHECK_EQ_INT(wallGaps, 0, "border walls intact after generation");
	CHECK(boardA.tile(spec.playerStart) == TileType::Player, "player start marked");

	QList<GridPos> allStarts = spec.playerStarts;
	allStarts.prepend(spec.playerStart);
	for (const GridPos& start : allStarts)
	{
		CHECK(boardA.tile(start) != TileType::Wall, "spawn cell is not a wall");
		CHECK(boardA.tile(start) != TileType::Brick, "spawn cell is not a brick");

		// The standard Bomberman guarantee: the full 3x3 around a spawn is clear.
		for (int dr = -1; dr <= 1; ++dr)
		{
			for (int dc = -1; dc <= 1; ++dc)
			{
				const GridPos p(start.row + dr, start.col + dc);
				if (!GameBoard::inBounds(p)) { continue; }
				CHECK(boardA.tile(p) != TileType::Brick, "spawn safe zone has no brick");
			}
		}
	}

	for (const EnemySpawn& spawn : mapA.enemies)
	{
		CHECK(spawn.pos != spec.playerStart, "enemy does not take player start");
		CHECK(spawn.pos.row >= 1 && spawn.pos.row <= kRows - 2, "enemy not on top/bottom border row");
		CHECK(spawn.pos.col >= 1 && spawn.pos.col <= kCols - 2, "enemy not on left/right border col");
	}

	GameBoard boardC;
	LevelSpec spec2 = spec;
	spec2.seed = 99999;
	generator.generate(boardC, spec2);
	bool anyDiff = false;
	for (int r = 0; r < kRows && !anyDiff; ++r)
	{
		for (int c = 0; c < kCols; ++c)
		{
			if (boardA.tile(GridPos(r, c)) != boardC.tile(GridPos(r, c))) { anyDiff = true; break; }
		}
	}
	CHECK(anyDiff, "different seed produces different map");
}

// ============================================================
// 5. movement is independent of the logic step length
// ============================================================
static double runRightward(SingleGameEngine& engine, int steps, double stepSeconds)
{
	InputState in;
	in.right = true;
	engine.setInput(in);

	for (int i = 0; i < steps; ++i)
	{
		engine.tick(stepSeconds);
	}
	return engine.playerPos().col();
}

static void testMovementFrameRateIndependence()
{
	GameConfig config;
	config.playerSpeed = 3.5;
	config.enemyCountPerLevel = 0;

	const double expected = 3.5;

	{
		SingleGameEngine engine(config);
		engine.setRandomSeed(1);
		engine.startLevel(1);
		const double start = engine.playerPos().col();
		const double end = runRightward(engine, 60, 1.0 / 60.0);
		CHECK_NEAR(end - start, expected, 0.25, "60 steps x 1/60s = 3.5 cells");
	}
	{
		SingleGameEngine engine(config);
		engine.setRandomSeed(1);
		engine.startLevel(1);
		const double start = engine.playerPos().col();
		const double end = runRightward(engine, 30, 1.0 / 30.0);
		CHECK_NEAR(end - start, expected, 0.25, "30 steps x 1/30s = 3.5 cells (frame-rate independent)");
	}
	{
		SingleGameEngine engine(config);
		engine.setRandomSeed(1);
		engine.startLevel(1);
		const double start = engine.playerPos().col();
		const double end = runRightward(engine, 120, 1.0 / 120.0);
		CHECK_NEAR(end - start, expected, 0.25, "120 steps x 1/120s = 3.5 cells (frame-rate independent)");
	}
}

// ============================================================
// 6. input takes effect / no input means no movement
// ============================================================
static void testEngineInput()
{
	GameConfig config;
	config.enemyCountPerLevel = 0;

	SingleGameEngine engine(config);
	engine.setRandomSeed(2);
	engine.startLevel(1);

	const GridPosF before = engine.playerPos();
	for (int i = 0; i < 30; ++i) { engine.tick(1.0 / 60.0); }
	CHECK_NEAR(engine.playerPos().col(), before.col(), 0.0001, "no input: col unchanged");
	CHECK_NEAR(engine.playerPos().row(), before.row(), 0.0001, "no input: row unchanged");

	InputState right;
	right.right = true;
	engine.setInput(right);
	for (int i = 0; i < 12; ++i) { engine.tick(1.0 / 60.0); }
	CHECK(engine.playerPos().col() > before.col(), "right key increases col");

	engine.setInput(InputState());
	const double settled = engine.playerPos().col();
	for (int i = 0; i < 12; ++i) { engine.tick(1.0 / 60.0); }
	CHECK_NEAR(engine.playerPos().col(), settled, 0.0001, "release key stops movement");
}

// ============================================================
// 7. walking into walls: stay inside, borders stay intact
// ============================================================
static void testEngineWallsStayIntact()
{
	GameConfig config;
	config.enemyCountPerLevel = 0;
	config.playerSpeed = 6.0;

	SingleGameEngine engine(config);
	engine.setRandomSeed(3);
	engine.startLevel(1);

	InputState in;
	in.left = true;
	engine.setInput(in);
	for (int i = 0; i < 120; ++i) { engine.tick(1.0 / 60.0); }

	in = InputState();
	in.up = true;
	engine.setInput(in);
	for (int i = 0; i < 120; ++i) { engine.tick(1.0 / 60.0); }

	const GridPosF now = engine.playerPos();
	CHECK(GameBoard::inBounds(now.cell), "player cell is in bounds after ramming walls");

	int wallGaps = 0;
	for (int c = 0; c < kCols; ++c)
	{
		if (engine.board().tile(GridPos(0, c)) != TileType::Wall) { ++wallGaps; }
		if (engine.board().tile(GridPos(kRows - 1, c)) != TileType::Wall) { ++wallGaps; }
	}
	for (int r = 0; r < kRows; ++r)
	{
		if (engine.board().tile(GridPos(r, 0)) != TileType::Wall) { ++wallGaps; }
		if (engine.board().tile(GridPos(r, kCols - 1)) != TileType::Wall) { ++wallGaps; }
	}
	CHECK_EQ_INT(wallGaps, 0, "border walls intact after ramming");
}

// ============================================================
// 8. bomb fuse, explosion mark lifetime
// ============================================================
static void testEngineBombAndExplosion()
{
	GameConfig config;
	config.enemyCountPerLevel = 0;
	config.playerBombFuse = 1.5;
	config.explosionPersistSeconds = 0.3;

	SingleGameEngine engine(config);
	engine.setRandomSeed(4);
	engine.startLevel(1);

	CHECK_EQ_INT(engine.bombs().size(), 0, "no bomb at start");

	InputState in;
	in.placeBomb = true;
	engine.setInput(in);
	engine.tick(1.0 / 60.0);
	CHECK_EQ_INT(engine.bombs().size(), 1, "placing a bomb increases bomb count");
	CHECK(engine.board().tile(engine.playerPos().cell) == TileType::Bomb, "bomb cell marked");

	engine.setInput(InputState());

	// The explosion mark only lives explosionPersistSeconds (0.3s), so check for
	// it WHILE the fuse is still burning out, not after: step until the bomb is
	// gone, then look for the mark in the following step.
	bool hasExplosion = false;
	int firstMarkTick = -1;
	int remainingBombs = -1;
	for (int i = 0; i < 95; ++i)
	{
		engine.tick(1.0 / 60.0);
		if (remainingBombs < 0)
		{
			remainingBombs = engine.bombs().size();
		}
		if (!hasExplosion)
		{
			for (int r = 0; r < kRows && !hasExplosion; ++r)
			{
				for (int c = 0; c < kCols; ++c)
				{
					if (engine.board().tile(GridPos(r, c)) == TileType::Explosion)
					{
						hasExplosion = true;
						firstMarkTick = i;
						break;
					}
				}
			}
		}
	}
	CHECK_EQ_INT(engine.bombs().size(), 0, "bomb gone after fuse burns out");
	CHECK(hasExplosion, "explosion leaves a mark");

	int clearedAfter = -1;
	for (int i = 0; i < 60; ++i)
	{
		engine.tick(1.0 / 60.0);

		bool found = false;
		for (int r = 0; r < kRows && !found; ++r)
		{
			for (int c = 0; c < kCols; ++c)
			{
				if (engine.board().tile(GridPos(r, c)) == TileType::Explosion) { found = true; break; }
			}
		}
		if (!found) { clearedAfter = i + 1; break; }
	}

	std::printf("  [debug] first mark at tick %d, cleared after %d ticks\n", firstMarkTick, clearedAfter);
	CHECK(clearedAfter >= 0, "explosion mark expires back to empty");
}

// ============================================================
// 9. determinism with a fixed seed
// ============================================================
static void testEngineDeterminism()
{
	GameConfig config;
	config.enemyCountPerLevel = 1;

	QList<GridPos> cellsA;
	QList<GridPos> cellsB;
	double colA = 0.0;
	double colB = 0.0;

	for (int run = 0; run < 2; ++run)
	{
		SingleGameEngine engine(config);
		engine.setRandomSeed(777);
		engine.startLevel(2);

		InputState in;
		in.right = true;
		engine.setInput(in);

		for (int i = 0; i < 120; ++i) { engine.tick(1.0 / 60.0); }

		QList<GridPos>& target = (run == 0) ? cellsA : cellsB;
		target.clear();
		for (const Enemy& enemy : engine.enemies())
		{
			target.append(enemy.pos.cell);
		}

		if (run == 0) { colA = engine.playerPos().col(); }
		else { colB = engine.playerPos().col(); }
	}

	CHECK_EQ_INT(cellsA.size(), cellsB.size(), "fixed seed: same enemy count");
	bool sameCells = (cellsA.size() == cellsB.size());
	for (int i = 0; i < cellsA.size() && i < cellsB.size() && sameCells; ++i)
	{
		if (cellsA[i] != cellsB[i]) { sameCells = false; }
	}
	CHECK(sameCells, "fixed seed: same enemy cells");
	CHECK_NEAR(colA, colB, 0.0001, "fixed seed: same player position");
}

// ============================================================
// 10. enemies never leave the map nor park on the border
// ============================================================
static void testEngineEnemiesStayInside()
{
	GameConfig config;
	config.enemyCountPerLevel = 3;
	config.enemySpeed = 5.0;

	SingleGameEngine engine(config);
	engine.setRandomSeed(5);
	engine.startLevel(2);

	for (int i = 0; i < 600; ++i)
	{
		engine.tick(1.0 / 60.0);
		if (engine.phase() != Phase::Running) { break; }
	}

	int outside = 0;
	int onBorder = 0;
	for (const Enemy& enemy : engine.enemies())
	{
		if (!GameBoard::inBounds(enemy.pos.cell)) { ++outside; }
		if (!enemy.dying)
		{
			if (enemy.pos.cell.row == 0 || enemy.pos.cell.col == 0) { ++onBorder; }
		}
	}

	CHECK_EQ_INT(outside, 0, "enemies always inside the map");
	CHECK_EQ_INT(onBorder, 0, "enemies never park on border cells");
}

// ============================================================
// 11. GameLoop stepping and accumulator
// ============================================================
static void testGameLoopStepping()
{
	GameConfig config;
	config.enemyCountPerLevel = 0;

	SingleGameEngine engine(config);
	engine.setRandomSeed(6);
	engine.startLevel(1);

	GameLoop loop(&engine);
	loop.setStepSeconds(1.0 / 60.0);

	const int before = engine.stepIndex();
	for (int i = 0; i < 25; ++i) { loop.advanceStep(); }
	CHECK_EQ_INT(engine.stepIndex() - before, 25, "advanceStep advances 25 steps");

	const int mid = engine.stepIndex();
	loop.pump(0.05);
	CHECK_EQ_INT(engine.stepIndex() - mid, 3, "pump(0.05s) advances 3 logic steps");

	const int high = engine.stepIndex();
	loop.pump(10.0);
	CHECK_EQ_INT(engine.stepIndex() - high, 4, "a huge frame is capped at 4 steps");
	CHECK(loop.droppedSteps() > 0, "excess steps are recorded as dropped");
}

// ============================================================
// 12. geometry invariants
// ============================================================
static void testGeometryInvariants()
{
	CHECK_EQ_INT(kRows, 11, "rows fixed at 11");
	CHECK_EQ_INT(kCols, 13, "cols fixed at 13");
	CHECK(GameBoard::inBounds(GridPos(1, 1)), "player start (1,1) is in bounds");
	CHECK(GameBoard::inBounds(GridPos(kRows - 2, kCols - 2)), "bottom-right playable corner in bounds");

	// The outer ring IS inside the array, but it must never be walkable:
	// that is exactly what stops players/enemies from leaving the map.
	GameBoard board;
	board.fill(TileType::Empty);
	board.buildBorderWalls();
	CHECK(!board.walkable(GridPos(0, 0)), "top-left border cell is not walkable");
	CHECK(!board.walkable(GridPos(kRows - 1, kCols - 1)), "bottom-right border cell is not walkable");
	CHECK(board.walkable(GridPos(1, 1)), "the cell next to the border is walkable");
}

// ============================================================
// 13. a brand-new game mode must be cheap: DemoGameEngine proof
//     (this is the whole point of phase 3 -- "new mode = subclass")
// ============================================================
static void testDemoModeEngine()
{
	bp::GameConfig config;
	config.enemyCountPerLevel = 0;   // demo mode has no enemies
	config.playerSpeed = 4.0;

	bp::DemoGameEngine engine(config);
	engine.startLevel(1);

	// starts in the middle of the board
	CHECK(engine.phase() == bp::Phase::Running, "demo mode starts running");
	CHECK_NEAR(engine.playerPos().row(), bp::kRows / 2 + 0.5, 0.001, "demo player row is centered");
	CHECK_NEAR(engine.playerPos().col(), bp::kCols / 2 + 0.5, 0.001, "demo player col is centered");

	// moves with input
	const double startCol = engine.playerPos().col();
	InputState right;
	right.right = true;
	engine.setInput(right);
	for (int i = 0; i < 30; ++i) { engine.tick(1.0 / 60.0); }
	CHECK(engine.playerPos().col() > startCol, "demo player moves right with input");

	// places a bomb, and it shows up in the snapshot with a fuse progress
	engine.setInput(InputState());
	InputState place;
	place.placeBomb = true;
	engine.setInput(place);
	engine.tick(1.0 / 60.0);
	CHECK_EQ_INT(engine.bombs().size(), 1, "demo mode places a bomb");
	CHECK_EQ_INT(engine.snapshot().bombViews.size(), 1, "snapshot exposes a bomb view");
	CHECK(engine.snapshot().bombViews.first().owner == bp::BombOwner::Player, "bomb view keeps its owner");
	CHECK_NEAR(engine.snapshot().bombViews.first().progress, 1.0, 0.05, "fresh bomb fuse progress is 1");

	// the fuse burns down and the bomb explodes (event + explosion mark)
	engine.setInput(InputState());
	bool sawExplosionEvent = false;
	for (int i = 0; i < 120 && !sawExplosionEvent; ++i)
	{
		engine.tick(1.0 / 60.0);
		if (engine.snapshot().eventCount(bp::GameEventType::BombExploded) > 0) { sawExplosionEvent = true; }
	}
	CHECK(sawExplosionEvent, "demo mode reports a BombExploded event");
	CHECK_EQ_INT(engine.bombs().size(), 0, "demo bomb is gone after exploding");
	CHECK(engine.explosions() >= 1, "demo mode counts the explosion");
}

// ============================================================
// 14. multiplayer: players, spawns, snapshot
// ============================================================

// A deterministic map generator used by the multiplayer tests: an empty board
// with border walls plus exactly the bricks we ask for. Two reasons to plug it in:
//   1. the tests never depend on random layout (no flaky assertions)
//   2. it proves the IMapGenerator seam actually works -- this is the same seam
//      that a JSON/mods level generator will use later
class FixedMapGenerator : public bp::IMapGenerator
{
public:
	explicit FixedMapGenerator(const QList<bp::GridPos>& bricks = QList<bp::GridPos>())
		: m_bricks(bricks) {}

	bp::GeneratedMap generate(bp::GameBoard& board, const bp::LevelSpec& spec) override
	{
		Q_UNUSED(spec);
		board.fill(bp::TileType::Empty);
		board.buildBorderWalls();
		for (const bp::GridPos& cell : m_bricks)
		{
			board.setTile(cell, bp::TileType::Brick);
		}

		bp::GeneratedMap result;
		result.ok = true;
		return result;
	}

	QString name() const override { return QStringLiteral("FixedMapGenerator"); }

private:
	QList<bp::GridPos> m_bricks;
};

static void testMultiplayerSetup()
{
	bp::GameConfig config;
	config.playerMaxLives = 3;
	config.playerBombRange = 1;
	config.playerMaxBombs = 2;

	bp::MultiGameEngine engine(config);
	engine.setMapGenerator(std::make_unique<FixedMapGenerator>());
	engine.setRandomSeed(2024);
	engine.setLocalPlayerId(1);
	engine.addPlayer(1, QStringLiteral("P1"));
	engine.addPlayer(2, QStringLiteral("P2"));
	engine.addPlayer(3, QStringLiteral("P3"));
	engine.addPlayer(4, QStringLiteral("P4"));

	CHECK_EQ_INT(engine.players().size(), 4, "four players registered");
	CHECK_EQ_INT(engine.aliveCount(), 4, "everyone starts alive");
	CHECK(engine.playerName(2) == QStringLiteral("P2"), "player names are kept");

	// registering the same id twice must not create a duplicate
	engine.addPlayer(2, QStringLiteral("P2-again"));
	CHECK_EQ_INT(engine.players().size(), 4, "duplicate player id is ignored");
	CHECK(engine.playerName(2) == QStringLiteral("P2"), "duplicate registration keeps the first name");

	CHECK(engine.beginMatch(), "beginMatch starts the match");
	CHECK(engine.phase() == bp::Phase::Running, "match phase is Running");

	QList<bp::GridPos> cells;
	for (const bp::MultiPlayer& player : engine.players())
	{
		cells.append(player.pos.cell);
		CHECK_EQ_INT(player.lives, config.playerMaxLives, "player starts at full lives");
		CHECK_EQ_INT(player.maxBombs, config.playerMaxBombs, "player starts with the configured bomb limit");
		CHECK_EQ_INT(player.bombRange, config.playerBombRange, "player starts with the configured range");
	}

	const bool distinct = (cells.size() == 4)
		&& cells[0] != cells[1] && cells[0] != cells[2] && cells[0] != cells[3]
		&& cells[1] != cells[2] && cells[1] != cells[3] && cells[2] != cells[3];
	CHECK(distinct, "four players spawn on four distinct corners");
	CHECK(engine.board().tile(cells[0]) != bp::TileType::Wall, "spawn cell is not a wall");
	CHECK(engine.board().tile(cells[0]) != bp::TileType::Brick, "spawn cell is not a brick");

	// Regression: the random generator used to protect only points[0], so with
	// a real (random) map players 2-4 could spawn inside bricks. Every seed must
	// place every player on a walkable cell.
	for (const quint32 seed : { 1u, 7u, 42u, 2024u, 99999u })
	{
		bp::MultiGameEngine randomEngine(config);
		randomEngine.setMapGenerator(std::make_unique<bp::RandomMapGenerator>());
		randomEngine.setRandomSeed(seed);
		randomEngine.setLocalPlayerId(1);
		randomEngine.addPlayer(1, QStringLiteral("P1"));
		randomEngine.addPlayer(2, QStringLiteral("P2"));
		randomEngine.addPlayer(3, QStringLiteral("P3"));
		randomEngine.addPlayer(4, QStringLiteral("P4"));
		randomEngine.beginMatch();

		for (const bp::MultiPlayer& player : randomEngine.players())
		{
			CHECK(randomEngine.board().walkable(player.pos.cell),
				  "random spawn puts every player on a walkable cell");
		}
	}

	const bp::GameSnapshot& snap = engine.snapshot();
	CHECK_EQ_INT(snap.players.size(), 4, "snapshot exposes the multiplayer player list");
	CHECK_EQ_INT(snap.localPlayerIndex, 0, "snapshot marks which entry is the local player");
	CHECK(snap.players.at(0).local, "player 1 is flagged local");
	CHECK(!snap.players.at(1).local, "player 2 is not flagged local");

	// the map is reproducible: same seed -> same board
	bp::MultiGameEngine twin(config);
	twin.setMapGenerator(std::make_unique<FixedMapGenerator>());
	twin.setMapGenerator(std::make_unique<bp::RandomMapGenerator>());
	twin.setRandomSeed(2024);
	twin.setLocalPlayerId(1);
	twin.addPlayer(1, QStringLiteral("P1"));
	twin.addPlayer(2, QStringLiteral("P2"));
	twin.beginMatch();

	bp::MultiGameEngine twin2(config);
	twin2.setMapGenerator(std::make_unique<bp::RandomMapGenerator>());
	twin2.setRandomSeed(2024);
	twin2.setLocalPlayerId(1);
	twin2.addPlayer(1, QStringLiteral("P1"));
	twin2.addPlayer(2, QStringLiteral("P2"));
	twin2.beginMatch();

	int differing = 0;
	for (int r = 0; r < bp::kRows; ++r)
	{
		for (int c = 0; c < bp::kCols; ++c)
		{
			if (twin.board().tile(bp::GridPos(r, c)) != twin2.board().tile(bp::GridPos(r, c)))
			{
				++differing;
			}
		}
	}
	CHECK_EQ_INT(differing, 0, "the match seed alone decides the map (host and client agree)");
}

// ============================================================
// 15. multiplayer: input isolation and bomb ownership
// ============================================================
static void testMultiplayerInputAndBombs()
{
	bp::GameConfig config;
	config.playerSpeed = 3.5;
	config.playerBombRange = 1;
	config.playerMaxBombs = 1;
	config.playerBombFuse = 1.5;
	config.dropChance = 0.0;                 // keep drops out of this test

	bp::MultiGameEngine engine(config);
	engine.setMapGenerator(std::make_unique<FixedMapGenerator>());
	engine.setRandomSeed(11);
	engine.setLocalPlayerId(1);
	engine.addPlayer(1, QStringLiteral("P1"));
	engine.addPlayer(2, QStringLiteral("P2"));
	engine.setSpawnPoints(QList<bp::GridPos>{ bp::GridPos(1, 1), bp::GridPos(1, 6) });
	engine.beginMatch();

	// only player 1 gets input: player 2 must stay put
	bp::InputState right;
	right.right = true;
	engine.submitRemoteInput(1, right);

	const double p1Start = engine.players().at(0).pos.col();
	const double p2Start = engine.players().at(1).pos.col();

	for (int i = 0; i < 30; ++i) { engine.tick(1.0 / 60.0); }

	CHECK(engine.players().at(0).pos.col() > p1Start, "player 1 moves with its own input");
	CHECK_NEAR(engine.players().at(1).pos.col(), p2Start, 0.0001, "player 2 stays still without input");

	// input for an unknown player id is dropped, not applied to someone else
	bp::InputState left;
	left.left = true;
	engine.submitRemoteInput(99, left);
	const double p1Before = engine.players().at(0).pos.col();
	engine.tick(1.0 / 60.0);
	CHECK_NEAR(engine.players().at(0).pos.col(), p1Before + 3.5 / 60.0, 0.0001,
			   "input for an unknown player is ignored (player 1 keeps moving right)");

	// bomb ownership
	bp::InputState place;
	place.placeBomb = true;
	engine.submitRemoteInput(1, place);
	engine.tick(1.0 / 60.0);

	CHECK_EQ_INT(engine.bombs().size(), 1, "player 1 placed a bomb");
	CHECK_EQ_INT(engine.bombs().at(0).ownerId, 1, "the bomb records who placed it");
	CHECK_EQ_INT(engine.bombCountOf(1), 1, "bombCountOf counts that player's bombs");
	CHECK_EQ_INT(engine.bombCountOf(2), 0, "the other player owns no bombs");
	CHECK_EQ_INT(engine.snapshot().bombViews.size(), 1, "the bomb shows up in the snapshot");
	CHECK_EQ_INT(engine.snapshot().bombViews.at(0).ownerId, 1, "the bomb view keeps the owner id");

	// per-player limit: a second bomb of the same player is refused
	engine.submitRemoteInput(1, place);
	engine.tick(1.0 / 60.0);
	CHECK_EQ_INT(engine.bombs().size(), 1, "each player has their own bomb limit");

	// but the other player may place one
	engine.submitRemoteInput(1, bp::InputState());
	engine.submitRemoteInput(2, place);
	engine.tick(1.0 / 60.0);
	CHECK_EQ_INT(engine.bombs().size(), 2, "the other player can still place a bomb");
	CHECK_EQ_INT(engine.bombCountOf(2), 1, "the second bomb belongs to player 2");

	// the two bombs are on different cells (each player bombs under themselves)
	CHECK(engine.bombs().at(0).pos.cell != engine.bombs().at(1).pos.cell,
		  "bombs are placed on their owner's own cell");
}

// ============================================================
// 16. multiplayer: blast damage rules
// ============================================================
static void testMultiplayerBlastDamage()
{
	bp::GameConfig config;
	config.playerMaxLives = 3;
	config.playerBombRange = 1;
	config.playerBombFuse = 1.5;
	config.dropChance = 0.0;

	{
		// default: you are immune to your own bomb (the old multiplayer behaviour)
		bp::MultiGameEngine engine(config);
		engine.setMapGenerator(std::make_unique<FixedMapGenerator>());
		engine.setRandomSeed(5);
		engine.setLocalPlayerId(1);
		engine.addPlayer(1, QStringLiteral("P1"));
		engine.addPlayer(2, QStringLiteral("P2"));
		engine.addPlayer(3, QStringLiteral("P3"));
		engine.setSpawnPoints(QList<bp::GridPos>{ bp::GridPos(1, 1), bp::GridPos(2, 1), bp::GridPos(8, 8) });
		engine.beginMatch();

		CHECK(engine.tryPlaceBombFor(1), "player 1 places a bomb under itself");

		int damageEvents = 0;
		for (int i = 0; i < 200 && engine.isRunning(); ++i)
		{
			engine.tick(1.0 / 60.0);
			damageEvents += engine.snapshot().eventCount(bp::GameEventType::PlayerDamaged);
		}

		CHECK_EQ_INT(engine.players().at(0).lives, 3, "a player is not hurt by their own bomb by default");
		CHECK_EQ_INT(engine.players().at(1).lives, 2, "the other player inside the blast loses one life");
		CHECK_EQ_INT(engine.players().at(2).lives, 3, "a distant player is untouched");
		CHECK_EQ_INT(damageEvents, 1, "exactly one damage event was emitted");
		CHECK(engine.board().tile(bp::GridPos(1, 1)) != bp::TileType::Bomb, "the bomb cell was cleared");
	}

	{
		// multiSelfDamage = true turns on the classic bomberman rule
		bp::GameConfig selfHarm = config;
		selfHarm.multiSelfDamage = true;

		bp::MultiGameEngine engine(selfHarm);
		engine.setMapGenerator(std::make_unique<FixedMapGenerator>());
		engine.setRandomSeed(5);
		engine.setLocalPlayerId(1);
		engine.addPlayer(1, QStringLiteral("P1"));
		engine.addPlayer(2, QStringLiteral("P2"));
		engine.setSpawnPoints(QList<bp::GridPos>{ bp::GridPos(1, 1), bp::GridPos(8, 8) });
		engine.beginMatch();

		CHECK(engine.tryPlaceBombFor(1), "self-damage run: bomb placed");
		for (int i = 0; i < 200 && engine.isRunning(); ++i) { engine.tick(1.0 / 60.0); }

		CHECK_EQ_INT(engine.players().at(0).lives, 2, "with multiSelfDamage the owner is hurt too");
	}
}

// ============================================================
// 17. multiplayer: a client mirrors the host and never runs rules
// ============================================================
static void testMultiplayerClientMirror()
{
	bp::GameConfig config;
	config.dropChance = 0.0;

	bp::MultiGameEngine engine(config);
	engine.setMapGenerator(std::make_unique<FixedMapGenerator>());
	engine.setRandomSeed(7);
	engine.setLocalPlayerId(2);
	engine.addPlayer(1, QStringLiteral("P1"));
	engine.addPlayer(2, QStringLiteral("P2"));
	engine.setAuthoritative(false);
	engine.setSpawnPoints(QList<bp::GridPos>{ bp::GridPos(1, 1), bp::GridPos(1, 2) });
	engine.beginMatch();

	CHECK(!engine.isAuthoritative(), "the client engine is not authoritative");

	// a locally submitted input must not move anything on a client
	bp::InputState right;
	right.right = true;
	engine.submitRemoteInput(2, right);

	const double before = engine.players().at(1).pos.col();
	for (int i = 0; i < 30; ++i) { engine.tick(1.0 / 60.0); }
	CHECK_NEAR(engine.players().at(1).pos.col(), before, 0.0001,
			   "a non-authoritative engine does not run the movement rules");

	// it mirrors the host instead
	QList<int> ids;
	ids << 1 << 2;
	QList<bp::GridPosF> positions;
	positions << bp::GridPosF::centered(bp::GridPos(3, 3))
			  << bp::GridPosF::centered(bp::GridPos(4, 4));
	QList<int> lives;
	lives << 2 << 1;

	engine.applyRemotePlayers(ids, positions, lives);

	CHECK_NEAR(engine.players().at(0).pos.col(), 3.5, 0.0001, "mirrored position of player 1");
	CHECK_EQ_INT(engine.players().at(1).lives, 1, "mirrored lives of player 2");
	CHECK_NEAR(engine.snapshot().players.at(1).pos.row(), 4.5, 0.0001, "mirrored state reaches the snapshot");

	// Optional stat lists (pickups change range / capacity / max lives): the
	// client HUD must mirror them too, not just position and current lives.
	QList<int> ranges;
	ranges << 5 << 2;
	QList<int> maxBombs;
	maxBombs << 6 << 3;
	QList<int> maxLives;
	maxLives << 7 << 4;
	engine.applyRemotePlayers(ids, positions, lives, ranges, maxBombs, maxLives);

	CHECK_EQ_INT(engine.players().at(0).bombRange, 5, "mirrored bomb range of player 1");
	CHECK_EQ_INT(engine.players().at(1).maxBombs, 3, "mirrored bomb capacity of player 2");
	CHECK_EQ_INT(engine.snapshot().players.at(0).maxLives, 7, "mirrored max lives reach the snapshot");

	// mirrored bombs keep their fuse progress, and the board marker follows
	QList<bp::Bomb> bombs;
	bp::Bomb bomb;
	bomb.pos = bp::GridPosF::centered(bp::GridPos(2, 2));
	bomb.owner = bp::BombOwner::Player;
	bomb.ownerId = 1;
	bomb.fuse = 1.0;
	bomb.fuseTotal = 2.0;
	bomb.range = 2;
	bombs.append(bomb);

	engine.applyRemoteBombs(bombs);

	CHECK_EQ_INT(engine.snapshot().bombViews.size(), 1, "the mirrored bomb appears in the snapshot");
	CHECK_NEAR(engine.snapshot().bombViews.at(0).progress, 0.5, 0.001, "the mirrored bomb keeps its fuse progress");
	CHECK(engine.board().tile(bp::GridPos(2, 2)) == bp::TileType::Bomb, "the mirrored bomb is marked on the board");

	// replacing the mirrored list clears the old board marker again
	engine.applyRemoteBombs(QList<bp::Bomb>());
	CHECK_EQ_INT(engine.snapshot().bombViews.size(), 0, "an empty mirror list clears the bombs");
	CHECK(engine.board().tile(bp::GridPos(2, 2)) != bp::TileType::Bomb, "the stale board marker is gone");

	// mirrored drops
	QList<bp::DropItem> drops;
	bp::DropItem drop;
	drop.pos = bp::GridPosF::centered(bp::GridPos(5, 5));
	drop.name = QStringLiteral("mod_item");
	drops.append(drop);
	engine.applyRemoteDrops(drops);

	CHECK_EQ_INT(engine.drops().size(), 1, "the mirrored drop appears");
	CHECK(engine.drops().first().name == QStringLiteral("mod_item"), "the mirrored drop keeps its name");
}

// ============================================================
// 18. multiplayer: win condition
// ============================================================
static void testMultiplayerWinCondition()
{
	bp::GameConfig config;
	config.playerMaxLives = 1;
	config.playerBombRange = 1;
	config.dropChance = 0.0;

	bp::MultiGameEngine engine(config);
	engine.setMapGenerator(std::make_unique<FixedMapGenerator>());
	engine.setRandomSeed(9);
	engine.setLocalPlayerId(1);
	engine.addPlayer(1, QStringLiteral("P1"));
	engine.addPlayer(2, QStringLiteral("P2"));
	engine.setSpawnPoints(QList<bp::GridPos>{ bp::GridPos(1, 1), bp::GridPos(2, 1) });
	engine.beginMatch();

	CHECK(engine.isRunning(), "match is running before the blast");
	CHECK_EQ_INT(engine.winnerId(), -1, "no winner yet");
	CHECK(engine.tryPlaceBombFor(1), "player 1 places the deciding bomb");

	for (int i = 0; i < 200 && engine.isRunning(); ++i) { engine.tick(1.0 / 60.0); }

	CHECK(!engine.isRunning(), "the match ends when only one player is left");
	CHECK_EQ_INT(engine.aliveCount(), 1, "exactly one player survived");
	CHECK_EQ_INT(engine.winnerId(), 1, "the surviving player is the winner");
	CHECK(engine.phase() == bp::Phase::Victory, "the winning (local) player sees Victory");
	CHECK_EQ_INT(engine.players().at(1).lives, 0, "the other player ran out of lives");

	// a one-player match must never declare itself the winner
	bp::MultiGameEngine solo(config);
	solo.setMapGenerator(std::make_unique<FixedMapGenerator>());
	solo.setRandomSeed(3);
	solo.setLocalPlayerId(1);
	solo.addPlayer(1, QStringLiteral("solo"));
	solo.beginMatch();
	for (int i = 0; i < 120; ++i) { solo.tick(1.0 / 60.0); }
	CHECK(solo.isRunning(), "a one-player match never ends by itself");
	CHECK_EQ_INT(solo.winnerId(), -1, "a one-player match has no winner");
}

// ============================================================
// 19. multiplayer: drops, pickups and per-player item effects
// ============================================================
static void testMultiplayerDropsAndEffects()
{
	bp::GameConfig config;
	config.playerMaxLives = 3;
	config.playerBombRange = 1;
	config.playerMaxBombs = 1;
	config.dropChance = 1.0;                 // every destroyed brick drops an item

	bp::MultiGameEngine engine(config);
	engine.setMapGenerator(std::make_unique<FixedMapGenerator>(QList<bp::GridPos>{ bp::GridPos(1, 2) }));
	engine.setRandomSeed(123);
	engine.setLocalPlayerId(1);
	engine.addPlayer(1, QStringLiteral("P1"));
	engine.addPlayer(2, QStringLiteral("P2"));
	engine.setSpawnPoints(QList<bp::GridPos>{ bp::GridPos(1, 1), bp::GridPos(8, 8) });
	engine.beginMatch();

	CHECK(engine.tryPlaceBombFor(1), "bomb placed next to the brick");
	for (int i = 0; i < 200 && engine.isRunning(); ++i) { engine.tick(1.0 / 60.0); }

	CHECK(engine.board().tile(bp::GridPos(1, 2)) == bp::TileType::Empty, "the brick was destroyed");
	CHECK_EQ_INT(engine.drops().size(), 1, "a destroyed brick dropped an item");

	// walking onto the drop picks it up -- and only the player standing there
	// (the brick was to the right of the spawn point, so walk right)
	bp::InputState right2;
	right2.right = true;
	engine.submitRemoteInput(1, right2);

	for (int i = 0; i < 120 && !engine.drops().isEmpty(); ++i) { engine.tick(1.0 / 60.0); }
	CHECK_EQ_INT(engine.drops().size(), 0, "the player picked the drop up by walking onto it");
	engine.submitRemoteInput(1, bp::InputState());

	// effect table: ids come from itemInfo.json, and each one touches one player only
	bp::MultiGameEngine effects(config);
	effects.setMapGenerator(std::make_unique<FixedMapGenerator>());
	effects.setRandomSeed(77);
	effects.setLocalPlayerId(1);
	effects.addPlayer(1, QStringLiteral("P1"));
	effects.addPlayer(2, QStringLiteral("P2"));
	effects.setSpawnPoints(QList<bp::GridPos>{ bp::GridPos(1, 1), bp::GridPos(8, 8) });
	effects.beginMatch();

	const int rangeBefore = effects.players().at(0).bombRange;
	CHECK(effects.applyEffectToPlayer(1, QStringLiteral("bomb_range_up")), "bomb_range_up is a known effect");
	CHECK_EQ_INT(effects.players().at(0).bombRange, rangeBefore + 1, "bomb_range_up raises that player's range");
	CHECK_EQ_INT(effects.players().at(1).bombRange, config.playerBombRange, "the other player is unaffected");

	const int bombsBefore = effects.players().at(0).maxBombs;
	CHECK(effects.applyEffectToPlayer(1, QStringLiteral("bomb_capacity_up")), "bomb_capacity_up is a known effect");
	CHECK_EQ_INT(effects.players().at(0).maxBombs, bombsBefore + 1, "bomb_capacity_up raises that player's limit");

	// the "effect:" prefix form works too, and the local player shortcut uses it
	CHECK(effects.applyEffect(QStringLiteral("effect:bomb_capacity_up")), "the effect: prefix is accepted");
	CHECK_EQ_INT(effects.players().at(0).maxBombs, bombsBefore + 2, "applyEffect() targets the local player");

	CHECK(!effects.applyEffectToPlayer(1, QStringLiteral("no_such_effect")), "unknown effect ids are refused");
	CHECK(!effects.applyEffectToPlayer(42, QStringLiteral("heal")), "unknown players are refused");

	// heal is capped by max lives; set lives through the mirror path
	QList<int> ids;
	ids << 1 << 2;
	QList<bp::GridPosF> positions;
	positions << bp::GridPosF::centered(bp::GridPos(1, 1)) << bp::GridPosF::centered(bp::GridPos(8, 8));
	QList<int> lives;
	lives << 1 << 2;
	effects.applyRemotePlayers(ids, positions, lives);

	CHECK(effects.applyEffectToPlayer(1, QStringLiteral("heal")), "heal is a known effect");
	CHECK_EQ_INT(effects.players().at(0).lives, 3, "heal restores two lives but never passes the cap");
	CHECK_EQ_INT(effects.players().at(1).lives, 2, "heal does not touch the other player");

	// ghost mode is timed
	CHECK(effects.applyEffectToPlayer(2, QStringLiteral("star_broken")), "star_broken is a known effect");
	CHECK(effects.players().at(1).ghost, "star_broken grants ghost mode");
	CHECK_EQ_INT(effects.players().at(1).lives, 1, "star_broken forces lives down to one");

	for (int i = 0; i < 9 * 60 && effects.isRunning(); ++i) { effects.tick(1.0 / 60.0); }
	CHECK(!effects.players().at(1).ghost, "ghost mode expires after its eight seconds");
}

// ============================================================
// 20. data-driven resources: injected drop table + effect resolver
//     (this is the seam the itemInfo.json loader uses at runtime)
// ============================================================
static void testEngineResourceInjection()
{
	bp::GameConfig config;
	config.enemyCountPerLevel = 0;
	config.playerBombRange = 1;
	config.playerBombFuse = 1.5;
	config.dropChance = 1.0;
	config.rareDropWeights.clear();          // force the engine to need an injected table
	config.droppableItems.clear();

	bp::SingleGameEngine engine(config);
	engine.setRandomSeed(1);
	engine.setMapGenerator(std::make_unique<FixedMapGenerator>(QList<bp::GridPos>{ bp::GridPos(1, 2) }));

	// the effect resolver is consulted before the built-in name -> effect map
	engine.setEffectResolver([](const QString& name) {
		return name == QStringLiteral("mod_shield") ? bp::EffectId::SpeedUp : QString();
	});

	engine.startLevel(1);

	CHECK(engine.applyEffect(QStringLiteral("mod_shield")), "an injected effect alias resolves");
	CHECK(!engine.applyEffect(QStringLiteral("no_such_effect")), "an unknown effect is refused");
	CHECK(engine.applyEffect(bp::EffectId::BombRangeUp), "a raw effect id still works");

	// the injected drop table fully replaces the built-in weights
	QList<QPair<QString, double>> table;
	table.append(qMakePair(QStringLiteral("mod_item"), 1.0));
	engine.setDropTable(table);

	bp::InputState place;
	place.placeBomb = true;
	engine.setInput(place);
	engine.tick(1.0 / 60.0);
	engine.setInput(bp::InputState());

	for (int i = 0; i < 200 && engine.isRunning(); ++i) { engine.tick(1.0 / 60.0); }

	CHECK(engine.board().tile(bp::GridPos(1, 2)) == bp::TileType::Empty, "single player: the brick is gone");
	CHECK_EQ_INT(engine.drops().size(), 1, "single player: the brick dropped exactly one item");
	if (!engine.drops().isEmpty())
	{
		CHECK(engine.drops().first().name == QStringLiteral("mod_item"),
			  "the drop name comes from the injected table");
	}
}

// ============================================================
// 21. the wire format itself (host <-> client)
//     There is no UDP in this test on purpose: the risky part is not the socket,
//     it is the shared struct both sides agree on. If this round trip is wrong,
//     every multiplayer game is wrong, and it is the one thing a single machine
//     can verify without a second PC.
// ============================================================
static void testNetworkProtocolRoundTrip()
{
	GameStateSnapshot out;
	out.playerIds << 1 << 2;
	// (x = col, y = row) continuous grid coordinates
	out.playerPositions << QPointF(3.5, 1.25) << QPointF(11.5, 9.5);
	out.playerLives << 2 << 1;
	out.playerNames << QStringLiteral("P1") << QStringLiteral("P2");
	out.playerRanges << 2 << 1;
	out.playerMaxBombs << 3 << 2;
	out.bombPositions << QPointF(1.5, 1.5);
	out.bombOwners << 1;
	out.bombTimers << 1.25f;
	out.bombFuseTotals << 1.5f;
	out.bombRanges << 2;
	out.dropPositions << QPointF(4.5, 4.5);
	out.dropNames << QStringLiteral("mod_item");
	out.gameRunning = false;
	out.winnerId = 2;

	QByteArray packet;
	QDataStream writer(&packet, QIODevice::WriteOnly);
	writer.setVersion(QDataStream::Qt_6_0);
	writer << out;

	GameStateSnapshot in;
	QDataStream reader(packet);
	reader.setVersion(QDataStream::Qt_6_0);
	reader >> in;

	CHECK_EQ_INT(in.playerIds.size(), 2, "protocol: player ids survive the round trip");
	CHECK_EQ_INT(in.playerPositions.size(), 2, "protocol: player positions survive");
	CHECK_EQ_INT(in.playerNames.size(), 2, "protocol: player names survive");
	CHECK_EQ_INT(in.playerRanges.size(), 2, "protocol: player ranges survive");
	CHECK_EQ_INT(in.bombFuseTotals.size(), 1, "protocol: bomb fuse totals survive");
	CHECK_EQ_INT(in.dropNames.size(), 1, "protocol: drop names survive");
	CHECK_EQ_INT(in.dropPositions.size(), 1, "protocol: drop positions survive");
	CHECK(in.playerNames.at(1) == QStringLiteral("P2"), "protocol: names keep their order");
	CHECK(in.dropNames.at(0) == QStringLiteral("mod_item"), "protocol: drop names keep their value");
	CHECK(!in.gameRunning, "protocol: gameRunning survives");
	CHECK_EQ_INT(in.winnerId, 2, "protocol: winner id survives");

	// the (col, row) convention must round-trip back into GridPosF exactly:
	// x -> col, y -> row (getting this backwards is the classic multiplayer bug)
	const bp::GridPosF back = bp::posFromRowCol(in.playerPositions.at(0).y(), in.playerPositions.at(0).x());
	CHECK_EQ_INT(back.cell.row, 1, "protocol: y becomes the row");
	CHECK_EQ_INT(back.cell.col, 3, "protocol: x becomes the col");
	CHECK_NEAR(back.offsetY, 0.25, 0.0001, "protocol: row offset survives");
	CHECK_NEAR(back.offsetX, 0.5, 0.0001, "protocol: col offset survives");

	// player input has its own round trip
	PlayerInput inputOut;
	inputOut.playerId = 7;
	inputOut.is_moveup = true;
	inputOut.is_placeBomb = true;

	QByteArray inputPacket;
	QDataStream inputWriter(&inputPacket, QIODevice::WriteOnly);
	inputWriter.setVersion(QDataStream::Qt_6_0);
	inputWriter << inputOut;

	PlayerInput inputIn;
	QDataStream inputReader(inputPacket);
	inputReader.setVersion(QDataStream::Qt_6_0);
	inputReader >> inputIn;

	CHECK_EQ_INT(inputIn.playerId, 7, "protocol: input keeps the player id");
	CHECK(inputIn.is_moveup, "protocol: input keeps the pressed key");
	CHECK(inputIn.is_placeBomb, "protocol: input keeps the bomb request");
	CHECK(!inputIn.is_moveleft, "protocol: input keeps the released keys");
}

// ============================================================
// 22. level data: JSON -> LevelDefinition -> board
//
// This is the test that turns "adding a level = adding a JSON file" from a
// claim into a fact: the whole path from a JSON object to a laid-out board is
// exercised here, with no file system involved.
// ============================================================
static bp::LevelDefinition parseLevel(const char* json, QString* error)
{
	QJsonParseError parseError;
	const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json), &parseError);
	CHECK(parseError.error == QJsonParseError::NoError, "level json parses");
	return bp::LevelDefinition::fromJson(doc.object(), error);
}

static void testLevelData()
{
	// ---------- a hand-written layout level ----------
	const char* layoutJson = R"({
		"id": 3, "name": "layout test", "enemyHp": 5, "seed": 77, "coins": 99, "ep": 12,
		"layout": [
			"#############",
			"#P..........#",
			"#.B.........#",
			"#....E......#",
			"#...........#",
			"#...........#",
			"#...........#",
			"#...........#",
			"#...........#",
			"#..........E#",
			"#############"
		]
	})";

	QString error;
	const bp::LevelDefinition def = parseLevel(layoutJson, &error);
	CHECK(error.isEmpty(), "a well-formed layout reports no error");
	CHECK_EQ_INT(def.id, 3, "level id is read from json");
	CHECK_EQ_INT(def.enemyHp, 5, "level enemy hp is read from json");
	CHECK_EQ_INT(static_cast<int>(def.seed), 77, "level seed is read from json");
	CHECK_EQ_INT(def.coins, 99, "level coins are read from json");
	CHECK_EQ_INT(def.ep, 12, "level ep is read from json");
	CHECK(def.hasLayout(), "the layout is recognised");

	bp::JsonMapGenerator generator(def);
	bp::GameBoard board;
	bp::LevelSpec spec;
	spec.brickCount = 999;      // must be ignored: the layout decides the bricks
	spec.enemyCount = 999;      // must be ignored: the 'E' characters decide the enemies
	spec.enemyHp = 1;           // the json value (5) must win
	spec.seed = 1;
	spec.playerStart = bp::GridPos(9, 9);

	const bp::GeneratedMap generated = generator.generate(board, spec);

	CHECK(generated.ok, "layout generation succeeds");
	CHECK(generated.usedLayout, "the layout path was used");
	CHECK(generated.hasPlayerStart, "the generator reports a spawn point");
	CHECK(generated.playerStart == bp::GridPos(1, 1), "the 'P' character decides the spawn point");
	CHECK_EQ_INT(generated.enemies.size(), 2, "two 'E' characters become two enemies");
	CHECK(board.tile(bp::GridPos(0, 0)) == bp::TileType::Wall, "layout '#' becomes a wall");
	CHECK(board.tile(bp::GridPos(2, 2)) == bp::TileType::Brick, "layout 'B' becomes a brick");
	CHECK(board.tile(bp::GridPos(1, 1)) == bp::TileType::Player, "the spawn cell is marked");
	if (!generated.enemies.isEmpty())
	{
		CHECK_EQ_INT(generated.enemies.at(0).hp, 5, "enemy hp comes from the level, not from the spec");
	}

	// ---------- the border is sealed even when the layout forgets it ----------
	bp::LevelDefinition openDef;
	openDef.id = 4;
	openDef.playerStart = bp::GridPos(1, 1);
	for (int r = 0; r < bp::kRows; ++r)
	{
		openDef.layout.append(QString(bp::kCols, QLatin1Char('.')));
	}

	bp::JsonMapGenerator openGenerator(openDef);
	bp::GameBoard openBoard;
	const bp::GeneratedMap openMap = openGenerator.generate(openBoard, bp::LevelSpec());

	CHECK(openMap.ok, "an open-border layout still generates");
	CHECK(openBoard.tile(bp::GridPos(0, 0)) == bp::TileType::Wall, "top-left border is sealed anyway");
	CHECK(openBoard.tile(bp::GridPos(bp::kRows - 1, bp::kCols - 1)) == bp::TileType::Wall,
		  "bottom-right border is sealed anyway");
	CHECK(openBoard.tile(bp::GridPos(1, 1)) == bp::TileType::Player,
		  "the spec spawn point is marked when 'P' is missing");

	// ---------- a structurally broken layout falls back, it does not fail ----------
	bp::LevelDefinition broken;
	broken.id = 5;
	broken.layout.append(QStringLiteral("###"));   // wrong size on purpose

	bp::JsonMapGenerator brokenGenerator(broken);
	bp::GameBoard brokenBoard;
	bp::LevelSpec brokenSpec;
	brokenSpec.brickCount = 10;
	brokenSpec.enemyCount = 1;
	brokenSpec.enemyHp = 2;
	brokenSpec.seed = 4242;

	const bp::GeneratedMap brokenMap = brokenGenerator.generate(brokenBoard, brokenSpec);
	CHECK(brokenMap.ok, "a broken layout still produces a playable map");
	CHECK(!brokenMap.usedLayout, "a broken layout falls back to random generation");

	// ---------- structural errors are reported, never thrown ----------
	const char* badJson = R"({"id":6,"layout":["#####","#####","#####","#####","#####","#####","#####","#####","#####","#####","#####"]})";
	QString badError;
	const bp::LevelDefinition badDef = parseLevel(badJson, &badError);
	CHECK(!badError.isEmpty(), "a wrong column count reports an error");
	CHECK_EQ_INT(badDef.id, 6, "the rest of the definition is still usable");

	const char* unknownJson = R"({"id":7,"layout":["#############","#P....?.....#","#...........#","#...........#","#...........#","#...........#","#...........#","#...........#","#...........#","#...........#","#############"]})";
	QString unknownError;
	const bp::LevelDefinition unknownDef = parseLevel(unknownJson, &unknownError);
	CHECK(!unknownError.isEmpty(), "an unknown layout character reports an error");
	CHECK_EQ_INT(unknownDef.id, 7, "an unknown character does not lose the definition");

	// ---------- procedural levels (no layout) ----------
	const char* proceduralJson = R"({
		"id": 2, "name": "procedural", "bricks": 30, "enemyCount": 2, "enemyHp": 4, "seed": 12345
	})";
	QString proceduralError;
	const bp::LevelDefinition proceduralDef = parseLevel(proceduralJson, &proceduralError);
	CHECK(proceduralError.isEmpty(), "a procedural level has no structural error");

	bp::JsonMapGenerator proceduralGenerator(proceduralDef);
	bp::GameBoard proceduralBoard;
	bp::LevelSpec proceduralSpec;
	proceduralSpec.brickCount = 1;        // the json value (30) must win
	proceduralSpec.enemyCount = 99;
	proceduralSpec.seed = 999;

	const bp::GeneratedMap proceduralMap = proceduralGenerator.generate(proceduralBoard, proceduralSpec);

	CHECK(proceduralMap.ok, "a procedural level generates");
	CHECK(!proceduralMap.usedLayout, "a level without a layout uses the random generator");
	CHECK_EQ_INT(proceduralMap.enemies.size(), 2, "procedural enemy count comes from the json");

	int bricks = 0;
	for (int r = 0; r < bp::kRows; ++r)
	{
		for (int c = 0; c < bp::kCols; ++c)
		{
			if (proceduralBoard.tile(bp::GridPos(r, c)) == bp::TileType::Brick) { ++bricks; }
		}
	}
	CHECK_EQ_INT(bricks, 30, "procedural brick count comes from the json");

	// the json seed makes it reproducible (same seed -> same board)
	bp::JsonMapGenerator proceduralGenerator2(proceduralDef);
	bp::GameBoard proceduralBoard2;
	proceduralGenerator2.generate(proceduralBoard2, proceduralSpec);

	int differing = 0;
	for (int r = 0; r < bp::kRows; ++r)
	{
		for (int c = 0; c < bp::kCols; ++c)
		{
			if (proceduralBoard.tile(bp::GridPos(r, c)) != proceduralBoard2.tile(bp::GridPos(r, c)))
			{
				++differing;
			}
		}
	}
	CHECK_EQ_INT(differing, 0, "the json seed makes the same level reproducible");

	// ---------- engine integration: a layout level actually plays ----------
	bp::GameConfig config;
	config.enemyCountPerLevel = 0;

	bp::SingleGameEngine engine(config);
	engine.setRandomSeed(1);
	engine.setMapGenerator(std::make_unique<bp::JsonMapGenerator>(def));
	engine.startLevel(3);

	CHECK(engine.phase() == bp::Phase::Running, "a data-driven level starts running");
	CHECK_EQ_INT(engine.enemies().size(), 2, "the level's enemies reach the engine");
	CHECK(engine.playerPos().cell == bp::GridPos(1, 1), "the engine puts the player on the 'P' cell");
	CHECK(engine.board().tile(bp::GridPos(2, 2)) == bp::TileType::Brick, "the engine board keeps the layout");

	// a level with no JSON at all still works: nullptr restores the random generator
	bp::SingleGameEngine fallbackEngine(config);
	fallbackEngine.setMapGenerator(nullptr);
	fallbackEngine.startLevel(1);
	CHECK(fallbackEngine.phase() == bp::Phase::Running, "a level without data still starts");

	// ---------- defaults are neutral, not random garbage ----------
	const bp::LevelSpec fallbackSpec;
	const bp::LevelDefinition defaults = bp::LevelDefinition::fromJson(QJsonObject());
	CHECK_EQ_INT(defaults.id, 0, "an empty json object yields a neutral definition");
	CHECK(!defaults.hasLayout(), "an empty json object has no layout");
	CHECK(defaults.toSpec(fallbackSpec).brickCount >= 0, "missing fields fall back to the spec");
}

// ============================================================
// 23. mod contract: version/feature gating + gameplay patches
//
// These are the two pieces that decide whether a mod may load at all and what
// it is allowed to change. Both are pure logic on purpose: "a bad mod must not
// make the game unlaunchable" is a promise that has to be provable in a test,
// not verified by hand with a broken mod on disk.
// ============================================================
static void testModApiContract()
{
	const bp::ModApiDeclaration decl = bp::ModApiDeclaration::builtin();

	CHECK(decl.engineApiVersion == bp::kModApiVersion, "the built-in declaration matches the code constant");
	CHECK(decl.supportsVersion(bp::kModApiVersion), "the current api version is accepted");
	// additive evolution, as a promise we can actually prove: an old manifest keeps working
	CHECK(decl.supportsVersion(QStringLiteral("0.1.0")),
		  "the previous api version 0.1.0 is still accepted (append-only evolution)");
	CHECK(decl.supportedModApiVersions.size() >= 2, "the accepted version list keeps older entries");
	CHECK(decl.supportsFeature(bp::ModFeature::Gameplay), "gameplay capability is open");
	CHECK(decl.supportsFeature(bp::ModFeature::Items), "items capability is open");
	CHECK(decl.supportsFeature(bp::ModFeature::Levels), "levels capability is open");
	CHECK(decl.supportsFeature(bp::ModFeature::Hooks), "hooks (native mods) are open since 0.1.0");
	CHECK(!decl.supportsFeature(bp::ModFeature::Modes), "modes are NOT open yet");
	CHECK(!decl.history.isEmpty(), "the declaration documents its own history");

	// ---------- a valid manifest passes ----------
	bp::ModManifest good;
	good.id = QStringLiteral("good");
	good.apiVersion = bp::kModApiVersion;
	good.features << bp::ModFeature::Gameplay << bp::ModFeature::Levels;
	CHECK(decl.isAcceptable(good), "a manifest inside the contract is accepted");
	CHECK(decl.check(good).isEmpty(), "an accepted manifest has no reason text");

	// ---------- a missing api version is refused ----------
	bp::ModManifest noVersion;
	noVersion.id = QStringLiteral("no_version");
	noVersion.apiVersion.clear();
	CHECK(!decl.isAcceptable(noVersion), "a manifest without apiVersion is refused");
	CHECK(!decl.check(noVersion).isEmpty(), "the refusal comes with a readable reason");

	// ---------- an unsupported version is refused, and the reason lists what IS supported ----------
	bp::ModManifest future;
	future.id = QStringLiteral("future");
	future.apiVersion = QStringLiteral("9.9.9");
	CHECK(!decl.isAcceptable(future), "a future api version is refused");
	CHECK(decl.check(future).contains(decl.supportedVersionsText()),
		  "the refusal names the supported versions");

	// ---------- an unavailable capability is refused ----------
	bp::ModManifest wantsModes;
	wantsModes.id = QStringLiteral("wants_modes");
	wantsModes.apiVersion = bp::kModApiVersion;
	wantsModes.features << bp::ModFeature::Modes;
	CHECK(!decl.isAcceptable(wantsModes), "a manifest needing closed capabilities is refused");
	CHECK(decl.check(wantsModes).contains(bp::ModFeature::Modes), "the refusal names the capability");

	// ---------- an unknown capability is refused (typo protection) ----------
	bp::ModManifest typo;
	typo.id = QStringLiteral("typo");
	typo.apiVersion = bp::kModApiVersion;
	typo.features << QStringLiteral("gamepaly");   // deliberate typo
	CHECK(!decl.isAcceptable(typo), "an unknown capability name is refused");

	// ---------- parsing mod.json ----------
	const char* manifestJson = R"({
		"id": "demo", "name": "Demo", "version": "2.1", "author": "someone",
		"priority": 7, "enabledByDefault": false, "apiVersion": "0.1.0",
		"features": ["levels", "levels", "items"]
	})";
	QJsonParseError parseError;
	const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(manifestJson), &parseError);
	CHECK(parseError.error == QJsonParseError::NoError, "mod.json parses");

	QString error;
	const bp::ModManifest parsed = bp::ModManifest::fromJson(doc.object(), &error);
	CHECK(error.isEmpty(), "parsing a well-formed manifest reports no error");
	CHECK(parsed.id == QStringLiteral("demo"), "manifest id is read");
	CHECK(parsed.version == QStringLiteral("2.1"), "manifest version is read");
	CHECK_EQ_INT(parsed.priority, 7, "manifest priority is read");
	CHECK(!parsed.enabledByDefault, "manifest enabledByDefault is read");
	CHECK(parsed.apiVersion == QStringLiteral("0.1.0"), "manifest apiVersion is read");
	CHECK_EQ_INT(parsed.features.size(), 2, "duplicate capabilities are collapsed");
	CHECK(parsed.features.contains(bp::ModFeature::Items), "capabilities keep their order-independent meaning");

	// ---------- the declaration itself can come from a file ----------
	const char* declJson = R"({
		"engineApiVersion": "0.4.0",
		"supportedModApiVersions": ["0.2.0", "0.3.0", "0.4.0"],
		"features": { "gameplay": true, "items": false, "levels": false, "hooks": true },
		"history": [ { "apiVersion": "0.4.0", "note": "test" } ]
	})";
	const QJsonDocument declDoc = QJsonDocument::fromJson(QByteArray(declJson), &parseError);
	QString declError;
	const bp::ModApiDeclaration fromFile = bp::ModApiDeclaration::fromJson(declDoc.object(), &declError);
	CHECK(declError.isEmpty(), "a well-formed declaration reports no error");
	CHECK(fromFile.engineApiVersion == QStringLiteral("0.4.0"), "engine api version comes from the file");
	CHECK(fromFile.supportsVersion(QStringLiteral("0.2.0")), "the file decides which mod versions are accepted");
	CHECK(!fromFile.supportsVersion(QStringLiteral("0.1.0")), "a version outside the list is refused");
	CHECK(fromFile.supportsFeature(bp::ModFeature::Hooks), "the file can open capabilities");
	CHECK(!fromFile.supportsFeature(bp::ModFeature::Items), "the file can close capabilities");
	CHECK_EQ_INT(fromFile.history.size(), 1, "the file carries its own history");

	// a broken (empty) version list falls back to the built-in one instead of banning everything
	const QJsonDocument brokenDecl = QJsonDocument::fromJson(QByteArray(R"({"supportedModApiVersions": []})"));
	QString brokenError;
	const bp::ModApiDeclaration fallback = bp::ModApiDeclaration::fromJson(brokenDecl.object(), &brokenError);
	CHECK(!fallback.supportedModApiVersions.isEmpty(), "an empty version list falls back to the built-in one");
	CHECK(!brokenError.isEmpty(), "the fallback is reported instead of being silent");
}

static void testGameplayPatches()
{
	// ---------- the whitelist itself ----------
	const QStringList keys = bp::gameplayPatchKeys();
	CHECK(keys.contains(QStringLiteral("playerSpeed")), "playerSpeed is patchable");
	CHECK(keys.contains(QStringLiteral("dropChance")), "dropChance is patchable");
	CHECK(keys.contains(QStringLiteral("warningSeconds")), "warningSeconds is patchable");
	CHECK(!bp::isGameplayPatchKey(QStringLiteral("playerSpeeed")), "a typo is not a patchable key");
	CHECK(bp::isGameplayFlagKey(QStringLiteral("multiSelfDamage")), "multiSelfDamage is a flag");
	CHECK(!bp::isGameplayFlagKey(QStringLiteral("playerSpeed")), "playerSpeed is not a flag");

	// ---------- applying patches ----------
	bp::GameConfig config;
	const double baseSpeed = config.playerSpeed;

	QList<bp::GameplayPatch> patches;
	patches << bp::GameplayPatch(QStringLiteral("playerSpeed"), 6.5, QStringLiteral("test/gameplay.json"))
			<< bp::GameplayPatch(QStringLiteral("dropChance"), 0.75, QStringLiteral("test/gameplay.json"))
			<< bp::GameplayPatch(QStringLiteral("playerBombRange"), 3.0, QStringLiteral("test/gameplay.json"))
			<< bp::GameplayPatch(QStringLiteral("multiSelfDamage"), 1.0, QStringLiteral("test/gameplay.json"));

	QStringList problems;
	const int applied = bp::applyGameplayPatches(config, patches, &problems);

	CHECK_EQ_INT(applied, 4, "all four patches applied");
	CHECK(problems.isEmpty(), "a clean patch set reports no problem");
	CHECK_NEAR(config.playerSpeed, 6.5, 0.0001, "playerSpeed was patched");
	CHECK_NEAR(config.dropChance, 0.75, 0.0001, "dropChance was patched");
	CHECK_EQ_INT(config.playerBombRange, 3, "an int field accepts a double patch");
	CHECK(config.multiSelfDamage, "a flag field accepts 1.0");
	CHECK(baseSpeed != config.playerSpeed, "the default really changed");

	// ---------- an unknown key is refused AND reported (never silent) ----------
	bp::GameConfig other;
	const double otherSpeed = other.playerSpeed;
	QStringList otherProblems;
	QList<bp::GameplayPatch> badPatches;
	badPatches << bp::GameplayPatch(QStringLiteral("playerSpeeed"), 99.0, QStringLiteral("typo/gameplay.json"))
			   << bp::GameplayPatch(QStringLiteral("playerSpeed"), 5.0, QStringLiteral("typo/gameplay.json"));

	const int appliedBad = bp::applyGameplayPatches(other, badPatches, &otherProblems);
	CHECK_EQ_INT(appliedBad, 1, "only the known key is applied");
	CHECK_EQ_INT(otherProblems.size(), 1, "the unknown key is reported");
	CHECK(otherProblems.first().contains(QStringLiteral("playerSpeeed")), "the report names the offending key");
	CHECK(otherProblems.first().contains(QStringLiteral("typo/gameplay.json")), "the report names the source file");
	CHECK_NEAR(other.playerSpeed, 5.0, 0.0001, "the good patch still applied");
	CHECK(otherSpeed != 5.0, "the default was not 5.0, so the change is visible");

	// ---------- single-key API ----------
	bp::GameConfig one;
	CHECK(bp::applyGameplayPatch(one, QStringLiteral("enemySpeed"), 9.0), "single-key apply works");
	CHECK_NEAR(one.enemySpeed, 9.0, 0.0001, "single-key apply changed the field");
	CHECK(!bp::applyGameplayPatch(one, QStringLiteral("enemySpeeed"), 9.0), "single-key apply refuses a typo");
	CHECK_NEAR(one.enemySpeed, 9.0, 0.0001, "a refused key changes nothing");

	// ---------- later patches win (this is what priority ordering relies on) ----------
	bp::GameConfig layered;
	QList<bp::GameplayPatch> layers;
	layers << bp::GameplayPatch(QStringLiteral("brickBaseCount"), 10.0, QStringLiteral("low-priority"))
		   << bp::GameplayPatch(QStringLiteral("brickBaseCount"), 40.0, QStringLiteral("high-priority"));
	bp::applyGameplayPatches(layered, layers, nullptr);
	CHECK_EQ_INT(layered.brickBaseCount, 40, "the later patch wins (priority order is meaningful)");

	// ---------- an empty list is a no-op, not an error ----------
	bp::GameConfig untouched;
	const int none = bp::applyGameplayPatches(untouched, QList<bp::GameplayPatch>(), nullptr);
	CHECK_EQ_INT(none, 0, "an empty patch list applies nothing");
	CHECK_NEAR(untouched.playerSpeed, bp::GameConfig().playerSpeed, 0.0001,
			   "an empty patch list leaves the defaults alone");
}

// ============================================================
// 24. UI tokens: the "@name" template layer that lets a mod reskin the UI
//
// Pure string/JSON logic on purpose: "a mod can change the colours without
// touching layout" is a promise, and this is the part of it that can be proven
// without a QApplication.
// ============================================================
static void testUiTokens()
{
	QHash<QString, QString> tokens = UiTokens::builtin();

	CHECK(tokens.contains(QStringLiteral("primary")), "builtin tokens define the primary colour");
	CHECK(tokens.contains(QStringLiteral("radius")), "builtin tokens define the corner radius");
	CHECK(tokens.contains(QStringLiteral("hudBg")), "builtin tokens define the in-game HUD colour");
	CHECK(!UiTokens::names(tokens).isEmpty(), "token names can be listed");

	// ---------- merging a json object (what ui/tokens.json and a mod's config/ui.json do) ----------
	QJsonParseError parseError;
	const QJsonDocument doc = QJsonDocument::fromJson(
		QByteArray(R"({"primary": "#FF0000", "radius": "2px", "badType": 42, "_comment": "ignored"})"),
		&parseError);
	CHECK(parseError.error == QJsonParseError::NoError, "a token file parses");

	QStringList problems;
	UiTokens::merge(tokens, doc.object(), QStringLiteral("test/tokens.json"), &problems);

	CHECK(tokens.value(QStringLiteral("primary")) == QStringLiteral("#FF0000"), "a token can be overridden");
	CHECK(tokens.value(QStringLiteral("radius")) == QStringLiteral("2px"), "a shape token can be overridden");
	CHECK(!tokens.contains(QStringLiteral("badType")), "a non-string token is not merged");
	CHECK(!tokens.contains(QStringLiteral("_comment")), "comment keys are ignored");
	CHECK_EQ_INT(problems.size(), 1, "the non-string token is reported");
	CHECK(problems.first().contains(QStringLiteral("badType")), "the report names the offending token");
	CHECK(UiTokens::builtin().value(QStringLiteral("primary")) != QStringLiteral("#FF0000"),
		  "merging does not mutate the builtin table");

	// ---------- substitution ----------
	QHash<QString, QString> small;
	small.insert(QStringLiteral("a"), QStringLiteral("AAA"));
	small.insert(QStringLiteral("b"), QStringLiteral("2px"));

	QStringList missing;
	const QString filled = UiTokens::substitute(
		QStringLiteral("#x { color: @a; radius: @b; }"), small, &missing);
	CHECK(filled == QStringLiteral("#x { color: AAA; radius: 2px; }"), "@name is replaced");
	CHECK(missing.isEmpty(), "a complete substitution reports nothing missing");

	QStringList missing2;
	const QString partial = UiTokens::substitute(
		QStringLiteral("color: @a; bogus: @nope; tail: @a"), small, &missing2);
	CHECK(partial.contains(QStringLiteral("AAA")), "known tokens are still replaced later in the text");
	CHECK(partial.contains(QStringLiteral("@nope")), "an unknown token is kept verbatim");
	CHECK_EQ_INT(missing2.size(), 1, "an unknown token is reported once");
	CHECK(missing2.first() == QStringLiteral("nope"), "the report names the unknown token");

	// ---------- corners ----------
	QStringList missing3;
	CHECK(UiTokens::substitute(QStringLiteral("@a@@b"), small, &missing3) == QStringLiteral("AAA@2px"),
		  "an isolated @ is kept and neighbours still resolve");
	CHECK(UiTokens::substitute(QString(), small, nullptr).isEmpty(), "an empty sheet stays empty");
	CHECK(UiTokens::substitute(QStringLiteral("no tokens here"), small, nullptr)
		  == QStringLiteral("no tokens here"), "text without tokens is untouched");

	QStringList missing4;
	CHECK(UiTokens::substitute(QStringLiteral("border-radius:@b}"), small, &missing4)
		  == QStringLiteral("border-radius:2px}"), "a token can be followed immediately by punctuation");

	// Token names are ASCII-only: Chinese prose in a comment must not be mistaken for a token
	// (a CJK "name" is left verbatim and is NOT reported; an ASCII name is a token wherever it appears)
	QStringList missing5;
	const QString withComment = QStringLiteral("/* 只写 @令牌 这类名字 */\n#x { color: @a; }");
	const QString substituted = UiTokens::substitute(withComment, small, &missing5);
	CHECK(substituted.contains(QStringLiteral("AAA")), "a real token next to a comment still resolves");
	CHECK(substituted.contains(QStringLiteral("@令牌")), "the CJK name in the comment is left verbatim");
	CHECK_EQ_INT(missing5.size(), 0, "a CJK name is not treated as a token at all");
}

// ============================================================
// 23. crash protection and safe mode (LaunchGuard)
//
// Pure logic + a temp directory: the state machine must not need "let a mod really
// crash" to be verified. Every case first writes a launch record by hand and then
// asks LaunchGuard to judge it, so "two crashes in a row -> safe mode", "reaching the
// home screen resets the counter" and "a broken file is treated as normal" are all
// deterministic assertions.
// ============================================================
static QJsonObject readLaunchJson(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return QJsonObject(); }
	return QJsonDocument::fromJson(file.readAll()).object();
}

// Simulate "the record the previous process left behind"
static void writeLaunchState(const QString& path, bool running, int failures, bool reachedHome,
							 const QString& stage, const QStringList& nativeMods)
{
	QJsonObject obj;
	obj.insert(QStringLiteral("schema"), 1);
	obj.insert(QStringLiteral("appVersion"), QStringLiteral("0.1.0"));
	obj.insert(QStringLiteral("running"), running);
	obj.insert(QStringLiteral("lastExit"), running ? QStringLiteral("unknown") : QStringLiteral("clean"));
	obj.insert(QStringLiteral("consecutiveFailures"), failures);
	obj.insert(QStringLiteral("stage"), stage);
	obj.insert(QStringLiteral("startedAt"), QStringLiteral("2026-01-01T00:00:00"));
	obj.insert(QStringLiteral("nativeMods"), QJsonArray::fromStringList(nativeMods));
	obj.insert(QStringLiteral("loadedOk"), QJsonArray());
	obj.insert(QStringLiteral("reachedHome"), reachedHome);
	obj.insert(QStringLiteral("safeModeRequested"), false);
	obj.insert(QStringLiteral("notes"), QString());
	FileManager::instance().writeJson(path, QJsonDocument(obj));
}

static void testLaunchGuard()
{
	QTemporaryDir dir;
	CHECK(dir.isValid(), "a temp dir is available for the crash-protection test");
	if (!dir.isValid()) { return; }

	// Logs and the record go under the temp root: the test never touches real saves
	DirectoryLayout::instance().resolve(dir.path());
	QDir().mkpath(dir.path() + QStringLiteral("/Data"));   // the app creates this at startup

	LaunchGuard& guard = LaunchGuard::instance();
	guard.resetForTest();

	CHECK(LaunchGuard::defaultStatePath()
			== DirectoryLayout::instance().dataDir() + QStringLiteral("/launch_state.json"),
		  "default record path is Data/launch_state.json");

	const QString path = dir.path() + QStringLiteral("/Data/launch_state.json");

	// ---------- 1) missing file = first launch ----------
	CHECK(guard.startup(false, path) == LaunchGuard::State::Clean, "missing record -> Clean");
	CHECK_EQ_INT(guard.consecutiveFailures(), 0, "missing record -> failures 0");
	CHECK(QFile::exists(path), "the record is written immediately (not on exit)");
	{
		const QJsonObject record = readLaunchJson(path);
		CHECK_EQ_INT(record.value(QStringLiteral("schema")).toInt(), 1, "record carries a schema version");
		CHECK(record.value(QStringLiteral("running")).toBool() == true, "running=true while starting");
		CHECK(record.value(QStringLiteral("stage")).toString() == QStringLiteral("startup"),
			  "stage is startup right after startup()");
		CHECK(record.value(QStringLiteral("reachedHome")).toBool() == false, "home not reached yet");
	}
	// an atomic write leaves no temp file behind
	CHECK_EQ_INT(QDir(dir.path() + QStringLiteral("/Data")).entryList(QDir::Files).size(), 1,
				 "atomic write leaves exactly one file");

	// ---------- 2) a clean exit keeps the next start Clean ----------
	guard.markCleanExit();
	{
		const QJsonObject record = readLaunchJson(path);
		CHECK(record.value(QStringLiteral("running")).toBool() == false, "clean exit -> running=false");
		CHECK(record.value(QStringLiteral("lastExit")).toString() == QStringLiteral("clean"),
			  "clean exit writes lastExit=clean");
		CHECK(record.value(QStringLiteral("stage")).toString() == QStringLiteral("exiting"),
			  "clean exit stage is exiting");
	}
	CHECK(guard.startup(false, path) == LaunchGuard::State::Clean, "clean exit -> next start is Clean");
	CHECK(guard.lastExit() == QStringLiteral("clean"), "the previous conclusion reads clean");

	// ---------- 3) first abnormal end = Suspected (still loads, prompts once) ----------
	writeLaunchState(path, true, 0, false, QStringLiteral("loading_native:hello_native"),
					 QStringList() << QStringLiteral("hello_native"));
	CHECK(guard.startup(false, path) == LaunchGuard::State::Suspected, "first abnormal end -> Suspected");
	CHECK_EQ_INT(guard.consecutiveFailures(), 1, "first abnormal end -> failures 1");
	CHECK(guard.blockNativeMods() == false, "Suspected still loads native mods");
	CHECK(guard.lastExit() == QStringLiteral("crash"), "the previous conclusion reads crash");
	CHECK(guard.lastStage() == QStringLiteral("loading_native:hello_native"),
		  "the previous stage is preserved (that is the crash clue)");
	CHECK(guard.lastNativeMods() == (QStringList() << QStringLiteral("hello_native")),
		  "the native mods of the previous run are preserved");
	CHECK(guard.notes().contains(QStringLiteral("hello_native")), "the summary names the suspect mod");
	CHECK(guard.consumeCrashPrompt(), "Suspected shows the prompt once");
	CHECK(!guard.consumeCrashPrompt(), "the prompt is not shown twice");

	// load-time bookkeeping: the file points at the mod being loaded
	guard.markNativeModLoading(QStringLiteral("hello_native"));
	{
		const QJsonObject record = readLaunchJson(path);
		CHECK(record.value(QStringLiteral("stage")).toString()
				== QStringLiteral("loading_native:hello_native"),
			  "stage=loading_native:<id> is written before LoadLibrary");
		CHECK(record.value(QStringLiteral("nativeMods")).toArray().contains(QStringLiteral("hello_native")),
			  "the planned native mod list is in the file too");
	}
	guard.markNativeModLoaded(QStringLiteral("hello_native"));
	{
		const QJsonObject record = readLaunchJson(path);
		CHECK(record.value(QStringLiteral("loadedOk")).toArray().contains(QStringLiteral("hello_native")),
			  "a mod that finished init is recorded in loadedOk");
	}

	// ---------- 4) reaching home is the health signal: counter resets ----------
	guard.markReachedHome();
	{
		const QJsonObject record = readLaunchJson(path);
		CHECK_EQ_INT(record.value(QStringLiteral("consecutiveFailures")).toInt(), 0,
					 "reaching home resets the failure counter");
		CHECK(record.value(QStringLiteral("reachedHome")).toBool() == true, "reaching home is recorded");
		CHECK(record.value(QStringLiteral("stage")).toString() == QStringLiteral("running"),
			  "stage is running once home was reached");
	}
	// killed after reaching home (no clean exit) must not blame the mods
	CHECK(guard.startup(false, path) == LaunchGuard::State::Clean,
		  "reached home + killed later still counts as Clean");
	CHECK_EQ_INT(guard.consecutiveFailures(), 0, "a run that reached home does not accumulate");

	// ---------- 5) a second abnormal end in a row = automatic safe mode ----------
	writeLaunchState(path, true, 1, false, QStringLiteral("running"),
					 QStringList() << QStringLiteral("hello_native"));
	CHECK(guard.startup(false, path) == LaunchGuard::State::SafeMode, "two in a row -> SafeMode");
	CHECK_EQ_INT(guard.consecutiveFailures(), 2, "SafeMode reports failures 2");
	CHECK(guard.blockNativeMods() == true, "SafeMode blocks every native mod");
	CHECK(!guard.lastExitText().isEmpty(), "SafeMode has a sentence for the player");

	writeLaunchState(path, true, 2, false, QStringLiteral("bootstrap"), QStringList());
	CHECK(guard.startup(false, path) == LaunchGuard::State::SafeMode, "a third crash is still SafeMode");

	// the counter must not grow without bound (no absurd numbers on screen)
	writeLaunchState(path, true, 9, false, QStringLiteral("bootstrap"), QStringList());
	CHECK(guard.startup(false, path) == LaunchGuard::State::SafeMode, "a saturated counter is SafeMode");
	CHECK_EQ_INT(guard.consecutiveFailures(), 9, "the counter stops at 9");

	// ---------- 6) manual safe mode: this run only ----------
	writeLaunchState(path, false, 0, true, QStringLiteral("exiting"), QStringList());
	CHECK(guard.startup(true, path) == LaunchGuard::State::SafeMode, "--safe-mode forces SafeMode once");
	CHECK(guard.manualSafeMode() == true, "a manual safe mode is remembered as manual");
	CHECK(guard.blockNativeMods() == true, "manual safe mode blocks native mods too");

	// ---------- 7) broken file / unknown schema: always Clean, never block startup ----------
	{
		QFile broken(path);
		CHECK(broken.open(QIODevice::WriteOnly | QIODevice::Truncate), "can write a broken record");
		broken.write("{\"schema\": 1, \"running\": tr");   // truncated JSON
		broken.close();
	}
	CHECK(guard.startup(false, path) == LaunchGuard::State::Clean, "broken JSON -> Clean");
	{
		QJsonObject obj;
		obj.insert(QStringLiteral("schema"), 99);
		obj.insert(QStringLiteral("running"), true);
		obj.insert(QStringLiteral("reachedHome"), false);
		obj.insert(QStringLiteral("consecutiveFailures"), 5);
		FileManager::instance().writeJson(path, QJsonDocument(obj));
	}
	CHECK(guard.startup(false, path) == LaunchGuard::State::Clean, "unknown schema -> Clean");
	CHECK_EQ_INT(guard.consecutiveFailures(), 0, "unknown schema does not accumulate failures");

	// ---------- 8) a record left by a clean exit never accumulates ----------
	writeLaunchState(path, false, 5, false, QStringLiteral("exiting"), QStringList());
	CHECK(guard.startup(false, path) == LaunchGuard::State::Clean, "previous clean exit -> Clean");
	CHECK_EQ_INT(guard.consecutiveFailures(), 0, "previous clean exit resets the counter");

	// ---------- 9) the bootstrap stage is recorded too ----------
	guard.markStage(QStringLiteral("bootstrap"));
	CHECK(readLaunchJson(path).value(QStringLiteral("stage")).toString() == QStringLiteral("bootstrap"),
		  "the bootstrap stage is recorded");

	guard.resetForTest();
}

// ============================================================
// 24. the frozen part of the native-mod C ABI
//
// These assertions are not "testing a feature": they nail down what has already been
// promised to mod authors - struct offsets, event numbers, the tail of the function
// tables. Change one of them without bumping the ABI version and this group goes red.
// (bp_mod_api.h carries the same offsets as compile-time static_asserts.)
// ============================================================
static void testNativeAbiContract()
{
	// ---------- versions ----------
	CHECK(QString::fromLatin1(BP_MOD_API_VERSION) == QStringLiteral("0.1.2"),
		  "the interface version is 0.1.2 (0.1.1 -> 0.1.2 promoted the L3 slots, layout unchanged)");
	CHECK_EQ_INT(BP_MOD_ABI_VERSION, 1, "the ABI version is still 1 (append + slot promotion only)");
	CHECK(BP_BUILD_ID == static_cast<uint64_t>(BP_MOD_ABI_VERSION) * 1000003ull
						 + static_cast<uint64_t>(sizeof(void*)),
		  "BP_BUILD_ID = ABI version + pointer width (not sizeof: that would make every append breaking)");

	// ---------- layout: concrete numbers are only pinned on 64-bit ----------
	if (sizeof(void*) == 8)
	{
		CHECK_EQ_INT(sizeof(BpStr), 16, "BpStr is 16 bytes");
		CHECK_EQ_INT(offsetof(BpStr, len), 8, "BpStr.len sits at offset 8");
		CHECK_EQ_INT(offsetof(BpModInfo, buildId), 24, "BpModInfo.buildId offset unchanged");
		CHECK_EQ_INT(offsetof(BpModInfo, id), 32, "BpModInfo.id offset unchanged");
		CHECK_EQ_INT(offsetof(BpModInfo, modDir), 80, "BpModInfo.modDir offset unchanged");
		CHECK_EQ_INT(sizeof(BpModInfo), 96, "BpModInfo size unchanged");
		CHECK_EQ_INT(offsetof(BpGameEvent, payload), 8, "BpGameEvent.payload offset unchanged");
		CHECK_EQ_INT(sizeof(BpGameEvent), 16, "BpGameEvent size unchanged");
		CHECK_EQ_INT(offsetof(BpModApi, init), 8, "mod table: init is the second entry");
		CHECK_EQ_INT(offsetof(BpModApi, on_tick), 24, "mod table: on_tick offset unchanged");
		CHECK_EQ_INT(offsetof(BpHostApi, has_feature), 8, "host table: has_feature offset unchanged");
		CHECK_EQ_INT(offsetof(BpHostApi, subscribe), 112, "host table: subscribe offset unchanged");
		CHECK_EQ_INT(offsetof(BpHostApi, rng_range), 208, "host table: rng_range offset unchanged");
		CHECK_EQ_INT(offsetof(BpHostApi, reserved_win), 240, "host table: reserved slots unchanged");
		CHECK_EQ_INT(offsetof(BpBombInfo, remainingSeconds), 24,
					 "BpBombInfo puts its doubles on an 8-byte boundary");
		CHECK_EQ_INT(sizeof(BpBombInfo), 40, "BpBombInfo is 40 bytes");
	}

	// ---------- append-only: the 0.1.1 entries must sit after the 0.1.0 tail ----------
	CHECK(offsetof(BpHostApi, level_index) > offsetof(BpHostApi, reserved_win),
		  "new functions only ever go after the 0.1.0 table tail");
	CHECK(offsetof(BpHostApi, bombs_snapshot) > offsetof(BpHostApi, player_count),
		  "append order is fixed: level/phase/step/player_count, then bombs_snapshot");
	CHECK(offsetof(BpHostApi, set_player_start) == sizeof(BpHostApi) - sizeof(void*),
		  "set_player_start is the last entry of the host table");

	// ---------- 0.1.2: the L3 slots were promoted in place (layout identical) ----------
	if (sizeof(void*) == 8)
	{
		CHECK_EQ_INT(offsetof(BpHostApi, register_mode), 216,
					 "register_mode took the old reserved_mode slot (offset unchanged)");
	}
	CHECK(offsetof(BpHostApi, register_mode) < offsetof(BpHostApi, level_index),
		  "the promoted slot stays before the 0.1.1 additions");
	CHECK_EQ_INT(offsetof(BpModApi, on_generate_map), offsetof(BpModApi, on_tick) + sizeof(void*),
				 "on_generate_map took the first reserved mod slot");
	CHECK_EQ_INT(offsetof(BpModApi, on_check_win), offsetof(BpModApi, on_enemy_think) + sizeof(void*),
				 "the three hooks are in the order mapgen / ai / win");
	CHECK(offsetof(BpModApi, on_mode_stop) > offsetof(BpModApi, on_mode_start),
		  "on_mode_stop is the trailing addition");

	// ---------- 0.1.2 new structs are frozen from now on ----------
	if (sizeof(void*) == 8)
	{
		CHECK_EQ_INT(offsetof(BpModeDesc, flags), 76, "BpModeDesc.flags offset");
		CHECK_EQ_INT(sizeof(BpModeDesc), 104, "BpModeDesc size");
		CHECK_EQ_INT(sizeof(BpMapGenRequest), 28, "BpMapGenRequest size");
		CHECK_EQ_INT(sizeof(BpMapGenResult), 16, "BpMapGenResult size");
		CHECK_EQ_INT(sizeof(BpEnemyThinkRequest), 56, "BpEnemyThinkRequest size");
		CHECK_EQ_INT(sizeof(BpEnemyThinkResult), 16, "BpEnemyThinkResult size");
		CHECK_EQ_INT(sizeof(BpWinResult), 32, "BpWinResult size");
	}
	CHECK_EQ_INT(BP_MODE_KIND_HOOKS, 0, "mode kind HOOKS = 0");
	CHECK_EQ_INT(BP_MODE_KIND_OWN_ENGINE, 1, "mode kind OWN_ENGINE = 1 (declared, not implemented)");
	CHECK_EQ_INT(BP_MODE_FLAG_TAKE_OVER_WIN, 1 << 5, "mode flags are a stable bitfield");

	// ---------- event numbers: old ones frozen, new ones appended ----------
	CHECK_EQ_INT(BP_EVENT_ANY, 0, "event 0 is still ANY");
	CHECK_EQ_INT(BP_EVENT_LEVEL_START, 1, "event 1 unchanged");
	CHECK_EQ_INT(BP_EVENT_BOMB_EXPLODED, 3, "event 3 unchanged");
	CHECK_EQ_INT(BP_EVENT_LEVEL_WON, 9, "event 9 unchanged");
	CHECK_EQ_INT(BP_EVENT_LEVEL_LOST, 10, "event 10 unchanged");
	CHECK_EQ_INT(BP_EVENT_ENEMY_DAMAGED, 11, "0.1.1 appended ENEMY_DAMAGED = 11");
	CHECK_EQ_INT(BP_EVENT_ITEM_USED, 12, "0.1.1 appended ITEM_USED = 12");

	// ---------- every new payload is "old struct + trailing fields" ----------
	CHECK_EQ_INT(offsetof(BpEventDrop, row), offsetof(BpEventEntity, row),
				 "BpEventDrop aligns its first fields with BpEventEntity (old readers still work)");
	CHECK_EQ_INT(offsetof(BpEventDrop, col), offsetof(BpEventEntity, col),
				 "BpEventDrop.col aligns as well");
	CHECK(offsetof(BpEventDrop, itemName) > offsetof(BpEventDrop, col),
		  "the item name is a trailing field");
	CHECK_EQ_INT(offsetof(BpEventLevel, level),
				 offsetof(BpEventEntity, col) + static_cast<int>(sizeof(int32_t)),
				 "BpEventLevel.level follows the base four fields");
	CHECK(offsetof(BpEventExplosion, range) > offsetof(BpEventExplosion, destroyedBricks),
		  "the explosion range is a trailing field");

	// ---------- enums ----------
	CHECK_EQ_INT(BP_TILE_EMPTY, 0, "tile 0 unchanged");
	CHECK_EQ_INT(BP_TILE_BRICK, 2, "tile 2 unchanged");
	CHECK_EQ_INT(BP_TILE_EXPLOSION, 3, "0.1.1 appended explosion tile = 3");
	CHECK_EQ_INT(BP_PHASE_READY, 0, "phase 0 = Ready");
	CHECK_EQ_INT(BP_PHASE_DEFEAT, 4, "phase 4 = Defeat");

	// ---------- error codes ----------
	CHECK_EQ_INT(BP_OK, 0, "success is 0");
	CHECK_EQ_INT(BP_ERR_REENTRANT, -6, "the reentrancy code is unchanged (published values are frozen)");
	CHECK_EQ_INT(BP_ERR_DISABLED, -7, "the disabled code is unchanged");
}

// ============================================================
// 25. mod hooks and write requests (the L3 engine seams)
//
// These are the engine-side seams a native mod drives. Two things must hold:
//   1. every write request is a *request*: out-of-range / on a wall / after the level
//      ended must be rejected and change nothing;
//   2. the hooks can decide a step (which way an enemy goes, who won) but can never
//      bypass the engine's collision/validation rules.
// ============================================================
static void testModHooksAndWriteRequests()
{
	bp::GameConfig config;
	config.enemyCountPerLevel = 0;      // this group spawns its own enemies
	config.brickBaseCount = 0;
	config.brickPerLevel = 0;

	// ---------- 1) spawn validation + stable ids ----------
	bp::SingleGameEngine engine(config);
	engine.setMapGenerator(std::make_unique<FixedMapGenerator>());
	engine.startLevel(1);

	CHECK(!engine.requestSpawnEnemy(bp::GridPos(0, 0), 3), "spawning on a border wall is rejected");
	CHECK(!engine.requestSpawnEnemy(bp::GridPos(-1, 5), 3), "out of bounds spawn is rejected");
	CHECK(engine.requestSpawnEnemy(bp::GridPos(3, 3), 2), "a legal spawn is accepted");
	CHECK_EQ_INT(engine.enemies().size(), 1, "the enemy is in the list");

	const int enemyId = engine.enemies().first().id;
	CHECK(enemyId > 0, "the spawned enemy got a stable id");
	CHECK(engine.findEnemyById(enemyId) != nullptr, "findEnemyById finds it");
	CHECK(engine.findEnemyById(enemyId + 1000) == nullptr, "an unknown id finds nothing");

	engine.tick(1.0 / 60.0);
	CHECK_EQ_INT(engine.enemies().first().id, enemyId, "the id survives a tick");

	// ---------- 2) damage / remove ----------
	CHECK(!engine.requestDamageEntity(999999, 1), "damaging an unknown entity is rejected");
	CHECK(!engine.requestDamageEntity(enemyId, 0), "a zero damage request is rejected");
	CHECK(engine.requestDamageEntity(enemyId, 1), "damaging a known entity is accepted");
	CHECK_EQ_INT(engine.enemies().first().hp, 1, "hp dropped by the requested amount");
	engine.tick(1.0 / 60.0);
	CHECK(engine.snapshot().eventCount(bp::GameEventType::EnemyDamaged) > 0,
		  "the damage emitted an EnemyDamaged event");

	CHECK(engine.requestRemoveEntity(enemyId), "removing an enemy is accepted");
	CHECK_EQ_INT(engine.enemies().size(), 0, "the enemy is gone");
	CHECK(!engine.requestRemoveEntity(enemyId), "removing it twice is rejected");

	// ---------- 3) drops keep the requested item name ----------
	CHECK(engine.requestSpawnDrop(bp::GridPos(2, 2), QStringLiteral("生命药水")),
		  "a drop request with a name is accepted");
	CHECK_EQ_INT(engine.drops().size(), 1, "the drop exists");
	CHECK(engine.drops().first().name == QStringLiteral("生命药水"),
		  "the drop keeps the requested item name");
	CHECK(!engine.requestSpawnDrop(bp::GridPos(2, 2), QString()),
		  "an empty item name is rejected");
	CHECK(!engine.requestSpawnDrop(bp::GridPos(0, 0), QStringLiteral("x")) ||
		  engine.board().tile(bp::GridPos(0, 0)) == bp::TileType::Wall,
		  "a drop on the border is not silently accepted");

	// ---------- 4) set_var_* uses the same whitelist as gameplay.json ----------
	const int oldRange = engine.config().playerBombRange;
	CHECK(engine.requestSetConfigNumber(QStringLiteral("playerBombRange"), 5.0),
		  "a whitelisted value is applied");
	CHECK_EQ_INT(engine.config().playerBombRange, 5, "the value really changed");
	CHECK(!engine.requestSetConfigNumber(QStringLiteral("noSuchKey"), 1.0),
		  "an unknown key is rejected");
	CHECK_EQ_INT(engine.config().playerBombRange, 5, "a rejected key changes nothing");
	engine.requestSetConfigNumber(QStringLiteral("playerBombRange"), oldRange);

	// ---------- 5) enemy AI takeover ----------
	bp::SingleGameEngine aiEngine(config);
	aiEngine.setMapGenerator(std::make_unique<FixedMapGenerator>());
	aiEngine.startLevel(1);
	aiEngine.requestSpawnEnemy(bp::GridPos(5, 5), 5);

	QList<int> askedIds;
	aiEngine.setEnemyThinkHook([&askedIds](const bp::Enemy& enemy, const bp::FrameContext&,
										  int& direction, bool& placeBomb)
	{
		askedIds.append(enemy.id);
		direction = 2;        // always walk left
		placeBomb = false;
		return 1;             // "I decide this step"
	});
	CHECK(aiEngine.enemiesTakenOver(), "the engine knows the AI was taken over");

	const double startCol = aiEngine.enemies().first().pos.col();
	for (int i = 0; i < 40; ++i) { aiEngine.tick(1.0 / 60.0); }

	CHECK(!askedIds.isEmpty(), "the hook was asked at least once");
	CHECK_EQ_INT(askedIds.first(), aiEngine.enemies().first().id, "the hook got the stable id");
	CHECK(aiEngine.enemies().first().pos.col() < startCol, "the enemy moved left as told");
	CHECK(aiEngine.enemies().first().pos.cell.col >= 1, "collision still keeps it inside the board");
	CHECK(aiEngine.enemies().first().pos.cell.row == 5, "it never left its row");

	// ---------- 6) win condition takeover ----------
	bp::SingleGameEngine winEngine(config);
	winEngine.setMapGenerator(std::make_unique<FixedMapGenerator>());
	winEngine.startLevel(1);

	int verdictCalls = 0;
	winEngine.setDefaultWinDisabled(true);      // the mode declares "I decide"
	winEngine.setWinConditionHook([&verdictCalls](QString* reason)
	{
		++verdictCalls;
		if (verdictCalls < 3) { return -1; }    // "no opinion" for the first two steps
		if (reason) { *reason = QStringLiteral("mod says: time is up"); }
		return 1;
	});
	CHECK(winEngine.winTakenOver(), "the engine knows the win check was taken over");

	winEngine.tick(1.0 / 60.0);
	CHECK(winEngine.phase() == bp::Phase::Running, "no verdict yet -> still running");
	winEngine.tick(1.0 / 60.0);
	winEngine.tick(1.0 / 60.0);
	CHECK(winEngine.phase() == bp::Phase::Victory, "the mod's verdict ended the level");
	CHECK(winEngine.finishReason() == QStringLiteral("mod says: time is up"),
		  "the reason is kept for the UI");

	// the trap this flag exists for: a 0-enemy mode must not auto-win
	bp::SingleGameEngine idleEngine(config);
	idleEngine.setMapGenerator(std::make_unique<FixedMapGenerator>());
	idleEngine.startLevel(1);
	idleEngine.setDefaultWinDisabled(true);
	idleEngine.setWinConditionHook([](QString*) { return -1; });
	for (int i = 0; i < 5; ++i) { idleEngine.tick(1.0 / 60.0); }
	CHECK(idleEngine.phase() == bp::Phase::Running,
		  "default win check disabled -> a 0-enemy level does not auto-win");

	// ---------- 7) player damage request ----------
	bp::SingleGameEngine hurtEngine(config);
	hurtEngine.setMapGenerator(std::make_unique<FixedMapGenerator>());
	hurtEngine.startLevel(1);

	CHECK(!hurtEngine.requestDamagePlayer(1, 1), "single player only has index 0");
	const int lives = hurtEngine.playerLives();
	CHECK(hurtEngine.requestDamagePlayer(0, 1), "damaging player 0 is accepted");
	CHECK_EQ_INT(hurtEngine.playerLives(), lives - 1, "lives dropped by the requested amount");

	// ---------- 8) time scale (the loop side of time.scale) ----------
	bp::SingleGameEngine loopEngine(config);
	loopEngine.setMapGenerator(std::make_unique<FixedMapGenerator>());
	loopEngine.startLevel(1);

	bp::GameLoop tickLoop(&loopEngine);
	int stepped = 0;
	tickLoop.setSteppedCallback([&stepped](int n) { stepped += n; });

	tickLoop.setTimeScale(0.0);
	tickLoop.pump(0.5);
	CHECK_EQ_INT(stepped, 0, "time scale 0 advances no logic step");

	tickLoop.setTimeScale(1.0);
	tickLoop.pump(tickLoop.stepSeconds());
	CHECK_EQ_INT(stepped, 1, "time scale 1 advances exactly one step");

	tickLoop.setTimeScale(9.0);
	CHECK_NEAR(tickLoop.timeScale(), 4.0, 1e-9, "the scale is clamped to 4 (determinism guard)");
}

// ============================================================
// ghost anti-stuck: the ghost item lets you walk THROUGH walls;
// when it expires while you stand inside one, the player must be
// pushed back to a walkable cell instead of being stuck forever.
// ============================================================
static void testGhostAntiStuck()
{
	GameConfig config;
	config.enemyCountPerLevel = 0;
	config.playerSpeed = 4.0;

	SingleGameEngine engine(config);
	engine.setRandomSeed(7);
	engine.startLevel(1);

	CHECK(engine.applyEffect(bp::EffectId::Star), "ghost item (star) applies");
	CHECK(engine.playerGhost(), "player is in ghost state (can pass walls)");

	// find a brick cell in the interior: bricks are NOT walkable, so standing on
	// one when the ghost expires is exactly the "stuck inside a solid tile" case
	GridPos brick(0, 0);
	bool foundBrick = false;
	for (int r = 1; r < engine.board().rows() - 1 && !foundBrick; ++r)
	{
		for (int c = 1; c < engine.board().cols() - 1; ++c)
		{
			const GridPos candidate(r, c);
			if (engine.board().tile(candidate) == TileType::Brick)
			{
				brick = candidate;
				foundBrick = true;
				break;
			}
		}
	}
	CHECK(foundBrick, "the level contains at least one brick");

	if (foundBrick)
	{
		// with the ghost active we walk through everything: align row, then column
		for (int guard = 0; guard < 600 && engine.playerPos().cell.row != brick.row; ++guard)
		{
			InputState in;
			if (engine.playerPos().cell.row > brick.row) { in.up = true; } else { in.down = true; }
			engine.setInput(in);
			engine.tick(1.0 / 60.0);
		}
		for (int guard = 0; guard < 600 && engine.playerPos().cell.col != brick.col; ++guard)
		{
			InputState in;
			if (engine.playerPos().cell.col > brick.col) { in.left = true; } else { in.right = true; }
			engine.setInput(in);
			engine.tick(1.0 / 60.0);
		}

		CHECK(engine.playerPos().cell == brick, "ghost walked onto a brick tile");
		CHECK(engine.board().tile(engine.playerPos().cell) == TileType::Brick,
			  "the player really stands on a non-walkable tile");

		// stop moving and let the ghost timer run out (15s for the star, 18s is safe)
		InputState none;
		for (int i = 0; i < 18 * 60; ++i) { engine.setInput(none); engine.tick(1.0 / 60.0); }

		CHECK(!engine.playerGhost(), "ghost expired");
		const GridPos after = engine.playerPos().cell;
		CHECK(engine.board().walkable(after),
			  "after the ghost expires the player was nudged onto a walkable cell (anti-stuck)");
	}
}

// ============================================================
// lobby bind sequence: refresh (bind 0) then join (bind 0 again)
// must NOT fail. This is the reported "port bind failed after
// refreshing the room list" bug, reproduced without any UI.
// ============================================================
static void testLobbyBindSequence()
{
	NetSession& session = NetSession::instance();
	session.close();

	CHECK(session.bind(0), "refresh: binds an ephemeral port");
	const quint16 ephemeral = session.localPort();
	CHECK(ephemeral != 0, "refresh: system assigned a port");

	// the regression: joining right after a refresh used to call bind(0) again
	CHECK(session.bindOrRebind(0), "join after refresh: does not fail");
	CHECK_EQ_INT(static_cast<int>(session.localPort()), static_cast<int>(ephemeral),
				 "join after refresh: keeps the already bound port");

	// joining then hosting needs the lobby port, so it must rebind
	// (any free port here: 9999 may be taken by a real lobby on this machine)
	const quint16 hostPort = 45999;
	CHECK(session.bindOrRebind(hostPort), "host after join: rebinds to the lobby port");
	CHECK_EQ_INT(static_cast<int>(session.localPort()), static_cast<int>(hostPort),
				 "host: bound to the lobby port");

	CHECK(session.bindOrRebind(hostPort), "rebinding the same port is a no-op success");

	session.close();
	CHECK(!session.isOpen(), "session closed cleanly");
}

int main(int argc, char* argv[])
{
	QCoreApplication app(argc, argv);

	std::printf("=== BlastPioneer engine-core tests ===\n");

	testGridPos();
	testBoardBasics();
	testBlastCells();
	testMapGeneration();
	testMovementFrameRateIndependence();
	testEngineInput();
	testEngineWallsStayIntact();
	testEngineBombAndExplosion();
	testEngineDeterminism();
	testEngineEnemiesStayInside();
	testGameLoopStepping();
	testGeometryInvariants();
	testDemoModeEngine();
	testKeyDispatch();

	// phase 2: the multiplayer mode lives in the engine too
	testMultiplayerSetup();
	testMultiplayerInputAndBombs();
	testMultiplayerBlastDamage();
	testMultiplayerClientMirror();
	testMultiplayerWinCondition();
	testMultiplayerDropsAndEffects();
	testEngineResourceInjection();
	testNetworkProtocolRoundTrip();
	testLevelData();
	testModApiContract();
	testGameplayPatches();
	testUiTokens();
	testLaunchGuard();
	testNativeAbiContract();
	testModHooksAndWriteRequests();
	testGhostAntiStuck();
	testLobbyBindSequence();

	std::printf("-------------------------------------\n");
	std::printf("passed %d, failed %d\n", g_passed, g_failed);

	return g_failed == 0 ? 0 : 1;
}
