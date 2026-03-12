#include "GameWindow.h"
#include "levelSelectWindow.h"
#include "PlayerInfo.h"
#include "BagWindow.h"
#include "SettingsManager.h"
#include "MainWindow.h"

//构造函数
GameWindow::GameWindow(int level, PlayerInfo& info, QWidget* parent)
	: level(level), QWidget(parent), playerInfo(info), maxLives(3), bombRange(2), maxBombPlace(3),
	isMoveup(false), isMovedown(false), isMoveleft(false), isMoveright(false), cellSize(0),
	bagWin(nullptr), state(paused), speedFactor(1.0), enemySpeedFactor(1.0), ghostTimer(nullptr),isGhost(false)
{
	setWindowFlags(Qt::Window);
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

	//从设置中读取信息
	frameRate = SettingsManager::instance()->frameRate();
	frameTime = 1.0 / frameRate;
	showGrid = SettingsManager::instance()->showGrid();

	//加载图片资源
	loadImages();

	//初始化游戏，使用计时器构建游戏主循环
	gameTimer = new QTimer(this);
	connect(gameTimer, &QTimer::timeout, this, &GameWindow::updateGame);
	gameTimer->setInterval(1000/frameRate);

	//电子幽灵计时器
	ghostTimer = new QTimer(this);
	connect(ghostTimer, &QTimer::timeout, this, &GameWindow::disableGhost);
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
	updateCellSize();
	initMap(level);
	state = running;
	gameTimer->start();
	update();
}

//加载图片资源
void GameWindow::loadImages()
{
	QString appDir = QCoreApplication::applicationDirPath();
	QString basePath = appDir + "/Assets/picture/";

	auto loadPixmap = [&](const QString& fileName)->QPixmap
		{
			QString fullPath = basePath + fileName;
			QPixmap pix;
			if (QFile::exists(fullPath)) { pix.load(fullPath); }
			else { QMessageBox::critical(this, "错误", "图片加载失败"); }
			return pix;
		};
	playerImg = loadPixmap("player.png");
	enemyImg = loadPixmap("enemy.png");
	bombImg = loadPixmap("bomb.png");
	liveBottleImg = loadPixmap("liveBottle.png");
	quickBootImg = loadPixmap("quickBoots.png");
	bombUpperImg = loadPixmap("bombUpper.png");

	useImages = !playerImg.isNull() && !enemyImg.isNull() && !bombImg.isNull() &&
		!liveBottleImg.isNull() && !quickBootImg.isNull() && !bombUpperImg.isNull();
}

//格子中心像素坐标
QPointF GameWindow::gridToPixel(int row, int col) const
{
	return QPointF(col * cellSize + cellSize / 2.0, row * cellSize + cellSize / 2.0);
}

//像素坐标转换为格子索引
QPoint GameWindow::pixelToGrid(const QPointF& pos) const
{
	int col = qFloor(pos.x() / cellSize);
	int row = qFloor(pos.y() / cellSize);
	row = qBound(0, row, ROWS - 1);
	col = qBound(0, col, COLS - 1);
	return QPoint(row, col);
}

//判断格子是否可走
bool GameWindow::isWalkable(int row, int col, bool ignorePlayer) const
{
	if (row < 0 || row >= ROWS || col < 0 || col >= COLS) { return false; }
	if (ignorePlayer) { return true; }
	TileType tile = map[row][col];
	if (tile == wallTile || tile == brickTile) { return false; }
	return true;
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
	playerPos = gridToPixel(1, 1);
	map[1][1] = playerTile;

	//清空数据列表残余
	enemyList.clear();
	bombList.clear();
	dropItemList.clear();

	int enemyCount = level;
	spawnEnemies(enemyCount);
	playerLives = maxLives;
}

//生成敌人
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
				Enemy enemy;
				enemy.pos = gridToPixel(row, col);
				enemy.maxHp = 3 + level / 2;
				enemy.hp = enemy.maxHp;
				enemy.velocity = QPointF(0, 0);
				enemy.bombCooldown = 0;
				enemyList.append(enemy);
				map[row][col] = enemyTile;
				break;
			}
			++attempts;
		}
	}
}

//处理玩家移动
void GameWindow::handMovement()
{
	//处理移动量
	double pixelSpeed = playerSpeed * cellSize * speedFactor;
	QPointF delta(0, 0);
	if (isMoveleft) { delta.rx() -= pixelSpeed * frameTime; }
	if (isMoveright) { delta.rx() += pixelSpeed * frameTime; }
	if (isMoveup) { delta.ry() -= pixelSpeed * frameTime; }
	if (isMovedown) { delta.ry() += pixelSpeed * frameTime; }
	if (delta.isNull()) { return; }

	//尝试在x轴方向上移动
	QPointF newPosX(playerPos.x() + delta.x(), playerPos.y());
	int gridXRow = qFloor(newPosX.y() / cellSize);
	int gridXCol = qFloor(newPosX.x() / cellSize);
	if (isWalkable(gridXRow, gridXCol, isGhost)) { playerPos.setX(newPosX.x()); }

	//尝试在y轴方向上移动
	QPointF newPosY(playerPos.x(), playerPos.y() + delta.y());
	int gridYRow = qFloor(newPosY.y() / cellSize);
	int gridYCol = qFloor(newPosY.x() / cellSize);
	if (isWalkable(gridYRow, gridYCol, isGhost)) { playerPos.setY(newPosY.y()); }

	//在地图更新玩家标记
	QPoint oldGrid = pixelToGrid(playerPos - delta);
	QPoint newGrid = pixelToGrid(playerPos);
	if (oldGrid != newGrid)
	{
		map[oldGrid.x()][oldGrid.y()] = emptyTile;
		map[newGrid.x()][newGrid.y()] = playerTile;
	}
}

//更新敌人
void GameWindow::updateEnemies()
{
	double enemyPixelSpeed = enemySpeed * cellSize * enemySpeedFactor;

	for (Enemy& enemy : enemyList)
	{
		//移动敌人（1%概率改变方向）
		if (QRandomGenerator::global()->bounded(100) < 1)
		{
			int direction = QRandomGenerator::global()->bounded(4);
			enemy.velocity = QPointF(0,0);
			switch (direction)
			{
			case 0:
				enemy.velocity = QPointF(0, -enemyPixelSpeed * frameTime);
				break;
			case 1: 
				enemy.velocity = QPointF(0, enemyPixelSpeed * frameTime);
				break;
			case 2: 
				enemy.velocity = QPointF(-enemyPixelSpeed * frameTime, 0); 
				break;
			case 3: 
				enemy.velocity = QPointF(enemyPixelSpeed * frameTime, 0); 
				break;
			}
		}

		//尝试更新敌人位置和地图标记
		QPointF newPos = enemy.pos + enemy.velocity;
		QPoint newGrid = pixelToGrid(newPos);
		if (isWalkable(newGrid.x(), newGrid.y()))
		{
			QPoint oldGrid = pixelToGrid(enemy.pos);
			if (oldGrid != newGrid)
			{
				map[oldGrid.x()][oldGrid.y()] = emptyTile;
				map[newGrid.x()][newGrid.y()] = enemyTile;
			}
			enemy.pos = newPos;
		}
		enemy.velocity = QPointF(0, 0);

		//修改冷却时间并尝试放置炸弹
		if (enemy.bombCooldown > 0)
		{
			enemy.bombCooldown--;
		}
		else
		{
			tryPlaceEnemyBomb(enemy);
		}
	}
}

//尝试放置敌人炸弹（每一秒有40%概率触发）
void GameWindow::tryPlaceEnemyBomb(Enemy& enemy)
{
	int p;
	if (frameRate == 30) { p = 17; }
	else if (frameRate == 60) { p = 8; }
	else { p = 4; }
	if (QRandomGenerator::global()->bounded(100) < 100) //调试用
	{
		QPoint grid = pixelToGrid(enemy.pos);
		for (const Bomb& bomb : bombList)
		{
			//不重复放置
			if (pixelToGrid(bomb.pos) == grid) { return; }
		}
		Bomb bomb;
		bomb.bombRange = 2;
		bomb.pos = grid;
		bomb.ower = EnemyBomb;
		bomb.timer = 1.5f;
		bombList.append(bomb);
		map[grid.x()][grid.y()] = bombTile;
		enemy.bombCooldown = 3.0f;
	}
}

//更新炸弹状态
void GameWindow::updateBombs()
{
	QList<Bomb> newBombList;
	for (Bomb& bomb : bombList)
	{
		bomb.timer -= frameTime;
		if (bomb.timer <= 0)
		{
			explodeBomb(bomb);
		}
		else
		{
			newBombList.append(bomb);
		}
	}
	bombList = newBombList;
}

//处理炸弹爆炸
void GameWindow::explodeBomb(const Bomb& bomb)
{
	//计算爆炸砖块
	QPoint centerGrid = pixelToGrid(bomb.pos);
	QList<QPoint> explosionCells;
	explosionCells.append(centerGrid);
	int directions[4][2] = { {-1,0},{1,0},{0,-1},{0,1} };
	for (int d = 0; d < 4; ++d)
	{
		for (int step = 1; step <= bomb.bombRange; ++step)
		{
			int row = centerGrid.x() + directions[d][0] * step;
			int col = centerGrid.y() + directions[d][1] * step;
			TileType tile = map[row][col];
			if (row < 0 || row >= ROWS || col < 0 || col >= COLS) { break; }
			if (tile == wallTile) { break; }
			explosionCells.append(QPoint(row, col));
			if (tile == brickTile) { break; }
		}
	}

	for (const QPoint& explosionCell : explosionCells)
	{
		int row = explosionCell.x();
		int col = explosionCell.y();
		TileType& tile = map[row][col];

		for (const QPoint& cell : explosionCells)
		{
			int row = cell.x();
			int col = cell.y();

			//伤害敌人逻辑
			if (bomb.ower == playerBomb)
			{
				for (Enemy& enemy : enemyList)
				{
					if (pixelToGrid(enemy.pos) == cell)
					{
						enemy.hp--;
						if (enemy.hp <= 0) { spawnDropItem(enemy.pos); }
					}
				}
				enemyList.erase(std::remove_if(enemyList.begin(), enemyList.end(),
					[](const Enemy& e) {return e.hp <= 0; }), enemyList.end());
			}
			//伤害玩家逻辑
			else
			{
				QPoint playerGrid = pixelToGrid(playerPos);
				if (playerGrid == cell)
				{
					playerLives--;
					if (playerLives <= 0) { loseGame(); }
				}
			}

			//砖块更新逻辑
			if (tile == brickTile)
			{
				tile = emptyTile;
				//15%概率生成掉落物
				if (QRandomGenerator::global()->bounded(100) < 15)
				{
					spawnDropItem(gridToPixel(row, col));
				}
				else if (tile != wallTile)
				{
					tile = explosionTile;
				}
			}
		}
	}
}

//生成掉落物
void GameWindow::spawnDropItem(const QPointF& pos)
{
	int index = QRandomGenerator::global()->bounded(dropItems.size());
	DropItem dropItem;
	dropItem.pos = pos;
	dropItem.name = dropItems[index];
	dropItem.collected = false;
	dropItemList.append(dropItem);
}

//检查收集掉落物
void GameWindow::checkDropCollection()
{
	if (dropItemList.isEmpty()) { return; }
	QPoint playerGrid = pixelToGrid(playerPos);
	for (auto it = dropItemList.begin(); it != dropItemList.end();)
	{
		QPoint itemGrid = pixelToGrid(it->pos);
		if ((itemGrid == playerGrid) && (!it->collected))
		{
			applyItemEffect(it->name);
			it = dropItemList.erase(it);
		}
		else { ++it; }
	}
}

//使用道具效果
void GameWindow::applyItemEffect(const QString& itemName)
{
	if (itemName == "生命药水")
	{
		addPlayerLives();
	}
	else if (itemName == "迅猛之靴")
	{
		increaseSpeed();
	}
	else if (itemName == "改造扳手")
	{
		upBombRange();
	}
}

//更新游戏
void GameWindow::updateGame()
{
	if (state != running) { return; }

	handMovement();
	updateEnemies();
	updateBombs();
	checkDropCollection();

	if (enemyList.isEmpty()) { winGame(); return; }

	update();
}

//根据窗口大小计算格子大小
void GameWindow::updateCellSize()
{
	double w = width();
	double h = height();
	double cellW = w / COLS;
	double cellH = h / ROWS;
	cellSize = qMin(cellW, cellH);
}

//缩放窗口时重新绘制
void GameWindow::recenterAll()
{
	//居中玩家
	QPoint playerGrid = pixelToGrid(playerPos);
	playerPos = gridToPixel(playerGrid.x(), playerGrid.y());

	//居中炸弹
	for (Bomb& bomb : bombList)
	{
		QPoint bombGrid = pixelToGrid(bomb.pos);
		bomb.pos = gridToPixel(bombGrid.x(), bombGrid.y());
	}

	//居中掉落物
	for (DropItem& item : dropItemList)
	{
		QPoint itemGrid = pixelToGrid(item.pos);
		item.pos = gridToPixel(itemGrid.x(), itemGrid.y());
	}
}

//画面渲染
void GameWindow::paintEvent(QPaintEvent* event)
{
	QPainter painter(this);
	painter.fillRect(rect(), Qt::white);
	if (cellSize <= 0) { updateCellSize(); }

	//绘制地图（颜色临时）
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
			case explosionTile:
				painter.setBrush(Qt::red);
				painter.drawRect(rect);
				break;
			default:
				painter.setBrush(Qt::gray);
				painter.drawRect(rect);
				break;
			}

			//网格线
			if (showGrid)
			{
				painter.setPen(Qt::black);
				painter.drawRect(rect);
			}
		}
	}

	//绘制炸弹（图片或图形）
	if (useImages)
	{
		double imgSize = cellSize * 0.6;
		for (const Bomb& bomb : bombList)
		{
			QRectF target(bomb.pos.x() - imgSize / 2.0, bomb.pos.y() - imgSize / 2.0, imgSize, imgSize);
			painter.drawPixmap(target, bombImg,QRectF(bombImg.rect()));
		}
	}
	else
	{
		painter.setBrush(Qt::darkCyan);
		double bombRadius = cellSize * 0.3;
		for (const Bomb& bomb : bombList)
		{
			painter.drawEllipse(bomb.pos, bombRadius, bombRadius);
		}
	}

	//绘制玩家及血条
	if (useImages)
	{
		double imgSize = cellSize * 0.6;
		QRectF target(playerPos.x() - imgSize / 2.0, playerPos.y() - imgSize / 2.0, imgSize, imgSize);
		painter.drawPixmap(target, playerImg, QRectF(playerImg.rect()));
	}
	else
	{
		painter.setBrush(Qt::blue);
		painter.setPen(Qt::NoPen);
		double playerRadius = cellSize * 0.3;
		painter.drawEllipse(playerPos, playerRadius, playerRadius);
	}

	double barWidth = cellSize * 0.6;
	double barHeight = cellSize * 0.1;
	double barX = playerPos.x() - barWidth / 2.0;
	double barY = playerPos.y() - cellSize * 0.4;
	painter.setBrush(Qt::red);
	painter.drawRect(QRectF(barX, barY, barWidth, barHeight));
	painter.setBrush(Qt::green);
	double width = barWidth * playerLives / maxLives;
	painter.drawRect(QRectF(barX, barY, width, barHeight));

	//绘制敌人及敌人血条
	if (useImages)
	{
		double imgSize = cellSize * 0.6;
		for (const Enemy& enemy : enemyList)
		{
			QRectF target(enemy.pos.x() - imgSize / 2.0, enemy.pos.y() - imgSize / 2.0, imgSize, imgSize);
			painter.drawPixmap(target, enemyImg, QRect(enemyImg.rect()));

			barX = enemy.pos.x() - barWidth / 2;
			barY = enemy.pos.y() - cellSize * 0.4;
			painter.setBrush(Qt::black);
			painter.drawRect(QRectF(barX, barY, barWidth, barHeight));
			painter.setBrush(Qt::red);
			double width = barWidth * playerLives / maxLives;
			painter.drawRect(QRectF(barX, barY, width, barHeight));
		}
	}
	else
	{
		painter.setBrush(Qt::darkMagenta);
		painter.setPen(Qt::NoPen);
		double enemyRadius = cellSize * 0.3;
		for (const Enemy& enemy : enemyList)
		{
			painter.drawEllipse(enemy.pos, enemyRadius, enemyRadius);

			barX = enemy.pos.x() - barWidth / 2;
			barY = enemy.pos.y() - cellSize * 0.4;
			painter.setBrush(Qt::black);
			painter.drawRect(QRectF(barX, barY, barWidth, barHeight));
			painter.setBrush(Qt::red);
			double width = barWidth * playerLives / maxLives;
			painter.drawRect(QRectF(barX, barY, width, barHeight));
		}
	}

	//绘制掉落物
	if (useImages)
	{
		double imgSize = cellSize * 0.4;
		for (const DropItem& item : dropItemList)
		{
			QPixmap targetImg;
			if (item.name == "生命药水") { targetImg = liveBottleImg; }
			else if (item.name == "迅猛之靴") { targetImg = quickBootImg; }
			else { targetImg = bombUpperImg; }

			QRectF target(item.pos.x() - imgSize / 2.0, item.pos.y() - imgSize / 2.0, imgSize, imgSize);
			painter.drawPixmap(target, targetImg, QRect(targetImg.rect()));
		}
	}
	else
	{
		painter.setBrush(Qt::yellow);
		double itemRadius = cellSize * 0.15;
		for (const DropItem& item : dropItemList)
		{
			painter.drawEllipse(item.pos, itemRadius, itemRadius);
		}
	}
}

//处理键盘事件(按下)
void GameWindow::keyPressEvent(QKeyEvent* event)
{
	if (state != running) { return; }

	switch (event->key())
	{
	case Qt::Key_W:
		isMoveup = true; break;
	case Qt::Key_S:
		isMovedown = true; break;
	case Qt::Key_A:
		isMoveleft = true; break;
	case Qt::Key_D:
		isMoveright = true; break;
	case Qt::Key_Space:
	{
		QPoint playerGrid = pixelToGrid(playerPos);
		int playerBombcnt = 0;
		for (const Bomb& bomb : bombList)
		{
			if (pixelToGrid(bomb.pos) == playerGrid) { return; }
			if (bomb.ower == playerBomb) { playerBombcnt++; }
			if (playerBombcnt >= maxBombPlace) { return; }
		}
		Bomb bomb;
		bomb.pos = gridToPixel(playerGrid.x(), playerGrid.y());
		bomb.timer = 1.5;
		bomb.bombRange = bombRange;
		bomb.ower = playerBomb;
		bombList.append(bomb);
		map[playerGrid.x()][playerGrid.y()] = bombTile;
		break;
	}
	case Qt::Key_B:
		openBag();
		break;
	}
}

//处理键盘事件（放下）
void GameWindow::keyReleaseEvent(QKeyEvent* event)
{
	switch (event->key())
	{
	case Qt::Key_W:
		isMoveup = false; break;
	case Qt::Key_S:
		isMovedown = false; break;
	case Qt::Key_A:
		isMoveleft = false; break;
	case Qt::Key_D:
		isMoveright = false; break;
	}
}

//窗口大小调整
void GameWindow::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	updateCellSize();
	recenterAll();
	update();
}

//关闭电子幽灵态
void GameWindow::disableGhost()
{
	isGhost = false;
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
	if (!bagWin) {
		bagWin = new BagWindow(playerInfo, nullptr, nullptr); // 独立窗口
		bagWin->setAttribute(Qt::WA_DeleteOnClose);
		bagWin->setGameWin(this);   // 设置游戏窗口指针
		// 查找并设置主窗口指针（可选）
		MainWindow* mainWin = qobject_cast<MainWindow*>(parent()->parent()); // 根据实际层级调整
		if (mainWin) bagWin->setMainWin(mainWin);
		connect(bagWin, &QObject::destroyed, this, [this]() { bagWin = nullptr; });
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
	recenterAll();
	state = running;
	gameTimer->start();
}

void GameWindow::closeEvent(QCloseEvent* event)
{
	SettingsManager::instance()->saveWindowGeometry(saveGeometry());
	gameTimer->stop();
	if (bagWin) { bagWin->close(); }
	if (ghostTimer->isActive()) { ghostTimer->stop(); isGhost = false; }
	QWidget::closeEvent(event);
}

//============== 道具实现接口 ===============
void GameWindow::addPlayerLives()
{
	playerLives = qMin(playerLives + 2, maxLives);
}

void GameWindow::increaseSpeed()
{
	speedFactor += 0.15;
}

void GameWindow::upBombRange()
{
	bombRange++;
}

void GameWindow::increaseMaxPlace()
{
	maxBombPlace++;
}

void GameWindow::stringUnraveEye_broken()
{
	maxLives += 2;
	enemySpeedFactor *= 0.95;
	int index = QRandomGenerator::global()->bounded(dropItems.size());
	QString itemName = dropItems[index];
	playerInfo.addItem(itemName);
}

void GameWindow::stringUnraveEye()
{
	maxLives += 2;
	playerLives = qMin(playerLives + 2, maxLives);

	for (Enemy& enemy : enemyList)
	{
		enemy.hp--;
	}
	enemyList.erase(std::remove_if(enemyList.begin(), enemyList.end(),
		[](const Enemy& enemy) {return enemy.hp < 0; }), enemyList.end());

	int index = QRandomGenerator::global()->bounded(dropItems.size());
	QString itemName = dropItems[index];

	playerInfo.addItem(itemName);
	enemySpeedFactor *= 0.85;
}

void GameWindow::bonVoyagingStar_broken()
{
	playerLives = 1;
	for (Enemy& enemy : enemyList)
	{
		enemy.hp = 1;
	}
	if (ghostTimer->isActive()) { ghostTimer->stop(); }
	isGhost = true;
	ghostTimer->start(8000);
}

void GameWindow::bonVoyagingStar()
{
	for (Enemy& enemy : enemyList)
	{
		enemy.hp = 1;
	}
	if (ghostTimer->isActive()) ghostTimer->stop();
	isGhost = true;
	ghostTimer->start(15000);
}