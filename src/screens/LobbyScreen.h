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
class QWidget;

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
	//Esc：在房间里先"离开房间"（保住玩家的直觉），空闲时才回上一屏
	bool handleBack() override;

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
	//加入请求发出后 6 秒内没有 ACK（IP/端口写错、房主没开）：回到空闲状态并说明原因
	void onJoinTimeout();

private:
	void setupUI();
	void connectSignals();

	void switchToHostMode();
	void switchToClientMode(const QHostAddress& addr, quint16 port);
	//离开当前房间/取消加入：停定时器、关 socket、把界面恢复成空闲态
	void leaveRoom();
	//按 currentMode 统一刷新"哪些按钮可见/可点"，所有状态迁移都必须经过它
	void applyModeUi();
	void updateActionHint();
	//房主离开时通知还在等待的客户端，避免他们永远停在"等待开始"
	void sendRoomClosed();
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
	QTimer* joinTimeoutTimer = nullptr; // 客户端等待 JOIN_ACK 的超时
	QHostAddress serverAddress;
	quint16 serverPort = 9999;

	// 状态
	enum Mode { None, Host, Client };
	Mode currentMode = None;
	bool isHost = false;
	int nextPlayerId = 2;        // 主机分配玩家ID
	int myPlayerId = 0;          // 客户端自己的ID
	int countdownSeconds = 0;
	bool joinedAck = false;      // 客户端是否已经收到接受回执
	// 大厅 -> 对战时不要把 socket 关掉：对战界面要用同一个 socket 收发。
	// 之前 onExit 无条件 close()，结果主机还能本地玩、客户端一个包都收不到。
	bool handOffToMatch = false;

	// 玩家数据
	QMap<int, QString> playerNames;               // id -> 名字
	QMap<int, ClientEndpoint> clientEndpoints;    // 主机记录所有客户端

	// 发现的房间列表（仅客户端）
	QList<RoomInfo> discoveredRooms;

	// UI控件
	QLabel* statusLabel = nullptr;
	QWidget* roomListSection = nullptr;         // "周围的房间"整段（空闲时才显示）
	QListWidget* playerListWidget = nullptr;    // 显示当前房间玩家
	QListWidget* roomListWidget = nullptr;      // 显示发现的房间
	QPushButton* createRoomBtn = nullptr;
	QPushButton* joinSelectedBtn = nullptr;
	QPushButton* manualJoinBtn = nullptr;
	QPushButton* refreshBtn = nullptr;
	QPushButton* startGameBtn = nullptr;
	QPushButton* leaveRoomBtn = nullptr;
	QPushButton* backBtn = nullptr;
	QLabel* actionHint = nullptr;
	QLabel* countdownLabel = nullptr;
};
