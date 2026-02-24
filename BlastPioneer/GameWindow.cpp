#include "GameWindow.h"

//构造函数
GameWindow::GameWindow(int level, PlayerInfo& playerInfo, QWidget* parent)
	: level(level), QWidget(parent), playerInfo(playerInfo)
{
	// 设置默认大小，如果QSettings中没有保存的值则使用默认值
	int defaultWidth = 800;
	int defaultHeight = 600;
	int width = settings->value("windows/width", defaultWidth).toInt();
	int height = settings->value("windows/height", defaultHeight).toInt();
	resize(width, height);

	// 恢复窗口位置
	QPoint defaultPos(100, 100);  // 默认位置
	QPoint savedPos = settings->value("windows/position", defaultPos).toPoint();
	move(savedPos);

	setWindowTitle(QString("关卡-%1").arg(level));

	//初始化成员变量
	bagWin = nullptr;
	playerLives = 3;
	maxBombPlace= 1;
	bombRange = 2;

	//初始化游戏，使用计时器构建游戏主循环
	state = running;
	gameTimer = new QTimer(this);
	connect(gameTimer, &QTimer::timeout, this, &GameWindow::updateGame);
	gameTimer->start(200);

	//初始化地图
	initMap(level);
}

//析构窗口（防止在游戏结束时打开背包导致内存泄露）
GameWindow::~GameWindow()
{
	if (bagWin)
	{
		bagWin->close();
		delete bagWin;
	}
}

//设置关卡(用于窗口复用，初始化不使用)
void GameWindow::setLevel(int level)
{
	this->level = level;
	setWindowTitle(QString("关卡-%1").arg(level));
	initMap(level);
	state = running;
	update();
}

//初始化地图
void GameWindow::initMap(int level)
{
	//将所有方格设置为空
	for (int i = 0; i < ROWS; ++i)
	{
		for (int j = 0; j < COLS; ++j)
		{
			map[i][j] = emptyTile;
		}
	}

	//设置四周墙壁
	for (int i = 0; i < ROWS; ++i)
	{
		map[i][0] = wallTile;
		map[i][COLS - 1] = wallTile;
	}
	for (int j = 0; j < COLS; ++j)
	{
		map[0][j] == wallTile;
		map[ROWS - 1][j] = wallTile;
	}

	//内部随机砖块
	int brickCount = 20 + level * 5;
	QRandomGenerator randomGenerator(level * 100);
	for (int i = 0; i < brickCount; ++i)
	{
		int row = randomGenerator.bounded(1, ROWS - 1);
		int col = randomGenerator.bounded(1, COLS - 1);
		if (map[row][col] == emptyTile)
		{
			map[row][col] = brickTile;
		}
	}
}