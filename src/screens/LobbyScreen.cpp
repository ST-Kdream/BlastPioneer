#include "LobbyScreen.h"
#include "ScreenRouter.h"
#include "NetSession.h"
#include "PlayerProfile.h"
#include "FileManager.h"
#include "NativeModHost.h"
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QDialog>
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

	joinTimeoutTimer = new QTimer(this);
	joinTimeoutTimer->setSingleShot(true);
	connect(joinTimeoutTimer, &QTimer::timeout, this, &LobbyScreen::onJoinTimeout);

	setupUI();
	connectSignals();
	applyModeUi();
}

LobbyScreen::~LobbyScreen()
{
	NetSession::instance().close();
}

//进入界面
void LobbyScreen::onEnter(const QVariantMap& args)
{
	Q_UNUSED(args);

	//清空一次联机的状态（从对局返回时端口/玩家表都必须重来）
	NetSession::instance().reset();
	socket = NetSession::instance().socket();

	announceTimer->stop();
	countdownTimer->stop();
	joinTimeoutTimer->stop();
	if (readyReadConnection) { disconnect(readyReadConnection); readyReadConnection = QMetaObject::Connection(); }

	currentMode = None;
	isHost = false;
	nextPlayerId = 2;
	myPlayerId = 0;
	countdownSeconds = 0;
	joinedAck = false;
	handOffToMatch = false;
	serverAddress = QHostAddress();
	serverPort = LOBBY_PORT;
	playerNames.clear();
	clientEndpoints.clear();
	discoveredRooms.clear();

	statusLabel->setText("选择模式：创建房间，或加入下面的房间");
	countdownLabel->hide();

	updatePlayerListUI();
	updateRoomListUI();
	applyModeUi();

	//监听网络数据（只在本界面可见时处理，交给 readPendingDatagrams 内部判断）
	readyReadConnection = connect(socket, &QUdpSocket::readyRead, this, &LobbyScreen::readPendingDatagrams);
}

//离开界面：停止定时器；只有"不是去对局"时才释放端口
void LobbyScreen::onExit()
{
	if (readyReadConnection) { disconnect(readyReadConnection); readyReadConnection = QMetaObject::Connection(); }

	announceTimer->stop();
	countdownTimer->stop();
	joinTimeoutTimer->stop();

	//关键：大厅 -> 对战时 onExit 先发生（replace 会先 leaveTop 再 push）。
	//如果这里无条件 close()，对战界面拿到的是一个已关闭的 socket：
	//主机还能本地跑，客户端收不到任何快照、按键也发不出去 —— 就是"客户端只有 Esc 有用"的根因。
	if (!handOffToMatch)
	{
		// 房主直接关窗口/退出程序时也通知一下客户端，别让他们永远停在等待界面
		if (currentMode == Host) { sendRoomClosed(); }
		NetSession::instance().close();
	}
	handOffToMatch = false;
}

bool LobbyScreen::handleBack()
{
	//在房间里按 Esc：先离开房间（而不是直接回主页）。这样不会把还在等待的客户端晾在原地。
	if (currentMode != None)
	{
		leaveRoom();
		return true;
	}
	return false;
}

//重新绑定端口（从对局返回大厅时端口已被释放）
bool LobbyScreen::ensureSocketBound()
{
	if (NetSession::instance().isOpen()) { return true; }

	quint16 port = (currentMode == Host) ? LOBBY_PORT : 0;
	if (!NetSession::instance().bindOrRebind(port))
	{
		FileManager::instance().log("lobby", QString("重新绑定端口 %1 失败").arg(port));
		return false;
	}

	//主机重新开始广播房间
	if (currentMode == Host) { announceTimer->start(3000); }
	return true;
}

//UI设置
//
// 这一版把大厅收成两个**明确的状态**：空闲 / 在房间里（主机或客户端）。
// 以前的问题不是"按钮样式"，而是按钮可点性与 currentMode 各改各的：
// 从对局回来时 onEnter 只清了数据、没有恢复按钮，于是"创建房间/刷新"永久变灰；
// 而"加入房间"又依赖列表选中，选中状态一变就时灵时不灵。
// 现在所有状态迁移都走 applyModeUi()，可点性只有一处来源。
void LobbyScreen::setupUI()
{
	QVBoxLayout* mainLayout = new QVBoxLayout(this);
	mainLayout->setSpacing(10);

	// 状态栏：一句话说清"现在是什么状态、下一步该点什么"
	statusLabel = new QLabel("选择模式：创建房间，或加入下面的房间", this);
	statusLabel->setObjectName("sectionTitle");
	statusLabel->setWordWrap(true);
	mainLayout->addWidget(statusLabel);

	// ---- 当前房间 ----
	QLabel* roomTitle = new QLabel("当前房间", this);
	roomTitle->setObjectName("sectionTitle");
	mainLayout->addWidget(roomTitle);

	QLabel* roomHint = new QLabel(
		"创建或加入房间后，玩家会显示在这里。房主点「开始游戏」后双方一起进入对局。", this);
	roomHint->setObjectName("fieldHint");
	roomHint->setWordWrap(true);
	mainLayout->addWidget(roomHint);

	playerListWidget = new QListWidget(this);
	playerListWidget->setObjectName("modList");
	playerListWidget->setMinimumHeight(96);
	mainLayout->addWidget(playerListWidget, 1);

	// ---- 周围的房间（只在空闲时出现；进房间后整段隐藏，避免"看得到点不了"） ----
	roomListSection = new QWidget(this);
	roomListSection->setObjectName("roomListSection");
	QVBoxLayout* roomListLayout = new QVBoxLayout(roomListSection);
	roomListLayout->setContentsMargins(0, 0, 0, 0);
	roomListLayout->setSpacing(6);

	QLabel* discoveredTitle = new QLabel("周围的房间", roomListSection);
	discoveredTitle->setObjectName("sectionTitle");
	roomListLayout->addWidget(discoveredTitle);

	QLabel* discoveredHint = new QLabel(
		"局域网里别人创建的房间。选中一个再点「加入房间」；找不到就点「刷新列表」，"
		"或者用「手动加入…」直接填 IP 和端口。", roomListSection);
	discoveredHint->setObjectName("fieldHint");
	discoveredHint->setWordWrap(true);
	roomListLayout->addWidget(discoveredHint);

	roomListWidget = new QListWidget(roomListSection);
	roomListWidget->setObjectName("modList");
	roomListWidget->setMinimumHeight(120);
	roomListLayout->addWidget(roomListWidget, 1);
	mainLayout->addWidget(roomListSection, 1);

	// 动作提示：说明"为什么这个按钮现在不能点 / 下一步该做什么"
	actionHint = new QLabel(this);
	actionHint->setObjectName("fieldHint");
	actionHint->setWordWrap(true);
	mainLayout->addWidget(actionHint);

	// ---- 按钮行 ----
	QHBoxLayout* btnRow = new QHBoxLayout();
	btnRow->setSpacing(10);
	createRoomBtn = new QPushButton("创建房间", this);
	joinSelectedBtn = new QPushButton("加入选中房间", this);
	manualJoinBtn = new QPushButton("手动加入…", this);
	refreshBtn = new QPushButton("刷新列表", this);
	startGameBtn = new QPushButton("开始游戏", this);
	leaveRoomBtn = new QPushButton("离开房间", this);
	backBtn = new QPushButton("返回主页", this);

	createRoomBtn->setObjectName("primaryBtn");
	joinSelectedBtn->setObjectName("ghostBtn");
	manualJoinBtn->setObjectName("ghostBtn");
	refreshBtn->setObjectName("ghostBtn");
	startGameBtn->setObjectName("primaryBtn");
	leaveRoomBtn->setObjectName("accentBtn");
	backBtn->setObjectName("ghostBtn");

	createRoomBtn->setToolTip("创建一个局域网房间，别人能在「周围的房间」里看到");
	joinSelectedBtn->setToolTip("先在下面的列表里选中一个房间，再点这里");
	manualJoinBtn->setToolTip("列表里找不到时，直接填房主的 IP 和端口");
	refreshBtn->setToolTip("重新搜索局域网里的房间");
	startGameBtn->setToolTip("至少 2 名玩家才能开始");
	leaveRoomBtn->setToolTip("离开当前房间；房主离开会解散房间");
	backBtn->setToolTip("返回主页");

	// 注意：这里**不预先禁用**任何按钮 —— 初始可点性由 applyModeUi() 统一决定。
	// 以前在构造里 setEnabled(false)、onEnter 又不恢复，就是"回来以后全点不动"的来源。
	btnRow->addWidget(createRoomBtn);
	btnRow->addWidget(refreshBtn);
	btnRow->addWidget(joinSelectedBtn);
	btnRow->addWidget(manualJoinBtn);
	btnRow->addStretch(1);
	btnRow->addWidget(startGameBtn);
	btnRow->addWidget(leaveRoomBtn);
	btnRow->addWidget(backBtn);
	mainLayout->addLayout(btnRow);

	// 倒计时标签
	countdownLabel = new QLabel(this);
	countdownLabel->setObjectName("countdownLabel");
	countdownLabel->setAlignment(Qt::AlignCenter);
	countdownLabel->hide();
	mainLayout->addWidget(countdownLabel);
}

void LobbyScreen::connectSignals()
{
	connect(createRoomBtn, &QPushButton::clicked, this, &LobbyScreen::createRoom);
	connect(joinSelectedBtn, &QPushButton::clicked, this, &LobbyScreen::joinSelected);
	connect(manualJoinBtn, &QPushButton::clicked, this, &LobbyScreen::onManualJoin);
	connect(refreshBtn, &QPushButton::clicked, this, &LobbyScreen::onRefresh);
	connect(startGameBtn, &QPushButton::clicked, this, &LobbyScreen::startGame);
	connect(leaveRoomBtn, &QPushButton::clicked, this, &LobbyScreen::leaveRoom);
	connect(backBtn, &QPushButton::clicked, this, &LobbyScreen::goBack);

	// 选中变化只更新提示，不再决定"加入"按钮能不能点：
	// 按钮永远可点，没选中时点它会明确提示"请先选中一个房间"（比灰按钮好懂）。
	connect(roomListWidget, &QListWidget::itemSelectionChanged, this, &LobbyScreen::updateActionHint);
	connect(roomListWidget, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
		joinSelected();
	});
}

// ==================== 主机模式 ====================

void LobbyScreen::switchToHostMode()
{
	if (currentMode != None) { return; }

	// 房主永远监听大厅固定端口：之前手动加入过别的端口后，这里若不重置，
	// 下一次"创建房间"会去绑那个旧端口，别人就发现不了。
	serverAddress = QHostAddress();
	serverPort = LOBBY_PORT;

	if (!NetSession::instance().bindOrRebind(serverPort))
	{
		statusLabel->setText(QString("创建房间失败：端口 %1 被占用").arg(serverPort));
		QMessageBox::warning(this, "创建房间失败",
			QString("无法监听端口 %1。\n请关闭占用该端口的程序（或换一台机器做房主）后重试。")
				.arg(serverPort));
		applyModeUi();
		return;
	}

	currentMode = Host;
	isHost = true;
	joinedAck = true;
	handOffToMatch = false;
	statusLabel->setText("房间已创建，等待玩家加入…");

	playerNames.clear();
	clientEndpoints.clear();
	playerNames[1] = PlayerProfile::instance().info().getUserName();
	nextPlayerId = 2;
	myPlayerId = 1;

	// 启动广播，让别人能发现本房间
	announceTimer->start(3000);

	updatePlayerListUI();
	applyModeUi();
}

// ==================== 客户端模式 ====================

void LobbyScreen::switchToClientMode(const QHostAddress& addr, quint16 port)
{
	if (currentMode != None) { return; }

	if (!NetSession::instance().bindOrRebind(0))
	{
		QMessageBox::warning(this, "加入失败", "无法创建本地网络连接，请检查防火墙设置。");
		applyModeUi();
		return;
	}

	currentMode = Client;
	isHost = false;
	joinedAck = false;
	serverAddress = addr;
	serverPort = port;

	statusLabel->setText(QString("正在加入 %1:%2 …").arg(addr.toString()).arg(port));

	// 发送加入请求
	// 末尾捎带本机的模组集合指纹：主机拿它和自己的比，不一致就明确拒绝（契约 §7.5）
	// ——"宁可进不去，也不要带着不同规则打一局"
	QByteArray packet;
	QDataStream stream(&packet, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_6_0);
	stream << static_cast<quint16>(MSG_JOIN_REQ) << PlayerProfile::instance().info().getUserName()
		   << NativeModHost::instance().modSetHash();
	socket->writeDatagram(packet, addr, port);

	// 没有回执时不能让玩家永远停在"正在加入…"：6 秒后自动回到空闲并说明原因
	joinTimeoutTimer->start(6000);
	applyModeUi();
}

// ==================== 按钮槽 ====================

void LobbyScreen::createRoom()
{
	if (currentMode != None) { return; }
	switchToHostMode();
}

void LobbyScreen::joinSelected()
{
	if (currentMode != None) { return; }

	QListWidgetItem* item = roomListWidget->currentItem();
	if (!item)
	{
		// 这里刻意不用"禁用按钮"：按钮永远可点，点错了给一句明确的话
		statusLabel->setText("还没有选中房间：请先在「周围的房间」里点一个房间");
		updateActionHint();
		roomListWidget->setFocus();
		return;
	}

	const int idx = item->data(Qt::UserRole).toInt();
	if (idx < 0 || idx >= discoveredRooms.size())
	{
		statusLabel->setText("这个列表项不是房间（看起来是提示文字），请重新刷新列表");
		return;
	}

	const RoomInfo& room = discoveredRooms[idx];
	switchToClientMode(room.address, room.port);
}

// 手动加入：IP / 端口放进一个对话框里
// （原来这两个输入框常驻在界面上，既占地方又让人不知道它们是干什么的）
void LobbyScreen::onManualJoin()
{
	if (currentMode != None) { return; }

	QDialog dialog(this);
	dialog.setWindowTitle("手动加入房间");
	dialog.setMinimumWidth(360);

	QLineEdit* ipField = new QLineEdit(&dialog);
	ipField->setPlaceholderText("127.0.0.1");
	QLineEdit* portField = new QLineEdit(&dialog);
	portField->setPlaceholderText("9999");
	portField->setText(QString::number(LOBBY_PORT));

	QFormLayout* form = new QFormLayout();
	form->addRow("房主的 IP：", ipField);
	form->addRow("端口：", portField);

	QDialogButtonBox* buttons = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	buttons->button(QDialogButtonBox::Ok)->setText("加入");
	buttons->button(QDialogButtonBox::Cancel)->setText("取消");

	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

	QVBoxLayout* layout = new QVBoxLayout(&dialog);
	QLabel* hint = new QLabel("输入同一局域网内房主的 IP 和端口（默认 9999）。", &dialog);
	hint->setObjectName("fieldHint");
	hint->setWordWrap(true);
	layout->addWidget(hint);
	layout->addLayout(form);
	layout->addWidget(buttons);

	if (dialog.exec() != QDialog::Accepted) { return; }

	const QString ip = ipField->text().trimmed();
	const quint16 port = static_cast<quint16>(portField->text().trimmed().toUShort());
	if (ip.isEmpty() || port == 0)
	{
		QMessageBox::warning(this, "提示", "请输入有效的 IP 和端口");
		return;
	}
	switchToClientMode(QHostAddress(ip), port);
}

// ==================== 状态机 ====================

//离开当前房间（或取消"正在加入"）。所有路径都从这里收尾，避免半截状态。
void LobbyScreen::leaveRoom()
{
	if (currentMode == None)
	{
		// 空闲态再按"离开"= 没有房间可离开，不做事
		return;
	}

	joinTimeoutTimer->stop();
	countdownTimer->stop();
	announceTimer->stop();
	countdownLabel->hide();
	countdownSeconds = 0;

	// 房主离开：先告诉客户端，否则他们会永远停在"等待开始"
	if (currentMode == Host) { sendRoomClosed(); }

	NetSession::instance().reset();
	socket = NetSession::instance().socket();

	currentMode = None;
	isHost = false;
	nextPlayerId = 2;
	myPlayerId = 0;
	joinedAck = false;
	handOffToMatch = false;
	serverAddress = QHostAddress();
	serverPort = LOBBY_PORT;
	playerNames.clear();
	clientEndpoints.clear();
	discoveredRooms.clear();

	statusLabel->setText("已离开房间。选择模式：创建房间，或加入下面的房间");
	updatePlayerListUI();
	updateRoomListUI();
	applyModeUi();
}

//所有"哪些按钮可见/可点"的唯一来源。
//规则：
//   空闲  : 创建 / 刷新 / 加入选中 / 手动加入 / 返回主页 全部可点
//   主机  : 开始游戏（至少 2 人时可点，否则禁用并在 actionHint 说明原因）/ 离开房间
//   客户端: 离开房间（等待/已加入都可点）；其余隐藏
//按钮的"禁用"只保留真正无法执行的一种情况（房主一个人开局），并且旁边一定有文字说明。
void LobbyScreen::applyModeUi()
{
	const bool idle = (currentMode == None);
	const bool host = (currentMode == Host);
	const bool countdownActive = countdownTimer->isActive();

	roomListSection->setVisible(idle);

	createRoomBtn->setVisible(idle);
	refreshBtn->setVisible(idle);
	joinSelectedBtn->setVisible(idle);
	manualJoinBtn->setVisible(idle);
	backBtn->setVisible(idle);

	startGameBtn->setVisible(host);
	leaveRoomBtn->setVisible(!idle);

	createRoomBtn->setEnabled(idle);
	refreshBtn->setEnabled(idle);
	joinSelectedBtn->setEnabled(idle);   // 永远可点：没选中会给提示
	manualJoinBtn->setEnabled(idle);
	backBtn->setEnabled(idle);

	const bool canStart = host && playerNames.size() >= 2 && !countdownActive;
	startGameBtn->setEnabled(canStart);
	startGameBtn->setToolTip(canStart
		? QStringLiteral("开始这一局（3 秒倒计时）")
		: (countdownActive ? QStringLiteral("倒计时中…")
						   : QStringLiteral("至少需要 2 名玩家才能开始")));

	leaveRoomBtn->setEnabled(!idle);

	updateActionHint();
}

void LobbyScreen::updateActionHint()
{
	if (!actionHint) { return; }

	switch (currentMode)
	{
	case None:
	{
		QListWidgetItem* item = roomListWidget->currentItem();
		const bool selected = item && item->data(Qt::UserRole).toInt() >= 0;
		actionHint->setText(selected
			? QStringLiteral("已选中一个房间。点「加入选中房间」进入；没有反应会自动取消并说明原因。")
			: QStringLiteral("空闲状态：创建房间，或先在「周围的房间」里选中一个再加入。"));
		break;
	}
	case Host:
		if (countdownTimer->isActive())
		{
			actionHint->setText(QStringLiteral("倒计时中…现在点「离开房间」会取消本局并通知所有客户端。"));
		}
		else if (playerNames.size() < 2)
		{
			actionHint->setText(QStringLiteral("等待玩家加入：至少 2 人才能开始（「开始游戏」会保持灰色）。"));
		}
		else
		{
			actionHint->setText(QStringLiteral("人齐了：点「开始游戏」，3 秒后双方一起进入对局。"));
		}
		break;
	case Client:
		actionHint->setText(joinedAck
			? QStringLiteral("已加入房间，等待房主开始。点「离开房间」可以退出。")
			: QStringLiteral("正在连接房主…6 秒内没有回应会自动取消并回到空闲状态。"));
		break;
	default:
		break;
	}
}

//房主离开时广播"房间已解散"
void LobbyScreen::sendRoomClosed()
{
	if (!socket || !NetSession::instance().isOpen()) { return; }

	QByteArray packet;
	QDataStream stream(&packet, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_6_0);
	stream << static_cast<quint16>(MSG_ROOM_CLOSED);

	for (auto it = clientEndpoints.constBegin(); it != clientEndpoints.constEnd(); ++it)
	{
		socket->writeDatagram(packet, it.value().address, it.value().port);
	}
}

void LobbyScreen::onJoinTimeout()
{
	if (currentMode != Client || joinedAck) { return; }

	leaveRoom();
	statusLabel->setText("加入失败：房主没有回应。请确认 IP/端口正确、房主已创建房间，再试一次。");
}

void LobbyScreen::onRefresh()
{
	// 只在空闲状态搜索：房间里刷新会把 socket 重绑到临时端口，破坏当前连接
	if (currentMode != None) { return; }

	discoveredRooms.clear();
	updateRoomListUI();

	// 已经绑定过就不再重复绑定（原来重复 bind 会失败，导致"刷新列表后端口无法绑定"）
	if (!NetSession::instance().isOpen()) {
		if (!NetSession::instance().bindOrRebind(0)) {
			statusLabel->setText("刷新失败：无法创建本地网络连接（请检查防火墙）");
			QMessageBox::warning(this, "刷新失败", "无法创建本地网络连接，请检查防火墙设置。");
			return;
		}
	}

	// 广播查询请求
	QByteArray packet;
	QDataStream stream(&packet, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_6_0);
	stream << static_cast<quint16>(MSG_JOIN_REQ) << QString("__broadcast__");
	socket->writeDatagram(packet, QHostAddress::Broadcast, LOBBY_PORT);

	statusLabel->setText("正在搜索局域网房间…若列表为空，可再刷一次或用「手动加入…」");
	updateActionHint();
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

				if (accepted && assignedId > 0) {
					joinTimeoutTimer->stop();
					joinedAck = true;
					myPlayerId = assignedId;
					statusLabel->setText(QString("已加入房间（你是 %1 号），等待房主开始…")
						.arg(assignedId));
					updateActionHint();
				}
				else if (!accepted) {
					joinTimeoutTimer->stop();
					// 留在联机大厅里，不要一脚踢回主页：玩家可以马上换一个房间再试
					leaveRoom();
					statusLabel->setText("加入失败：" + (reason.isEmpty()
						? QString("房间已满或不存在") : reason));
					QMessageBox::warning(this, "加入失败",
						reason.isEmpty() ? QString("房间已满或不存在") : reason);
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
			case MSG_ROOM_CLOSED: {
				// 房主解散/离开：回到空闲状态，并明确告诉玩家发生了什么
				leaveRoom();
				statusLabel->setText("房主已解散房间，已回到大厅空闲状态。");
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
			case MSG_GAME_PAUSE: {
				// 暂停由对战界面处理
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

				if (!accepted || hostName.isEmpty()) { continue; }

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
					updateActionHint();
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

	// 已经开始倒计时：不再中途收人（否则新玩家没有收到 START_GAME 的时序保证）
	if (countdownTimer->isActive() || handOffToMatch) {
		QByteArray packet;
		QDataStream reply(&packet, QIODevice::WriteOnly);
		reply.setVersion(QDataStream::Qt_6_0);
		reply << static_cast<quint16>(MSG_JOIN_ACK) << false << 0
			  << QString("房主已经开始游戏，请等这一局结束再加入");
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
	if (currentMode != Host) { return; }

	if (countdownTimer->isActive())
	{
		statusLabel->setText("已经在倒计时了，请稍候…");
		return;
	}

	if (playerNames.size() < 2)
	{
		// 按钮此时是禁用的；这里再兜一道，避免键盘/程序调用绕过 UI
		statusLabel->setText("至少需要 2 名玩家才能开始：请让朋友在大厅里刷新并加入你的房间。");
		updateActionHint();
		return;
	}

	// 从对局返回后端口已释放，这里重新绑定
	if (!ensureSocketBound()) {
		statusLabel->setText("无法绑定端口：请关闭占用该端口的程序后重试");
		QMessageBox::warning(this, "开始失败", "无法绑定端口，请关闭占用该端口的程序后重试");
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

	statusLabel->setText("已通知所有玩家，3 秒后开始…");
	// 主机自己倒计时
	showCountdown();
}

void LobbyScreen::showCountdown()
{
	countdownSeconds = 3;
	countdownLabel->setText(QString("游戏将在 %1 秒后开始").arg(countdownSeconds));
	countdownLabel->show();
	countdownTimer->start(1000);
	applyModeUi();
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
		joinTimeoutTimer->stop();

		// 关键：标记"这次离开大厅是去对局"，onExit 就不会把 socket 关掉。
		// 对战界面要用同一个 socket 收/发；关掉之后就是客户端全程静止。
		handOffToMatch = true;

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

	if (discoveredRooms.isEmpty())
	{
		QListWidgetItem* hint = new QListWidgetItem(
			QStringLiteral("还没有发现房间 —— 点「刷新列表」再试一次，或用「手动加入…」"), roomListWidget);
		hint->setFlags(Qt::NoItemFlags);
		hint->setData(Qt::UserRole, -1);
		return;
	}

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

	if (playerNames.isEmpty())
	{
		QListWidgetItem* hint = new QListWidgetItem(
			QStringLiteral("还没有创建或加入房间 —— 先点下面的「创建房间」，或加入一个周围的房间"),
			playerListWidget);
		hint->setFlags(Qt::NoItemFlags);
	}
	else
	{
		for (auto it = playerNames.begin(); it != playerNames.end(); ++it) {
			QString text = QString("玩家 %1：%2").arg(it.key()).arg(it.value());
			playerListWidget->addItem(text);
		}
	}

	// 玩家数变化会直接影响"开始游戏"能不能点，统一在这里刷新一次
	applyModeUi();
}

// ==================== 返回 ====================

void LobbyScreen::goBack()
{
	// 房间里的"返回"= 先离开房间（按钮本身在房间里是隐藏的，这里兜底键盘/程序调用）
	if (currentMode != None)
	{
		leaveRoom();
		return;
	}
	emit requestBack();
}
