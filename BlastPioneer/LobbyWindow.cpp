#include "LobbyWindow.h"
#include "MainWindow.h"
#include "NetworkProtocol.h"
#include "MultiGameWindow.h"
#include "SettingsManager.h"
#include <QRandomGenerator>
#include <QTimer>

static const quint16 LOBBY_PORT = 9999;

LobbyWindow::LobbyWindow(MainWindow* mainWin, QWidget* parent)
    : QWidget(parent), mainWin(mainWin), isHost(false), currentMode(None),
    nextPlayerId(2), countdownSeconds(0)
{
    setWindowTitle("联机大厅");
    resize(700, 500);
    setMinimumSize(600, 400);
    setWindowFlags(Qt::Window);

    socket = new QUdpSocket(this);
    serverPort = LOBBY_PORT;

    announceTimer = new QTimer(this);
    connect(announceTimer, &QTimer::timeout, this, &LobbyWindow::sendRoomAnnounce);

    countdownTimer = new QTimer(this);
    connect(countdownTimer, &QTimer::timeout, this, &LobbyWindow::updateCountdown);

    setupUI();
    connectSignals();

    connect(socket, &QUdpSocket::readyRead, this, &LobbyWindow::readPendingDatagrams);
}

LobbyWindow::~LobbyWindow()
{
    if (socket->isOpen()) socket->close();
}

void LobbyWindow::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // 状态栏
    statusLabel = new QLabel("选择模式：创建房间或加入房间", this);
    statusLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #2C3E50;");
    mainLayout->addWidget(statusLabel);

    // 玩家列表（显示已加入的玩家）
    playerListWidget = new QListWidget(this);
    playerListWidget->setStyleSheet("font-size: 13px;");
    playerListWidget->setMaximumHeight(100);
    mainLayout->addWidget(playerListWidget);

    // 房间列表
    roomListWidget = new QListWidget(this);
    roomListWidget->setStyleSheet("font-size: 13px;");
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

    // 样式（略... 保持原有风格）
    QString btnStyle = R"(
        QPushButton {
            font-size: 13px; padding: 8px 16px; border: none;
            border-radius: 6px; color: white;
        }
        QPushButton:disabled { background-color: #CCCCCC; color: #666; }
    )";
    createRoomBtn->setStyleSheet(btnStyle + "QPushButton { background-color: #27AE60; } QPushButton:hover { background-color: #2ECC71; }");
    joinSelectedBtn->setStyleSheet(btnStyle + "QPushButton { background-color: #2980B9; } QPushButton:hover { background-color: #3498DB; }");
    manualJoinBtn->setStyleSheet(btnStyle + "QPushButton { background-color: #8E44AD; } QPushButton:hover { background-color: #9B59B6; }");
    refreshBtn->setStyleSheet(btnStyle + "QPushButton { background-color: #F39C12; } QPushButton:hover { background-color: #F1C40F; }");
    startGameBtn->setStyleSheet(btnStyle + "QPushButton { background-color: #E74C3C; } QPushButton:hover { background-color: #EC7063; }");
    backBtn->setStyleSheet(btnStyle + "QPushButton { background-color: #95A5A6; } QPushButton:hover { background-color: #BDC3C7; }");

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
    countdownLabel->setStyleSheet("font-size: 36px; font-weight: bold; color: #E74C3C; qproperty-alignment: AlignCenter;");
    countdownLabel->hide();
    mainLayout->addWidget(countdownLabel);

    setLayout(mainLayout);
}

void LobbyWindow::connectSignals()
{
    connect(createRoomBtn, &QPushButton::clicked, this, &LobbyWindow::createRoom);
    connect(joinSelectedBtn, &QPushButton::clicked, this, &LobbyWindow::joinSelected);
    connect(manualJoinBtn, &QPushButton::clicked, this, &LobbyWindow::onManualJoin);
    connect(refreshBtn, &QPushButton::clicked, this, &LobbyWindow::onRefresh);
    connect(startGameBtn, &QPushButton::clicked, this, &LobbyWindow::startGame);
    connect(backBtn, &QPushButton::clicked, this, &LobbyWindow::goBack);
    connect(roomListWidget, &QListWidget::itemClicked, this, [this]() {
        joinSelectedBtn->setEnabled(true);
        });
}

// ==================== 主机模式 ====================

void LobbyWindow::switchToHostMode()
{
    currentMode = Host;
    isHost = true;
    statusLabel->setText("主机模式 - 等待玩家加入...");

    createRoomBtn->setEnabled(false);
    joinSelectedBtn->setEnabled(false);
    manualJoinBtn->setEnabled(false);
    refreshBtn->setEnabled(false);
    startGameBtn->setEnabled(true);

    if (!socket->bind(QHostAddress::Any, serverPort)) {
        QMessageBox::warning(this, "错误", QString("绑定端口 %1 失败，请关闭占用该端口的程序").arg(serverPort));
        return;
    }

    playerNames.clear();
    clientEndpoints.clear();
    playerNames[1] = mainWin->getPlayerInfo().getUserName();
    nextPlayerId = 2;

    // 启动广播，让别人能发现本房间
    announceTimer->start(3000);

    updatePlayerListUI();
}

// ==================== 客户端模式 ====================

void LobbyWindow::switchToClientMode(const QHostAddress& addr, quint16 port)
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

    if (!socket->bind(QHostAddress::Any, 0)) {
        QMessageBox::warning(this, "错误", "绑定端口失败");
        return;
    }

    // 发送加入请求
    QByteArray packet;
    QDataStream stream(&packet, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << static_cast<quint16>(MSG_JOIN_REQ) << mainWin->getPlayerInfo().getUserName();
    socket->writeDatagram(packet, addr, port);
}

// ==================== 按钮槽 ====================

void LobbyWindow::createRoom()
{
    switchToHostMode();
}

void LobbyWindow::joinSelected()
{
    QListWidgetItem* item = roomListWidget->currentItem();
    if (!item) return;

    int idx = item->data(Qt::UserRole).toInt();
    if (idx < 0 || idx >= discoveredRooms.size()) return;

    const RoomInfo& room = discoveredRooms[idx];
    switchToClientMode(room.address, room.port);
}

void LobbyWindow::onManualJoin()
{
    QString ip = ipEdit->text().trimmed();
    quint16 port = static_cast<quint16>(portEdit->text().trimmed().toUShort());
    if (ip.isEmpty() || port == 0) {
        QMessageBox::warning(this, "提示", "请输入有效的IP和端口");
        return;
    }
    switchToClientMode(QHostAddress(ip), port);
}

void LobbyWindow::onRefresh()
{
    discoveredRooms.clear();
    updateRoomListUI();

    if (!socket->bind(QHostAddress::Any, 0)) {
        QMessageBox::warning(this, "错误", "刷新绑定端口失败");
        return;
    }

    // 广播查询请求
    QByteArray packet;
    QDataStream stream(&packet, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << static_cast<quint16>(MSG_JOIN_REQ) << QString("__broadcast__");
    socket->writeDatagram(packet, QHostAddress::Broadcast, LOBBY_PORT);
}

// ==================== 网络收发 ====================

void LobbyWindow::readPendingDatagrams()
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
                if (accepted) {
                    statusLabel->setText(QString("成功加入房间 (ID: %1)").arg(assignedId));
                    myPlayerId = assignedId;
                }
                else {
                    QMessageBox::warning(this, "加入失败", "房间已满或不存在");
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
                showCountdown();
                break;
            }
            case MSG_GAME_STATE: {
                // 游戏状态转发给 MultiGameWindow（在游戏窗口内部处理）
                break;
            }
            case MSG_GAME_OVER: {
                // 游戏结束，返回大厅
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

void LobbyWindow::processJoinRequest(const QHostAddress& sender, quint16 senderPort, const QByteArray& data)
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
            << mainWin->getPlayerInfo().getUserName()
            << static_cast<qint32>(playerNames.size())
            << static_cast<qint32>(4);
        socket->writeDatagram(packet, sender, senderPort);
        return;
    }

    // 正常加入请求
    if (playerNames.size() >= 4) {
        QByteArray packet;
        QDataStream reply(&packet, QIODevice::WriteOnly);
        reply.setVersion(QDataStream::Qt_6_0);
        reply << static_cast<quint16>(MSG_JOIN_ACK) << false << 0;
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

void LobbyWindow::broadcastPlayerList()
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

void LobbyWindow::sendRoomAnnounce()
{
    QByteArray packet;
    QDataStream stream(&packet, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << static_cast<quint16>(MSG_JOIN_ACK)
        << true << 0
        << mainWin->getPlayerInfo().getUserName()
        << static_cast<qint32>(playerNames.size())
        << static_cast<qint32>(4);
    socket->writeDatagram(packet, QHostAddress::Broadcast, LOBBY_PORT);
}

// ==================== 开始游戏 ====================

void LobbyWindow::startGame()
{
    if (currentMode != Host || playerNames.size() < 2) {
        QMessageBox::information(this, "提示", "至少需要2名玩家才能开始");
        return;
    }

    // 通知所有客户端开始
    QByteArray packet;
    QDataStream stream(&packet, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << static_cast<quint16>(MSG_START_GAME);

    for (auto it = clientEndpoints.begin(); it != clientEndpoints.end(); ++it) {
        socket->writeDatagram(packet, it.value().address, it.value().port);
    }

    // 主机自己倒计时
    showCountdown();
}

void LobbyWindow::showCountdown()
{
    countdownSeconds = 3;
    countdownLabel->setText(QString("游戏将在 %1 秒后开始").arg(countdownSeconds));
    countdownLabel->show();
    countdownTimer->start(1000);
}

void LobbyWindow::updateCountdown()
{
    countdownSeconds--;
    if (countdownSeconds <= 0) {
        countdownTimer->stop();
        countdownLabel->hide();

        // 创建游戏窗口
        MultiGameWindow* gameWin = new MultiGameWindow(
            mainWin->getPlayerInfo(),
            isHost,
            socket,
            serverAddress,
            serverPort,
            playerNames,
            clientEndpoints,
            this
        );
        gameWin->setAttribute(Qt::WA_DeleteOnClose);
        connect(gameWin, &QObject::destroyed, this, &LobbyWindow::onGameClosed);
        gameWin->show();
        this->hide();
    }
    else {
        countdownLabel->setText(QString("游戏将在 %1 秒后开始").arg(countdownSeconds));
    }
}

void LobbyWindow::onGameClosed()
{
    // 游戏结束，重新显示大厅
    this->show();
    this->raise();
    this->activateWindow();

    // 如果是主机，继续广播房间
    if (isHost) {
        announceTimer->start(3000);
        startGameBtn->setEnabled(true);
    }
}

// ==================== UI 更新 ====================

void LobbyWindow::updateRoomListUI()
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

void LobbyWindow::updatePlayerListUI()
{
    playerListWidget->clear();
    for (auto it = playerNames.begin(); it != playerNames.end(); ++it) {
        QString text = QString("玩家 %1: %2").arg(it.key()).arg(it.value());
        playerListWidget->addItem(text);
    }
}

// ==================== 返回与关闭 ====================

void LobbyWindow::goBack()
{
    if (socket->isOpen()) socket->close();
    announceTimer->stop();
    countdownTimer->stop();
    mainWin->show();
    this->close();
}

void LobbyWindow::closeEvent(QCloseEvent* event)
{
    if (socket->isOpen()) socket->close();
    announceTimer->stop();
    countdownTimer->stop();
    mainWin->show();
    QWidget::closeEvent(event);
}