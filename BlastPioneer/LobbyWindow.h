#pragma once
#include "NetworkProtocol.h"
#include <QWidget>
#include <QListWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QUdpSocket>
#include <QTimer>
#include <QHostAddress>
#include <QMap>
#include <QMessageBox>
#include <QCloseEvent>

class MainWindow;
class MultiGameWindow;

// 发现的房间信息
struct RoomInfo {
    QHostAddress address;
    quint16 port;
    QString hostName;
    int playerCount;
    int maxPlayers;
};

class LobbyWindow : public QWidget
{
    Q_OBJECT

public:
    explicit LobbyWindow(MainWindow* mainWin, QWidget* parent = nullptr);
    ~LobbyWindow();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void createRoom();
    void joinSelected();
    void onManualJoin();
    void onRefresh();
    void startGame();
    void goBack();
    void showCountdown();
    void updateCountdown();
    void readPendingDatagrams();
    void onGameClosed();

private:
    // 主窗口指针
    MainWindow* mainWin;

    // 网络相关
    QUdpSocket* socket;
    QTimer* announceTimer;   // 主机广播定时器
    QTimer* countdownTimer;  // 倒计时定时器
    QHostAddress serverAddress;
    quint16 serverPort;
    static const quint16 LOBBY_PORT = 9999;

    // 状态
    enum Mode { None, Host, Client };
    Mode currentMode;
    bool isHost;
    int nextPlayerId;        // 主机分配玩家ID
    int myPlayerId;          // 客户端自己的ID
    int countdownSeconds;

    // 玩家数据
    QMap<int, QString> playerNames;               // id -> 名字
    QMap<int, ClientEndpoint> clientEndpoints;    // 主机记录所有客户端

    // 发现的房间列表（仅客户端）
    QList<RoomInfo> discoveredRooms;

    // UI控件
    QLabel* statusLabel;
    QListWidget* playerListWidget;    // 显示当前房间玩家
    QListWidget* roomListWidget;      // 显示发现的房间
    QLineEdit* ipEdit;
    QLineEdit* portEdit;
    QPushButton* createRoomBtn;
    QPushButton* joinSelectedBtn;
    QPushButton* manualJoinBtn;
    QPushButton* refreshBtn;
    QPushButton* startGameBtn;
    QPushButton* backBtn;
    QLabel* countdownLabel;

    // 内部函数
    void setupUI();
    void connectSignals();

    void switchToHostMode();
    void switchToClientMode(const QHostAddress& addr, quint16 port);
    void processJoinRequest(const QHostAddress& sender, quint16 senderPort, const QByteArray& data);
    void broadcastPlayerList();
    void sendRoomAnnounce();

    void updateRoomListUI();
    void updatePlayerListUI();
};