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

	//玩家起始位置
	playerPos = QPoint(1, 1);
	map[1][1] = playerTile;

	//敌人
	enemyList.clear();
	int enemyCount = level;
	spawnEnemies(enemyCount);

	bombList.clear();
}

//放置敌人
void GameWindow::spawnEnemies(int count)
{
	QRandomGenerator randomGenerator(level * 200 + count);
	for (int i = 0; i < count; ++i)
	{
		int attempts = 0;
		while (attempts < 100)
		{
			int row = randomGenerator.bounded(1, ROWS - 1);
			int col = randomGenerator.bounded(1, COLS - 1);
			if ((map[row][col] == emptyTile) && (!(row == 1 && col == 1)))
			{
				map[row][col] = enemyTile;
				enemyList.append(QPoint(row, col));
				break;
			}
		}
		++attempts;
	}
}

//画面渲染
void GameWindow::paintEvent(QPaintEvent* event)
{
	QPainter painter(this);
	int cellSize = 50;

	painter.setBrush(Qt::black);
	painter.drawRect(rect());

	for (int i = 0; i < ROWS; ++i)
	{
		for (int j = 0; j < COLS; ++j)
		{
			QRect rect(j * cellSize, i * cellSize, cellSize, cellSize);
			switch (map[i][j])
			{
			case wallTile:
				painter.setBrush(Qt::darkGray);
				painter.drawRect(rect);
				break;
			case brickTile:
				painter.setBrush(Qt::lightGray);
				painter.drawRect(rect);
				break;
			case playerTile:
				painter.setBrush(Qt::blue);
				painter.drawEllipse(rect.adjusted(5, 5, -5, -5));
				break;
			case bombTile:
				painter.setBrush(Qt::yellow);
				painter.drawEllipse(rect.adjusted(10, 10, - 10, -10));
				break;
			case explosionTile:
				painter.setBrush(Qt::white);
				painter.drawRect(rect);
				break;
			default:
				painter.setBrush(Qt::gray);
				painter.drawRect(rect);
				break;
			}

			//网格线
			painter.setPen(Qt::gray);
			painter.drawRect(rect);
		}
	}
}

//处理输入事件
void GameWindow::keyPressEvent(QKeyEvent* event)
{
	if (state != running) { return; }

	int dx = 0, dy = 0;
	switch (event->key())
	{
	case Qt::Key_W:
		dy = -1;
		break;
	case Qt::Key_S:
		dy = 1;
		break;
	case Qt::Key_A:
		dx = -1;
		break;
	case Qt::Key_D:
		dx = 1;
		break;
	case Qt::Key_Space:
		placeBomb();
		return;
	case Qt::Key_B:
		openBag();
		return;
	}

	//移动玩家
	int newRow = playerPos.x() + dx;
	int newCol = playerPos.y() + dy;
	if (isWalkable(newRow, newCol, true))
	{
		map[playerPos.x()][playerPos.y()] = emptyTile();
		playerPos = QPoint(newRow, newCol);
		map[newRow][newCol] = playerTile;
		update();
	}
}

//判断砖块是否可走
bool GameWindow::isWalkable(int row, int col, bool ignorePlayer)
{
	if (row < 0 || row >= ROWS || col < 0 || col >= COLS) { return false; }
	TileType tile = map[row][col];
	if (tile == wallTile || tile == brickTile || tile == bombTile) { return false; }
	if ((!ignorePlayer) && (tile == playerTile)) { return false; }
	return true;
}

//放置炸弹
void GameWindow::placeBomb()
{
	for (const Bomb& bombs : bombList)
	{
		if (bombs.pos == playerPos) { return; }
	}

	Bomb bomb;
	bomb.pos = playerPos;
	bomb.timer = 10;
	bomb.power = 2;
	bombList.append(bomb);
	map[playerPos.x()][playerPos.y()] = bombTile;
	update();
}

//更新游戏
void GameWindow::updateGame()
{
	if (state != running) { return; }

	QList<Bomb> newBombs;
}