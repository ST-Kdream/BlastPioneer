#include "LobbyScreen.h"
#include "ScreenRouter.h"
#include "NetSession.h"
#include "PlayerProfile.h"
#include "FileManager.h"
#include "NativeModHost.h"
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QUdpSocket>
#include <QMessageBox>
#include <QRandomGenerator>

//大厅固定端口
static const quint16 LOBBY_PORT = 9999;

LobbyScreen::LobbyScreen(QWidget* parent) : ScreenBase(parent)
{
	setWindowTitle("联机大厅");

	//socket 由 NetSession 统一持有，这里只取指针
	socket = NetSession::instance().socket();
	serverPort = LOBBY_PORT;

	announceTimer = new QTimer(this);
	connect(announceTimer, &QTimer::timeout, this, &LobbyScreen::sendRoomAnnounce);

	countdownTimer = new QTimer(this);
	connect(countdownTimer, &QTimer::timeout, this, &LobbyScreen::updateCountdown);

	setupUI();
	connectSignals();
}

LobbyScreen::~LobbyScreen()
{
	NetSession::instance().close();
}

//进入界面
void LobbyScreen::onEnter(const QVariantMap& args)
{
	Q_UNUSED(args);

	//清空一次联机的状态
	NetSession::instance().reset();
	socket = NetSession::instance().socket();

	currentMode = None;
	isHost = false;
	nextPlayerId = 2;
	myPlayerId = 0;
	countdownSeconds = 0;
	playerNames.clear();
	clientEndpoints.clear();
	discoveredRooms.clear();

	updatePlayerListUI();
	updateRoomListUI();
	statusLabel->setText("选择模式：创建房间或加入房间");
	countdownLabel->hide();

	//监听网络数据（只在本界面可见时处理，交给 readPendingDatagrams 内部判断）
	readyReadConnection = connect(socket, &QUdpSocket::readyRead, this, &LobbyScreen::readPendingDatagrams);
}

//离开界面：停止定时器并释放端口
void LobbyScreen::onExit()
{
	if (readyReadConnection) { disconnect(readyReadConnection); }

	announceTimer->stop();
	countdownTimer->stop();

	NetSession::instance().close();
}

//重新绑定端口（从对局返回大厅时端口已被释放）
bool LobbyScreen::ensureSocketBound()
{
	if (NetSession::instance().isOpen()) { return true; }

	quint16 port = (currentMode == Host) ? LOBBY_PORT : 0;
	if (!NetSession::instance().bind(port))
	{
		FileManager::instance().log("lobby", QString("重新绑定端口 %1 失败").arg(port));
		return false;
	}

	//主机重新开始广播房间
	if (currentMode == Host) { announceTimer->start(3000); }
	return true;
}

//UI设置
void LobbyScreen::setupUI()
{
	QVBoxLayout* mainLayout = new QVBoxLayout(this);

	// 状态栏
	statusLabel = new QLabel("选择模式：创建房间或加入房间", this);
	statusLabel->setObjectName("sectionTitle");
	mainLayout->addWidget(statusLabel);

	// 玩家列表（显示已加入的玩家）
	playerListWidget = new QListWidget(this);
	playerListWidget->setMaximumHeight(100);
	mainLayout->addWidget(playerListWidget);

	// 房间列表
	roomListWidget = new QListWidget(this);
	mainLayout->addWidget(roomListWidget);

	// IP/端口输入
	QHBoxLayout* joinRow = new QHBoxLayout();
	QLabel* ipLabel = new QLabel("IP:", this);
	ipEdit = new QLineEdit(this);
	ipEdit->setPlaceholderText("127.0.0.1");
	ipEdit->setFixedWidth(140);
	QLabel* portLabel = new QLabel("端口:", this);
	portEdit = new QLineEdit(this);
	portEdit->setPlaceholderText("9999");
	portEdit->setFixedWidth(80);
	joinRow->addWidget(ipLabel);
	joinRow->addWidget(ipEdit);
	joinRow->addWidget(portLabel);
	joinRow->addWidget(portEdit);
	mainLayout->addLayout(joinRow);

	// 按钮行
	QHBoxLayout* btnRow = new QHBoxLayout();
	btnRow->setSpacing(10);
	createRoomBtn = new QPushButton("创建房间", this);
	joinSelectedBtn = new QPushButton("加入选中房间", this);
	manualJoinBtn = new QPushButton("手动加入", this);
	refreshBtn = new QPushButton("刷新列表", this);
	startGameBtn = new QPushButton("开始游戏", this);
	backBtn = new QPushButton("返回", this);

	// 样式走 objectName（三种按钮角色 + 一个倒计时标签），不在这里写颜色
	createRoomBtn->setObjectName("primaryBtn");
	joinSelectedBtn->setObjectName("ghostBtn");
	manualJoinBtn->setObjectName("ghostBtn");
	refreshBtn->setObjectName("ghostBtn");
	startGameBtn->setObjectName("primaryBtn");
	backBtn->setObjectName("accentBtn");

	startGameBtn->setEnabled(false);
	joinSelectedBtn->setEnabled(false);

	btnRow->addWidget(createRoomBtn);
	btnRow->addWidget(joinSelectedBtn);
	btnRow->addWidget(manualJoinBtn);
	btnRow->addWidget(refreshBtn);
	btnRow->addWidget(startGameBtn);
	btnRow->addWidget(backBtn);
	mainLayout->addLayout(btnRow);

	// 倒计时标签
	countdownLabel = new QLabel(this);
	countdownLabel->setObjectName("countdownLabel");
	countdownLabel->setAlignment(Qt::AlignCenter);
	countdownLabel->hide();
	mainLayout->addWidget(countdownLabel);

	setLayout(mainLayout);
}

void LobbyScreen::connectSignals()
{
	connect(createRoomBtn, &QPushButton::clicked, this, &LobbyScreen::createRoom);
	connect(joinSelectedBtn, &QPushButton::clicked, this, &LobbyScreen::joinSelected);
	connect(manualJoinBtn, &QPushButton::clicked, this, &LobbyScreen::onManualJoin);
	connect(refreshBtn, &QPushButton::clicked, this, &LobbyScreen::onRefresh);
	connect(startGameBtn, &QPushButton::clicked, this, &LobbyScreen::startGame);
	connect(backBtn, &QPushButton::clicked, this, &LobbyScreen::goBack);
	connect(roomListWidget, &QListWidget::itemClicked, this, [this]() {
		joinSelectedBtn->setEnabled(true);
		});
}

// ==================== 主机模式 ====================

void LobbyScreen::switchToHostMode()
{
	currentMode = Host;
	isHost = true;
	statusLabel->setText("主机模式 - 等待玩家加入...");

	createRoomBtn->setEnabled(false);
	joinSelectedBtn->setEnabled(false);
	manualJoinBtn->setEnabled(false);
	refreshBtn->setEnabled(false);
	startGameBtn->setEnabled(true);

	if (!NetSession::instance().bind(serverPort)) {
		QMessageBox::warning(this, "错误", QString("绑定端口 %1 失败，请关闭占用该端口的程序").arg(serverPort));
		currentMode = None;
		isHost = false;
		return;
	}

	playerNames.clear();
	clientEndpoints.clear();
	playerNames[1] = PlayerProfile::instance().info().getUserName();
	nextPlayerId = 2;
	myPlayerId = 1;

	// 启动广播，让别人能发现本房间
	announceTimer->start(3000);

	updatePlayerListUI();
}

// ==================== 客户端模式 ====================

void LobbyScreen::switchToClientMode(const QHostAddress& addr, quint16 port)
{
	currentMode = Client;
	isHost = false;
	serverAddress = addr;
	serverPort = port;

	statusLabel->setText(QString("已连接到主机 %1:%2，等待开始...").arg(addr.toString()).arg(port));

	createRoomBtn->setEnabled(false);
	joinSelectedBtn->setEnabled(false);
	manualJoinBtn->setEnabled(false);
	refreshBtn->setEnabled(false);
	startGameBtn->setEnabled(false);

	if (!NetSession::instance().bind(0)) {
		QMessageBox::warning(this, "错误", "绑定端口失败");
		currentMode = None;
		return;
	}

	// 发送加入请求
	// 末尾捎带本机的模组集合指纹：主机拿它和自己的比，不一致就明确拒绝（契约 §7.5）
	// ——"宁可进不去，也不要带着不同规则打一局"
	QByteArray packet;
	QDataStream stream(&packet, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_6_0);
	stream << static_cast<quint16>(MSG_JOIN_REQ) << PlayerProfile::instance().info().getUserName()
		   << NativeModHost::instance().modSetHash();
	socket->writeDatagram(packet, addr, port);
}

// ==================== 按钮槽 ====================

void LobbyScreen::createRoom()
{
	switchToHostMode();
}

void LobbyScreen::joinSelected()
{
	QListWidgetItem* item = roomListWidget->currentItem();
	if (!item) return;

	int idx = item->data(Qt::UserRole).toInt();
	if (idx < 0 || idx >= discoveredRooms.size()) return;

	const RoomInfo& room = discoveredRooms[idx];
	switchToClientMode(room.address, room.port);
}

void LobbyScreen::onManualJoin()
{
	QString ip = ipEdit->text().trimmed();
	quint16 port = static_cast<quint16>(portEdit->text().trimmed().toUShort());
	if (ip.isEmpty() || port == 0) {
		QMessageBox::warning(this, "提示", "请输入有效的IP和端口");
		return;
	}
	switchToClientMode(QHostAddress(ip), port);
}

void LobbyScreen::onRefresh()
{
	discoveredRooms.clear();
	updateRoomListUI();

	// 已经绑定过就不再重复绑定（原来重复 bind 会失败，导致"刷新列表后端口无法绑定"）
	if (!NetSession::instance().isOpen()) {
		if (!NetSession::instance().bind(0)) {
			QMessageBox::warning(this, "错误", "刷新绑定端口失败");
			return;
		}
	}

	// 广播查询请求
	QByteArray packet;
	QDataStream stream(&packet, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_6_0);
	stream << static_cast<quint16>(MSG_JOIN_REQ) << QString("__broadcast__");
	socket->writeDatagram(packet, QHostAddress::Broadcast, LOBBY_PORT);
}

// ==================== 网络收发 ====================

void LobbyScreen::readPendingDatagrams()
{
	while (socket->hasPendingDatagrams()) {
		QByteArray datagram;
		datagram.resize(socket->pendingDatagramSize());
		QHostAddress sender;
		quint16 senderPort;
		socket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);

		QDataStream stream(datagram);
		stream.setVersion(QDataStream::Qt_6_0);
		quint16 msgType;
		stream >> msgType;

		if (currentMode == Host) {
			if (msgType == MSG_JOIN_REQ) {
				processJoinRequest(sender, senderPort, datagram);
			}
		}
		else if (currentMode == Client) {
			switch (msgType) {
			case MSG_JOIN_ACK: {
				bool accepted;
				int assignedId;
				stream >> accepted >> assignedId;

				//拒绝原因（老版本不发送时读不到，就退回一句通用说明）
				QString reason;
				if (!stream.atEnd()) { stream >> reason; }

				if (accepted) {
					statusLabel->setText(QString("成功加入房间 (ID: %1)").arg(assignedId));
					myPlayerId = assignedId;
				}
				else {
					QMessageBox::warning(this, "加入失败",
						reason.isEmpty() ? QString("房间已满或不存在") : reason);
					goBack();
				}
				break;
			}
			case MSG_PLAYER_LIST: {
				stream >> playerNames;
				updatePlayerListUI();
				break;
			}
			case MSG_START_GAME: {
				// 顺带收下主机决定的对局种子（老版本不发送时读不到，会退回默认种子）
				quint32 matchSeed = 0;
				if (!stream.atEnd())
				{
					stream >> matchSeed;
					NetSession::instance().setMatchSeed(matchSeed);
				}
				showCountdown();
				break;
			}
			case MSG_GAME_STATE: {
				// 游戏状态由对战界面自己处理（本界面只在游戏界面隐藏时才会收到）
				break;
			}
			case MSG_GAME_OVER: {
				// 游戏结束，由对战界面处理
				break;
			}
			default:
				break;
			}
		}
		else {  // 空闲状态（未加入房间）
			if (msgType == MSG_JOIN_ACK) {
				// 收到广播回复
				bool accepted;
				int assignedId;
				QString hostName;
				qint32 playerCount, maxPlayers;
				stream >> accepted >> assignedId >> hostName >> playerCount >> maxPlayers;

				// 检查是否已存在该房间
				bool exists = false;
				for (const RoomInfo& r : discoveredRooms) {
					if (r.address == sender && r.port == senderPort) {
						exists = true;
						break;
					}
				}
				if (!exists) {
					RoomInfo room;
					room.address = sender;
					room.port = senderPort;
					room.hostName = hostName;
					room.playerCount = static_cast<int>(playerCount);
					room.maxPlayers = static_cast<int>(maxPlayers);
					discoveredRooms.append(room);
					updateRoomListUI();
				}
			}
		}
	}
}

// ==================== 主机处理加入请求 ====================

void LobbyScreen::processJoinRequest(const QHostAddress& sender, quint16 senderPort, const QByteArray& data)
{
	QDataStream stream(data);
	stream.setVersion(QDataStream::Qt_6_0);
	quint16 msgType;
	QString playerName;
	stream >> msgType >> playerName;

	// 广播查询回复
	if (playerName == "__broadcast__") {
		QByteArray packet;
		QDataStream reply(&packet, QIODevice::WriteOnly);
		reply.setVersion(QDataStream::Qt_6_0);
		reply << static_cast<quint16>(MSG_JOIN_ACK)
			<< true << 0
			<< PlayerProfile::instance().info().getUserName()
			<< static_cast<qint32>(playerNames.size())
			<< static_cast<qint32>(4);
		socket->writeDatagram(packet, sender, senderPort);
		return;
	}

	// 模组集合校验（契约 §7.5）：
	// 各端模组不同 = 各端规则不同 —— 那不该"进去之后行为诡异"，而该在这里说清楚
	QString clientMods;
	if (!stream.atEnd()) { stream >> clientMods; }

	const QString hostMods = NativeModHost::instance().modSetHash();
	if (!clientMods.isEmpty() && clientMods != hostMods) {
		FileManager::instance().log("lobby", QString("拒绝加入：模组集合不一致（对方 %1，本机 %2）")
			.arg(clientMods, hostMods));

		QByteArray packet;
		QDataStream reply(&packet, QIODevice::WriteOnly);
		reply.setVersion(QDataStream::Qt_6_0);
		reply << static_cast<quint16>(MSG_JOIN_ACK) << false << 0
			  << QString("房主的模组与你不同，无法加入：请把两边的模组（含原生模组）调成一致再试");
		socket->writeDatagram(packet, sender, senderPort);
		return;
	}

	// 正常加入请求
	if (playerNames.size() >= 4) {
		QByteArray packet;
		QDataStream reply(&packet, QIODevice::WriteOnly);
		reply.setVersion(QDataStream::Qt_6_0);
		reply << static_cast<quint16>(MSG_JOIN_ACK) << false << 0
			  << QString("房间已满（最多 4 人）");
		socket->writeDatagram(packet, sender, senderPort);
		return;
	}

	int assignedId = nextPlayerId++;
	ClientEndpoint ep;
	ep.address = sender;
	ep.port = senderPort;
	clientEndpoints[assignedId] = ep;
	playerNames[assignedId] = playerName;

	// 回复接受
	QByteArray packet;
	QDataStream reply(&packet, QIODevice::WriteOnly);
	reply.setVersion(QDataStream::Qt_6_0);
	reply << static_cast<quint16>(MSG_JOIN_ACK) << true << assignedId;
	socket->writeDatagram(packet, sender, senderPort);

	// 广播新玩家列表
	broadcastPlayerList();

	statusLabel->setText(QString("玩家 %1 已加入 (共 %2 人)").arg(playerName).arg(playerNames.size()));
	updatePlayerListUI();
}

// ==================== 广播 ====================

void LobbyScreen::broadcastPlayerList()
{
	QByteArray packet;
	QDataStream stream(&packet, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_6_0);
	stream << static_cast<quint16>(MSG_PLAYER_LIST) << playerNames;

	for (auto it = clientEndpoints.begin(); it != clientEndpoints.end(); ++it) {
		socket->writeDatagram(packet, it.value().address, it.value().port);
	}

	updatePlayerListUI();
}

void LobbyScreen::sendRoomAnnounce()
{
	QByteArray packet;
	QDataStream stream(&packet, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_6_0);
	stream << static_cast<quint16>(MSG_JOIN_ACK)
		<< true << 0
		<< PlayerProfile::instance().info().getUserName()
		<< static_cast<qint32>(playerNames.size())
		<< static_cast<qint32>(4);
	socket->writeDatagram(packet, QHostAddress::Broadcast, LOBBY_PORT);
}

// ==================== 开始游戏 ====================

void LobbyScreen::startGame()
{
	if (currentMode != Host || playerNames.size() < 2) {
		QMessageBox::information(this, "提示", "至少需要2名玩家才能开始");
		return;
	}

	// 从对局返回后端口已释放，这里重新绑定
	if (!ensureSocketBound()) {
		QMessageBox::warning(this, "错误", "无法绑定端口，请关闭占用该端口的程序后重试");
		return;
	}

	// 对局种子：主机在这里决定，随"开始游戏"一起广播
	// 为什么必须广播：地图是主机与客户端各自按种子生成的，
	// 种子不一致就会出现"两边地图不同、互相看不见"的诡异现象
	const quint32 matchSeed = QRandomGenerator::global()->generate();
	NetSession::instance().setMatchSeed(matchSeed);

	// 通知所有客户端开始
	QByteArray packet;
	QDataStream stream(&packet, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_6_0);
	stream << static_cast<quint16>(MSG_START_GAME) << matchSeed;

	for (auto it = clientEndpoints.begin(); it != clientEndpoints.end(); ++it) {
		socket->writeDatagram(packet, it.value().address, it.value().port);
	}

	// 主机自己倒计时
	showCountdown();
}

void LobbyScreen::showCountdown()
{
	countdownSeconds = 3;
	countdownLabel->setText(QString("游戏将在 %1 秒后开始").arg(countdownSeconds));
	countdownLabel->show();
	countdownTimer->start(1000);
}

void LobbyScreen::updateCountdown()
{
	countdownSeconds--;
	if (countdownSeconds <= 0) {
		countdownTimer->stop();
		countdownLabel->hide();

		// 进入对战界面：把本次联机的状态写进 NetSession，供对战界面读取
		NetSession& session = NetSession::instance();
		session.setIsHost(isHost);
		session.setMyPlayerId(isHost ? 1 : myPlayerId);
		session.setHostEndpoint(serverAddress, serverPort);
		session.setPlayerNames(playerNames);
		session.setClientEndpoints(clientEndpoints);

		// 主机在开局后停止大厅广播
		announceTimer->stop();

		// 替换栈顶：对战结束后回到本界面（本界面离开时会关闭端口，结束时会重新绑定）
		ScreenRouter::instance().replace(ScreenId::MultiGame);
	}
	else {
		countdownLabel->setText(QString("游戏将在 %1 秒后开始").arg(countdownSeconds));
	}
}

// ==================== UI 更新 ====================

void LobbyScreen::updateRoomListUI()
{
	roomListWidget->clear();
	for (int i = 0; i < discoveredRooms.size(); ++i) {
		const RoomInfo& room = discoveredRooms[i];
		QListWidgetItem* item = new QListWidgetItem(
			QString("%1 [%2/%3] - %4:%5")
			.arg(room.hostName)
			.arg(room.playerCount)
			.arg(room.maxPlayers)
			.arg(room.address.toString())
			.arg(room.port)
		);
		item->setData(Qt::UserRole, i);
		roomListWidget->addItem(item);
	}
}

void LobbyScreen::updatePlayerListUI()
{
	playerListWidget->clear();
	for (auto it = playerNames.begin(); it != playerNames.end(); ++it) {
		QString text = QString("玩家 %1: %2").arg(it.key()).arg(it.value());
		playerListWidget->addItem(text);
	}
}

// ==================== 返回 ====================

void LobbyScreen::goBack()
{
	emit requestBack();
}
