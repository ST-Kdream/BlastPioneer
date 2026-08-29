#include "MultiGameWindow.h"
#include "MainWindow.h"
#include "SettingsManager.h"
#include <QPainter>
#include <QtMath>
#include <QRandomGenerator>
#include <QCoreApplication>
#include <QDir>

MultiGameWindow::MultiGameWindow(PlayerInfo& info, bool isHost, QUdpSocket* sharedSocket,
    const QHostAddress& hostAddr, quint16 hostPort,
    const QMap<int, QString>& names, const QMap<int, ClientEndpoint>& endpoints,
    QWidget* parent)
    : QWidget(parent), playerInfo(info), isHost(isHost), socket(sharedSocket),
    hostAddress(hostAddr), hostPort(hostPort), clientEndpoints(endpoints),
    state(running), speedFactor(1.0), cellSize(0)
{
    setWindowFlags(Qt::Window);
    setWindowTitle("联机对战");
    resize(800, 600);

    // 从设置读取
    frameRate = SettingsManager::instance()->frameRate();
    frameTime = 1.0 / frameRate;
    showGrid = SettingsManager::instance()->showGrid();

    loadImages();

    // 确定自己的ID
    if (isHost)
        myPlayerId = 1;
    else
        myPlayerId = names.key(info.getUserName(), 2);  // 假设用户名唯一

    // 初始化玩家列表
    for (auto it = names.begin(); it != names.end(); ++it) {
        RemotePlayer rp;
        rp.id = it.key();
        rp.name = it.value();
        rp.position = QPointF(0, 0);
        rp.lives = 3;
        rp.maxLives = 3;
        rp.bombRange = 2;
        rp.maxBombPlace = 3;
        rp.isMoveUp = rp.isMoveDown = rp.isMoveLeft = rp.isMoveRight = false;
        players[it.key()] = rp;
    }

    // 初始化本机玩家数据
    if (players.contains(myPlayerId)) {
        playerPos = players[myPlayerId].position;
        playerLives = players[myPlayerId].lives;
        maxLives = players[myPlayerId].maxLives;
        bombRange = players[myPlayerId].bombRange;
        maxBombPlace = players[myPlayerId].maxBombPlace;
    }
    else {
        playerPos = QPointF(0, 0);
        playerLives = 3;
        maxLives = 3;
        bombRange = 2;
        maxBombPlace = 3;
    }

    isMoveUp = isMoveDown = isMoveLeft = isMoveRight = false;

    gameTimer = new QTimer(this);
    connect(gameTimer, &QTimer::timeout, this, &MultiGameWindow::updateGame);

    connect(socket, &QUdpSocket::readyRead, this, &MultiGameWindow::readNetworkData);

    initMap();
    updateCellSize();
    recenterAll();

    state = running;
    gameTimer->start(1000 / frameRate);
}

MultiGameWindow::~MultiGameWindow()
{
}

void MultiGameWindow::loadImages()
{
    QString basePath = QCoreApplication::applicationDirPath() + "/Assets/picture/";
    playerImg.load(basePath + "player.png");
    bombImg.load(basePath + "bomb.png");
    useImages = !playerImg.isNull() && !bombImg.isNull();
}

void MultiGameWindow::initMap()
{
    // 清空地图
    for (int i = 0; i < ROWS; ++i)
        for (int j = 0; j < COLS; ++j)
            map[i][j] = 0;

    // 四周墙
    for (int i = 0; i < ROWS; ++i) {
        map[i][0] = 1;
        map[i][COLS - 1] = 1;
    }
    for (int j = 0; j < COLS; ++j) {
        map[0][j] = 1;
        map[ROWS - 1][j] = 1;
    }

    // 随机砖块
    QRandomGenerator rng(42);
    int brickCount = 30;
    for (int i = 0; i < brickCount; ++i) {
        int row = rng.bounded(1, ROWS - 1);
        int col = rng.bounded(1, COLS - 1);
        // 避开四个角落（玩家出生点）
        if ((row == 1 && col == 1) || (row == 1 && col == COLS - 2) ||
            (row == ROWS - 2 && col == 1) || (row == ROWS - 2 && col == COLS - 2))
            continue;
        if (map[row][col] == 0)
            map[row][col] = 2;
    }

    // 玩家初始位置分配到四个角落
    QList<QPoint> spawnPoints;
    spawnPoints << QPoint(1, 1) << QPoint(1, COLS - 2)
        << QPoint(ROWS - 2, 1) << QPoint(ROWS - 2, COLS - 2);

    int idx = 0;
    for (auto it = players.begin(); it != players.end() && idx < spawnPoints.size(); ++it, ++idx) {
        it.value().position = gridToPixel(spawnPoints[idx].x(), spawnPoints[idx].y());
    }

    if (players.contains(myPlayerId))
        playerPos = players[myPlayerId].position;
    else
        playerPos = gridToPixel(1, 1);

    bombList.clear();
}

QPointF MultiGameWindow::gridToPixel(int row, int col) const
{
    return QPointF(col * cellSize + cellSize / 2.0, row * cellSize + cellSize / 2.0);
}

QPoint MultiGameWindow::pixelToGrid(const QPointF& pos) const
{
    int col = qFloor(pos.x() / cellSize);
    int row = qFloor(pos.y() / cellSize);
    row = qBound(0, row, ROWS - 1);
    col = qBound(0, col, COLS - 1);
    return QPoint(row, col);
}

bool MultiGameWindow::isWalkable(int row, int col) const
{
    if (row < 0 || row >= ROWS || col < 0 || col >= COLS) return false;
    return map[row][col] != 1 && map[row][col] != 2;
}

void MultiGameWindow::updateCellSize()
{
    double w = width();
    double h = height();
    cellSize = qMin(w / COLS, h / ROWS);
}

void MultiGameWindow::recenterAll()
{
    // 对齐玩家位置到网格
    if (players.contains(myPlayerId)) {
        QPoint pg = pixelToGrid(playerPos);
        playerPos = gridToPixel(pg.x(), pg.y());
        players[myPlayerId].position = playerPos;
    }

    // 对齐炸弹
    for (MultiBomb& bomb : bombList) {
        QPoint bg = pixelToGrid(bomb.pos);
        bomb.pos = gridToPixel(bg.x(), bg.y());
    }
}

// ==================== 主机游戏逻辑 ====================

void MultiGameWindow::handleMovement()
{
    if (!isHost) return; // 仅主机执行

    double pixelSpeed = playerSpeed * cellSize * speedFactor;
    // 处理每个玩家
    for (auto it = players.begin(); it != players.end(); ++it) {
        RemotePlayer& p = it.value();
        if (p.lives <= 0) continue; // 死亡不移动

        QPointF delta(0, 0);
        if (p.isMoveLeft) delta.rx() -= pixelSpeed * frameTime;
        if (p.isMoveRight) delta.rx() += pixelSpeed * frameTime;
        if (p.isMoveUp) delta.ry() -= pixelSpeed * frameTime;
        if (p.isMoveDown) delta.ry() += pixelSpeed * frameTime;
        if (delta.isNull()) continue;

        // X轴尝试
        QPointF newPosX = p.position + QPointF(delta.x(), 0);
        QPoint gridX = pixelToGrid(newPosX);
        if (isWalkable(gridX.x(), gridX.y()))
            p.position.setX(newPosX.x());

        // Y轴尝试
        QPointF newPosY = p.position + QPointF(0, delta.y());
        QPoint gridY = pixelToGrid(newPosY);
        if (isWalkable(gridY.x(), gridY.y()))
            p.position.setY(newPosY.y());
    }

    // 同步本机playerPos（如果是主机，也移动自己）
    if (players.contains(myPlayerId))
        playerPos = players[myPlayerId].position;
}

void MultiGameWindow::updateBombs()
{
    if (!isHost) return;

    QList<MultiBomb> newBombs;
    for (MultiBomb& bomb : bombList) {
        bomb.timer -= frameTime;
        if (bomb.timer <= 0) {
            explodeBomb(bomb);
        }
        else {
            newBombs.append(bomb);
        }
    }
    bombList = newBombs;
}

void MultiGameWindow::explodeBomb(const MultiBomb& bomb)
{
    // 计算爆炸波及单元格
    QPoint center = pixelToGrid(bomb.pos);
    QList<QPoint> cells;
    cells.append(center);
    int dirs[4][2] = { {-1,0},{1,0},{0,-1},{0,1} };
    for (int d = 0; d < 4; ++d) {
        for (int step = 1; step <= bomb.bombRange; ++step) {
            int row = center.x() + dirs[d][0] * step;
            int col = center.y() + dirs[d][1] * step;
            if (row < 0 || row >= ROWS || col < 0 || col >= COLS) break;
            if (map[row][col] == 1) break; // 墙阻挡
            cells.append(QPoint(row, col));
            if (map[row][col] == 2) {
                map[row][col] = 0; // 砖块摧毁
                break;
            }
        }
    }

    // 伤害判定
    for (const QPoint& cell : cells) {
        for (auto it = players.begin(); it != players.end(); ++it) {
            if (it.value().lives <= 0) continue;
            if (it.key() == bomb.ownerId) continue; // 不伤害自己（可选，可以自己伤害）
            QPoint pGrid = pixelToGrid(it.value().position);
            if (pGrid == cell) {
                it.value().lives--;
                if (it.value().lives < 0) it.value().lives = 0;
                // 如果死亡，移除炸弹放置限制？这里简单处理
            }
        }
    }
}

void MultiGameWindow::checkGameOver()
{
    if (!isHost) return;
    int aliveCount = 0;
    int winnerId = -1;
    for (auto it = players.begin(); it != players.end(); ++it) {
        if (it.value().lives > 0) {
            aliveCount++;
            winnerId = it.key();
        }
    }
    if (aliveCount <= 1) {
        state = defeat;
        gameTimer->stop();
        sendGameOver(winnerId);
        // 显示胜利信息
        if (winnerId != -1 && players.contains(winnerId)) {
            QMessageBox::information(this, "游戏结束", QString("玩家 %1 获胜！").arg(players[winnerId].name));
        }
        else {
            QMessageBox::information(this, "游戏结束", "平局？");
        }
        this->close();
    }
}

// ==================== 网络发送 ====================

void MultiGameWindow::sendInputToHost()
{
    if (isHost) return;
    QByteArray packet;
    QDataStream stream(&packet, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << static_cast<quint16>(MSG_PLAYER_INPUT);

    PlayerInput input;
    input.playerId = myPlayerId;
    input.is_moveup = isMoveUp;
    input.is_movedown = isMoveDown;
    input.is_moveleft = isMoveLeft;
    input.is_moveright = isMoveRight;
    input.is_placeBomb = false; // 我们通过按键发送炸弹
    stream << input;
    socket->writeDatagram(packet, hostAddress, hostPort);
}

void MultiGameWindow::broadcastGameState()
{
    if (!isHost) return;
    GameStateSnapshot snap;
    for (auto it = players.begin(); it != players.end(); ++it) {
        snap.playerIds.append(it.key());
        snap.playerPositions.append(it.value().position);
        snap.playerLives.append(it.value().lives);
    }
    for (const MultiBomb& bomb : bombList) {
        snap.bombPositions.append(bomb.pos);
        snap.bombTimers.append(bomb.timer);
        snap.bombRanges.append(bomb.bombRange);
        snap.bombOwners.append(bomb.ownerId);
    }
    snap.gameRunning = (state == running);
    snap.winnerId = -1;

    QByteArray packet;
    QDataStream stream(&packet, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << static_cast<quint16>(MSG_GAME_STATE) << snap;

    for (auto it = clientEndpoints.begin(); it != clientEndpoints.end(); ++it) {
        socket->writeDatagram(packet, it.value().address, it.value().port);
    }
}

void MultiGameWindow::sendGameOver(int winnerId)
{
    QByteArray packet;
    QDataStream stream(&packet, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << static_cast<quint16>(MSG_GAME_OVER) << winnerId;
    for (auto it = clientEndpoints.begin(); it != clientEndpoints.end(); ++it) {
        socket->writeDatagram(packet, it.value().address, it.value().port);
    }
}

// ==================== 网络接收 ====================

void MultiGameWindow::readNetworkData()
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

        if (isHost) {
            if (msgType == MSG_PLAYER_INPUT) {
                PlayerInput input;
                stream >> input;
                if (players.contains(input.playerId)) {
                    RemotePlayer& p = players[input.playerId];
                    p.isMoveUp = input.is_moveup;
                    p.isMoveDown = input.is_movedown;
                    p.isMoveLeft = input.is_moveleft;
                    p.isMoveRight = input.is_moveright;
                    if (input.is_placeBomb) {
                        // 放置炸弹
                        QPoint grid = pixelToGrid(p.position);
                        int bombCnt = 0;
                        for (const MultiBomb& b : bombList) {
                            if (b.ownerId == input.playerId) bombCnt++;
                        }
                        if (bombCnt < p.maxBombPlace) {
                            MultiBomb bomb;
                            bomb.pos = gridToPixel(grid.x(), grid.y());
                            bomb.timer = 1.5;
                            bomb.bombRange = p.bombRange;
                            bomb.ownerId = input.playerId;
                            bombList.append(bomb);
                        }
                    }
                }
            }
        }
        else { // 客户端
            if (msgType == MSG_GAME_STATE) {
                GameStateSnapshot snap;
                stream >> snap;
                applyGameState(snap);
            }
            else if (msgType == MSG_GAME_OVER) {
                int winnerId;
                stream >> winnerId;
                state = defeat;
                gameTimer->stop();
                if (winnerId != -1 && players.contains(winnerId))
                    QMessageBox::information(this, "游戏结束", QString("玩家 %1 获胜！").arg(players[winnerId].name));
                else
                    QMessageBox::information(this, "游戏结束", "游戏结束");
                this->close();
            }
        }
    }
}

// ==================== 应用状态（客户端） ====================

void MultiGameWindow::applyGameState(const GameStateSnapshot& snap)
{
    if (isHost) return;
    // 更新玩家
    for (int i = 0; i < snap.playerIds.size(); ++i) {
        int id = snap.playerIds[i];
        if (players.contains(id)) {
            players[id].position = snap.playerPositions[i];
            players[id].lives = snap.playerLives[i];
        }
    }
    // 更新炸弹
    bombList.clear();
    for (int i = 0; i < snap.bombPositions.size(); ++i) {
        MultiBomb bomb;
        bomb.pos = snap.bombPositions[i];
        bomb.timer = snap.bombTimers[i];
        bomb.bombRange = snap.bombRanges[i];
        bomb.ownerId = snap.bombOwners[i];
        bombList.append(bomb);
    }
    // 更新本机玩家位置
    if (players.contains(myPlayerId))
        playerPos = players[myPlayerId].position;
    update();
}

// ==================== 游戏主循环 ====================

void MultiGameWindow::updateGame()
{
    if (state != running) return;

    if (isHost) {
        // 主机更新逻辑
        handleMovement();
        // 处理炸弹放置（客户端已发送输入，已处理）
        updateBombs();
        checkGameOver();
        broadcastGameState();
    }
    else {
        // 客户端发送输入
        sendInputToHost();
        // 不需要移动逻辑，状态由主机同步
    }
    update();
}

// ==================== 绘制 ====================

void MultiGameWindow::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::white);
    if (cellSize <= 0) updateCellSize();

    // 绘制地图
    for (int i = 0; i < ROWS; ++i) {
        for (int j = 0; j < COLS; ++j) {
            QRect rect(j * cellSize, i * cellSize, cellSize, cellSize);
            switch (map[i][j]) {
            case 1: painter.setBrush(Qt::darkGray); break;
            case 2: painter.setBrush(Qt::lightGray); break;
            default: painter.setBrush(Qt::gray); break;
            }
            painter.drawRect(rect);
            if (showGrid) {
                painter.setPen(Qt::black);
                painter.drawRect(rect);
            }
        }
    }

    // 炸弹
    if (useImages) {
        double imgSize = cellSize * 0.6;
        for (const MultiBomb& bomb : bombList) {
            QRectF target(bomb.pos.x() - imgSize / 2, bomb.pos.y() - imgSize / 2, imgSize, imgSize);
            painter.drawPixmap(target, bombImg, QRectF(bombImg.rect()));
        }
    }
    else {
        painter.setBrush(Qt::darkCyan);
        double radius = cellSize * 0.3;
        for (const MultiBomb& bomb : bombList) {
            painter.drawEllipse(bomb.pos, radius, radius);
        }
    }

    // 玩家
    for (auto it = players.begin(); it != players.end(); ++it) {
        if (it.value().lives <= 0) continue;
        QColor color = getPlayerColor(it.key());
        double imgSize = cellSize * 0.6;
        if (useImages && it.key() == myPlayerId) {
            QRectF target(it.value().position.x() - imgSize / 2, it.value().position.y() - imgSize / 2, imgSize, imgSize);
            painter.drawPixmap(target, playerImg, QRectF(playerImg.rect()));
        }
        else {
            painter.setBrush(color);
            painter.setPen(Qt::NoPen);
            double radius = cellSize * 0.3;
            painter.drawEllipse(it.value().position, radius, radius);
        }

        // 血条
        double barWidth = cellSize * 0.6;
        double barHeight = cellSize * 0.1;
        double barX = it.value().position.x() - barWidth / 2;
        double barY = it.value().position.y() - cellSize * 0.4;
        painter.setBrush(Qt::white);
        painter.drawRect(QRectF(barX, barY, barWidth, barHeight));
        painter.setBrush(Qt::green);
        double hpWidth = barWidth * it.value().lives / it.value().maxLives;
        painter.drawRect(QRectF(barX, barY, hpWidth, barHeight));

        // 名字
        painter.setPen(Qt::white);
        painter.setFont(QFont("Arial", 8));
        QRect textRect(it.value().position.x() - 30, it.value().position.y() + cellSize * 0.3, 60, 15);
        painter.drawText(textRect, Qt::AlignCenter, it.value().name);
        painter.setPen(Qt::NoPen);
    }
}

// ==================== 事件处理 ====================

void MultiGameWindow::keyPressEvent(QKeyEvent* event)
{
    if (state != running) return;

    switch (event->key()) {
    case Qt::Key_W: isMoveUp = true; break;
    case Qt::Key_S: isMoveDown = true; break;
    case Qt::Key_A: isMoveLeft = true; break;
    case Qt::Key_D: isMoveRight = true; break;
    case Qt::Key_Space:
        // 放置炸弹
        if (isHost) {
            // 直接放置
            QPoint grid = pixelToGrid(playerPos);
            int bombCnt = 0;
            for (const MultiBomb& b : bombList) {
                if (b.ownerId == myPlayerId) bombCnt++;
            }
            if (bombCnt < maxBombPlace) {
                MultiBomb bomb;
                bomb.pos = gridToPixel(grid.x(), grid.y());
                bomb.timer = 1.5;
                bomb.bombRange = bombRange;
                bomb.ownerId = myPlayerId;
                bombList.append(bomb);
            }
        }
        else {
            // 发送输入（包含炸弹请求）
            QByteArray packet;
            QDataStream stream(&packet, QIODevice::WriteOnly);
            stream.setVersion(QDataStream::Qt_6_0);
            stream << static_cast<quint16>(MSG_PLAYER_INPUT);
            PlayerInput input;
            input.playerId = myPlayerId;
            input.is_moveup = isMoveUp;
            input.is_movedown = isMoveDown;
            input.is_moveleft = isMoveLeft;
            input.is_moveright = isMoveRight;
            input.is_placeBomb = true;
            stream << input;
            socket->writeDatagram(packet, hostAddress, hostPort);
        }
        break;
    }
}

void MultiGameWindow::keyReleaseEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_W: isMoveUp = false; break;
    case Qt::Key_S: isMoveDown = false; break;
    case Qt::Key_A: isMoveLeft = false; break;
    case Qt::Key_D: isMoveRight = false; break;
    }
}

void MultiGameWindow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateCellSize();
    recenterAll();
    update();
}

void MultiGameWindow::closeEvent(QCloseEvent* event)
{
    gameTimer->stop();
    if (isHost) {
        // 通知客户端游戏结束（如果尚未结束）
        sendGameOver(-1);
    }
    QWidget::closeEvent(event);
}

QColor MultiGameWindow::getPlayerColor(int playerId)
{
    switch (playerId % 4) {
    case 0: return QColor(52, 152, 219);
    case 1: return QColor(231, 76, 60);
    case 2: return QColor(46, 204, 113);
    case 3: return QColor(241, 196, 15);
    default: return QColor(155, 89, 182);
    }
}