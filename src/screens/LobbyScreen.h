#pragma once
#include "ScreenBase.h"
#include "NetworkProtocol.h"
#include <QMap>
#include <QList>
#include <QString>
#include <QHostAddress>
#include <QMetaObject>

class QLabel;
class QListWidget;
class QLineEdit;
class QPushButton;
class QTimer;
class QUdpSocket;

//发现的房间信息
struct RoomInfo {
	QHostAddress address;
	quint16 port;
	QString hostName;
	int playerCount;
	int maxPlayers;
};

//联机大厅界面：原 LobbyWindow
//socket 改由 NetSession 统一持有（大厅与对战界面共用），避免两个界面同时读同一个 socket
class LobbyScreen : public ScreenBase
{
	Q_OBJECT

public:
	explicit LobbyScreen(QWidget* parent = nullptr);
	~LobbyScreen();

	QString screenId() const override { return "Lobby"; }
	QString displayTitle() const override { return "联机大厅"; }
	//常驻：初始化时统一创建，之后反复进出复用同一个实例（onEnter 会重置联机状态）
	bool persistent() const override { return true; }

	void onEnter(const QVariantMap& args) override;
	void onExit() override;

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

private:
	void setupUI();
	void connectSignals();

	void switchToHostMode();
	void switchToClientMode(const QHostAddress& addr, quint16 port);
	//从对局返回大厅后端口已释放，需要重新绑定
	bool ensureSocketBound();
	void processJoinRequest(const QHostAddress& sender, quint16 senderPort, const QByteArray& data);
	void broadcastPlayerList();
	void sendRoomAnnounce();

	void updateRoomListUI();
	void updatePlayerListUI();

	//网络对象（来自 NetSession）
	QUdpSocket* socket = nullptr;
	QMetaObject::Connection readyReadConnection;

	QTimer* announceTimer = nullptr;   // 主机广播定时器
	QTimer* countdownTimer = nullptr;  // 倒计时定时器
	QHostAddress serverAddress;
	quint16 serverPort = 9999;

	// 状态
	enum Mode { None, Host, Client };
	Mode currentMode = None;
	bool isHost = false;
	int nextPlayerId = 2;        // 主机分配玩家ID
	int myPlayerId = 0;          // 客户端自己的ID
	int countdownSeconds = 0;

	// 玩家数据
	QMap<int, QString> playerNames;               // id -> 名字
	QMap<int, ClientEndpoint> clientEndpoints;    // 主机记录所有客户端

	// 发现的房间列表（仅客户端）
	QList<RoomInfo> discoveredRooms;

	// UI控件
	QLabel* statusLabel = nullptr;
	QListWidget* playerListWidget = nullptr;    // 显示当前房间玩家
	QListWidget* roomListWidget = nullptr;      // 显示发现的房间
	QLineEdit* ipEdit = nullptr;
	QLineEdit* portEdit = nullptr;
	QPushButton* createRoomBtn = nullptr;
	QPushButton* joinSelectedBtn = nullptr;
	QPushButton* manualJoinBtn = nullptr;
	QPushButton* refreshBtn = nullptr;
	QPushButton* startGameBtn = nullptr;
	QPushButton* backBtn = nullptr;
	QLabel* countdownLabel = nullptr;
};
