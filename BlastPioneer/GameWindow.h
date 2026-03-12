#pragma once
#include "PlayerInfo.h"
#include <QWidget>
#include <QTimer>
#include <QKeyEvent>
#include <QPainter>
#include <QList>
#include <QPoint>
#include <QRandomGenerator>
#include <QMessageBox>
#include <QPointF>
#include <QtMath>
#include <QCoreApplication>
#include <QDir>
#include <QPixmap>
#include <algorithm>

class BagWindow;
class MainWindow;

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

//炸弹所有者
enum BombOwer { playerBomb, EnemyBomb };

//炸弹结构体
struct Bomb
{
	QPointF pos;
	float timer;
	int bombRange;
	BombOwer ower;
};

//敌人结构体
struct Enemy
{
	QPointF pos;
	int hp;
	int maxHp;
	QPointF velocity;
	float bombCooldown;
};

//掉落物结构体
struct DropItem
{
	QPointF pos;
	QString name;
	bool collected;
};

class GameWindow :public QWidget
{
	Q_OBJECT

private:
	int level = 0;
	PlayerInfo playerInfo;
	QTimer* gameTimer;
	BagWindow* bagWin;
	MainWindow* mainWin;

	//游戏状态枚举
	enum GameState { running, paused, victory, defeat };
	GameState state;

	//游戏数据
	static const int ROWS = 11;
	static const int COLS = 13;
	TileType map[ROWS][COLS];

	//玩家数据
	QPointF playerPos;
	int playerLives;
	int maxLives;
	int bombRange;
	int maxBombPlace;

	//是否移动
	bool isMoveup, isMovedown, isMoveleft, isMoveright;

	//速度常量
	const double playerSpeed = 3.5;
	const double enemySpeed = 3.5;
	double speedFactor; //速度因子
	double enemySpeedFactor;

	//其他数据
	int frameRate;
	double frameTime;
	double cellSize;
	bool showGrid;

	//数据列表
	QList<Enemy> enemyList;
	QList<Bomb> bombList;
	QList<DropItem> dropItemList;
	QStringList dropItems = { "生命药水","迅猛之靴","改造扳手","超级背包"}; //可掉落物品

	//电子幽灵计时器
	bool isGhost;
	QTimer* ghostTimer;

	//图片资源
	QPixmap playerImg;
	QPixmap enemyImg;
	QPixmap bombImg;
	QPixmap liveBottleImg;
	QPixmap quickBootImg;
	QPixmap bombUpperImg;
	bool useImages;
	void loadImages();

	//游戏函数
	void initMap(int level);
	void spawnEnemies(int count);
	QPointF gridToPixel(int row, int col) const;
	QPoint pixelToGrid(const QPointF& pos) const;
	bool isWalkable(int row, int col, bool ignorePlayer = false) const;
	void handMovement();
	void updateEnemies();
	void tryPlaceEnemyBomb(Enemy& enemy);
	void updateBombs();
	void explodeBomb(const Bomb& bomb);
	void checkDropCollection();
	void spawnDropItem(const QPointF& pos);
	void applyItemEffect(const QString& itemName);
	void winGame();
	void loseGame();
	void openBag();

	//画面缩放
	void updateCellSize();
	void recenterAll();

public:
	GameWindow(int level, PlayerInfo& info, MainWindow* mainWin, QWidget* parent = nullptr);
	~GameWindow();
	void setLevel(int level);

	//道具实现接口
	void addPlayerLives();
	void increaseSpeed();
	void upBombRange();
	void increaseMaxPlace();
	void stringUnraveEye_broken();
	void stringUnraveEye();
	void bonVoyagingStar_broken();
	void bonVoyagingStar();

protected:
	//重写事件相关函数
	void paintEvent(QPaintEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void keyReleaseEvent(QKeyEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
	void showEvent(QShowEvent* event) override;
	void closeEvent(QCloseEvent* event) override;

private slots:
	void updateGame();
	void closeBag();
	void disableGhost();
};
