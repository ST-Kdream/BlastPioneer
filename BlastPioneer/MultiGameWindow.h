#pragma once

#include "PlayerInfo.h"
#include "NetworkProtocol.h"
#include <QWidget>
#include <QTimer>
#include <QKeyEvent>
#include <QPainter>
#include <QList>
#include <QPointF>
#include <QHostAddress>
#include <QUdpSocket>
#include <QMap>
#include <QMessageBox>
#include <QCloseEvent>

class MainWindow;

// 玩家状态（主机维护，客户端同步）
struct RemotePlayer {
    int id;
    QString name;
    QPointF position;
    int lives;
    int maxLives;
    int bombRange;
    int maxBombPlace;
    bool isMoveUp, isMoveDown, isMoveLeft, isMoveRight;
};

// 炸弹（多人版）
struct MultiBomb {
    QPointF pos;
    float timer;
    int bombRange;
    int ownerId;
};

class MultiGameWindow : public QWidget
{
    Q_OBJECT

public:
    MultiGameWindow(PlayerInfo& info, bool isHost, QUdpSocket* sharedSocket,
        const QHostAddress& hostAddr, quint16 hostPort,
        const QMap<int, QString>& names, const QMap<int, ClientEndpoint>& endpoints,
        QWidget* parent = nullptr);
    ~MultiGameWindow();

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void updateGame();          // 游戏主循环
    void readNetworkData();     // 接收网络数据

public slots:
    void applyGameState(const GameStateSnapshot& snap);  // 客户端应用状态

private:
    // 玩家信息（用于获取名字）
    PlayerInfo& playerInfo;

    // 网络相关
    bool isHost;
    QUdpSocket* socket;
    QHostAddress hostAddress;
    quint16 hostPort;
    QMap<int, ClientEndpoint> clientEndpoints;   // 主机记录所有客户端
    int myPlayerId;

    // 定时器
    QTimer* gameTimer;
    int frameRate;
    double frameTime;

    // 游戏状态
    enum GameState { running, paused, victory, defeat };
    GameState state;

    // 地图常量
    static const int ROWS = 11;
    static const int COLS = 13;
    int map[ROWS][COLS];    // 0-空，1-墙，2-砖块

    // 玩家数据（所有玩家）
    QMap<int, RemotePlayer> players;
    QList<MultiBomb> bombList;

    // 本机玩家数据（引用players中对应项或单独变量）
    QPointF playerPos;
    int playerLives;
    int maxLives;
    int bombRange;
    int maxBombPlace;

    // 移动状态
    bool isMoveUp, isMoveDown, isMoveLeft, isMoveRight;

    // 速度
    const double playerSpeed = 3.5;
    double speedFactor;

    // 网格
    double cellSize;
    bool showGrid;

    // 图片
    QPixmap playerImg;
    QPixmap bombImg;
    bool useImages;
    void loadImages();

    // 游戏内部函数
    void initMap();
    QPointF gridToPixel(int row, int col) const;
    QPoint pixelToGrid(const QPointF& pos) const;
    bool isWalkable(int row, int col) const;
    void updateCellSize();
    void recenterAll();
    void handleMovement();          // 仅主机调用
    void updateBombs();             // 仅主机调用
    void explodeBomb(const MultiBomb& bomb);   // 仅主机调用
    void checkGameOver();           // 仅主机调用

    // 网络发送
    void sendInputToHost();         // 客户端发送输入
    void broadcastGameState();      // 主机广播状态
    void sendGameOver(int winnerId);// 主机发送游戏结束

    // 颜色辅助
    QColor getPlayerColor(int playerId);
};