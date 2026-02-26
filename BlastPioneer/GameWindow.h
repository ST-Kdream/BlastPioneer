#pragma once
#include "levelSelectWindow.h"
#include "PlayerInfo.h"
#include "BagWindow.h"
#include <QWidget>
#include <QTimer>
#include <QKeyEvent>
#include <QPainter>
#include <QList>
#include <QPoint>
#include <QRandomGenerator>
#include <QMessageBox>

//地图元素枚举
enum TileType
{
	emptyTile,
	wallTile,   //可破坏
	brickTile,  //不可破坏
	playerTile,
	enemyTile,
	bombTile,
	explosionTile
};

class GameWindow :public QWidget
{
	Q_OBJECT

private:
	int level = 0;
	PlayerInfo playerInfo;
	QTimer* gameTimer;
	BagWindow* bagWin;

	//游戏状态枚举
	enum GameState { running, paused, victory, defeat };
	GameState state;

	//游戏数据
	static const int ROWS = 11;
	static const int COLS = 13;
	TileType map[ROWS][COLS];
	QPoint playerPos;
	int playerLives;
	int bombRange;
	int maxBombPlace;

	struct Bomb
	{
		QPoint pos;
		int timer;
		int power;
	};
	QList<Bomb> bombList;

	QList<QPoint> enemyList;

	//游戏函数
	void initMap(int level);
	void spawnEnemies(int count);
	void moveEnemies();
	void placeBomb();
	void updateExplosions();
	bool isWalkable(int row, int col, bool ignorePlayer = false);
	void winGame();
	void loseGame();
	void openBag();

public:
	GameWindow(int level, PlayerInfo& playerInfo, QWidget* parent = nullptr);
	~GameWindow();
	void setLevel(int level);

protected:
	//重写事件相关函数
	void paintEvent(QPaintEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void showEvent(QShowEvent* event) override;
	void closeEvent(QCloseEvent* event) override;

private slots:
	void updateGame();
	void closeBag();
};
