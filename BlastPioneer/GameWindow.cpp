#include "GameWindow.h"

//构造函数
GameWindow::GameWindow(int level, PlayerInfo& playerInfo, QWidget* parent)
	: level(level), QWidget(parent), playerInfo(playerInfo)
{
	setWindowTitle(QString("关卡-%1").arg(level));

	QByteArray savedGeo = SettingsManager::instance()->loadWindowGeometry();
	if (!savedGeo.isEmpty()) 
	{
		restoreGeometry(savedGeo);
	}
	else 
	{
		resize(800, 600);
		move(100, 100);
	}

	//初始化成员变量
	bagWin = nullptr;
	maxLives = 3;
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
		map[0][j] = wallTile;
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
	const int cellSize = 50;

	painter.setBrush(Qt::black);
	painter.drawRect(rect());

	//绘制地图
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
	
	//绘制玩家血条
	int barWidth = cellSize - 10;
	int barHeight = 6;
	int barX = playerPos.y() * cellSize + 5;
	int barY = playerPos.x() * cellSize + cellSize - barHeight - 5;

	painter.setBrush(Qt::red);
	painter.drawRect(barX, barY, barWidth, barHeight);
	painter.setBrush(Qt::green);
	int width = barWidth * playerLives / maxLives;
	painter.drawRect(barX, barY, width, barHeight);
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
		map[playerPos.x()][playerPos.y()] = emptyTile;
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
	for (Bomb& bomb : bombList)
	{
		bomb.timer--;
		//炸弹爆炸
		if (bomb.timer <= 0)
		{
			int row = bomb.pos.x();
			int col = bomb.pos.y();
			QList<QPoint> explosionCells;
			explosionCells.append(QPoint(row, col));
			int directions[4][2] = { {-1,0},{1,0},{0,-1},{0,1} };
			for (int d = 0; d < 4; ++d)
			{
				for (int step = 1; step <= bomb.power; ++step)
				{
					int r = row + directions[d][0] * step;
					int c = col + directions[d][1] * step;
					if (r<0 || r>ROWS || c<0 || c>COLS) { break; }
					TileType tile = map[r][c];
					if (tile == wallTile) { break; }
					explosionCells.append(QPoint(r, c));
					if (tile == brickTile) { break; }
				}
			}
			//处理爆炸效果
			for (const QPoint& position : explosionCells)
			{
				int row = position.x();
				int col = position.y();
				TileType& tile = map[row][col];
				if (tile == brickTile) { tile = emptyTile; }
				else if (tile == enemyTile) { tile = emptyTile; enemyList.removeAll(position); }
				else if (tile == playerTile) 
				{ 
					--playerLives;
					if (playerLives <= 0) { loseGame(); }
				}
				else { tile = explosionTile; }
			}
			for (const QPoint& position : explosionCells)
			{
				if(map[position.x()][position.y()]==explosionTile)
				{
					map[position.x()][position.y()] = emptyTile;
				}
			}
		}
		else
		{
			newBombs.append(bomb);
		}
	}
	bombList = newBombs;

	for (const Bomb& bomb : bombList) 
	{
		map[bomb.pos.x()][bomb.pos.y()] = bombTile;
	}

	//移动动人
	moveEnemies();
	for (const QPoint& enemy : enemyList)
	{
		if (enemy == playerPos)
		{
			loseGame();
			return;
		}
	}

	//检查胜利条件
	if (enemyList.isEmpty())
	{
		winGame();
		return;
	}

	update();
}

//移动敌人
void GameWindow::moveEnemies()
{
	QRandomGenerator randomGenerator;
	QList<QPoint> newEnemyList;
	for (QPoint enemy : enemyList)
	{
		int direction = randomGenerator.bounded(4);
		int row = enemy.x();
		int col = enemy.y();
		switch (direction)
		{
		case 0:
			row--;
			break;
		case 1:
			row++;
			break;
		case 2:
			col--;
			break;
		case 3:
			col++;
			break;
		}

		if (isWalkable(row, col) && map[row][col] != enemyTile)
		{
			enemy = QPoint(row, col);
			newEnemyList.append(enemy);
			map[enemy.x()][enemy.y()] = enemyTile;
		}
	}
	enemyList = newEnemyList;
}

//游戏胜利
void GameWindow::winGame()
{
	state = victory;
	gameTimer->stop();
	if (level > playerInfo.getPassedLevels())
	{
		playerInfo.setPassedLevels(level);
	}
	playerInfo.setCoins(playerInfo.getCoins() + 50 + 5 * level);
	playerInfo.setEP(playerInfo.getEP() + 10 * level);
	QMessageBox::information(this, "胜利",
		QString("恭喜通过关卡 %1！\n获得 %2 金币").arg(level).arg(50 * level));
	close();
}

//游戏失败
void GameWindow::loseGame()
{
	state = defeat;
	gameTimer->stop();
	QMessageBox::information(this, "失败", "你被炸死了...");
	close();
}

//打开背包
void GameWindow::openBag()
{
	if (!bagWin)
	{
		bagWin = new BagWindow(playerInfo, this, nullptr);
		bagWin->setAttribute(Qt::WA_DeleteOnClose);
		connect(bagWin, &QObject::destroyed, this, [this]() {bagWin = nullptr; });
		connect(bagWin, &QObject::destroyed, this, &GameWindow::closeBag);
	}
	state = paused;
	gameTimer->stop();
	bagWin->show();
	bagWin->raise();
	bagWin->activateWindow();
}

//背包关闭
void GameWindow::closeBag()
{
	if (state == paused)
	{
		state = running;
		gameTimer->start();
	}
}

//关闭与显示窗口
void GameWindow::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);
	initMap(level);
	state = running;
	gameTimer->start();
}

void GameWindow::closeEvent(QCloseEvent* event)
{
	SettingsManager::instance()->saveWindowGeometry(saveGeometry());
	gameTimer->stop();
	if (bagWin) { bagWin->close(); }
	QWidget::closeEvent(event);
}