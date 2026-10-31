#include "MultiGameScreen.h"
#include "GameCanvas.h"
#include "GameHud.h"
#include "ScreenRouter.h"
#include "NetSession.h"
#include "SettingsManager.h"
#include "FileManager.h"
#include "ItemDatabase.h"
#include "ModManager.h"
#include "NativeModHost.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QKeyEvent>
#include <QTimer>
#include <QMessageBox>
#include <QUdpSocket>
#include <QDataStream>

//隔几个逻辑步广播/上报一次
//逻辑是固定 60Hz，隔 2 步 = 30Hz：局域网足够顺，也不会把 UDP 打成风暴
static const int kNetEverySteps = 2;

MultiGameScreen::MultiGameScreen(QWidget* parent) : ScreenBase(parent)
{
	//画面与 HUD（和单人界面完全同一套视图，联机只是"快照里有多个玩家"）
	m_canvas = new GameCanvas(this);
	m_canvas->setShowGrid(SettingsManager::instance().showGrid());

	m_hud = new GameHud(this);

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(m_hud);
	layout->addWidget(m_canvas, 1);
	setLayout(layout);

	//引擎与帧循环（本屏常驻复用，每次进入用 beginMatch 开新局）
	m_engine = std::make_unique<bp::MultiGameEngine>(bp::GameConfig());
	m_loop = std::make_unique<bp::GameLoop>(m_engine.get());

	//视图只持有两个"最小只读接口"：快照来源 + 插值来源（和单人界面完全一样）
	m_canvas->setSnapshotSource(m_engine.get());
	m_canvas->setInterpolationSource(m_loop.get());
	m_hud->setSnapshotSource(m_engine.get());

	m_engine->setStateChangedCallback([this]() { m_canvas->update(); });
	m_engine->setLevelFinishedCallback([this](bool victory) { onMatchFinished(victory); });

	m_loop->setSteppedCallback([this](int advancedSteps) { onEngineStepped(advancedSteps); });

	//客户端心跳只接线一次：以前每次 onEnter 都 connect 一个 lambda，
	//多打几局之后同一个 timer 会触发 N 次（输入包重复发送）。
	m_clientTimer.setInterval(50);
	m_clientTimer.setTimerType(Qt::PreciseTimer);
	connect(&m_clientTimer, &QTimer::timeout, this, &MultiGameScreen::onClientHeartbeat);

	setupPauseOverlay();
}

MultiGameScreen::~MultiGameScreen()
{
	//先停循环、断开引擎回调，再销毁，避免回调访问已释放对象
	if (m_loop) { m_loop->stop(); }
	if (m_engine)
	{
		m_engine->setStateChangedCallback(nullptr);
		m_engine->setLevelFinishedCallback(nullptr);
	}
}

//暂停遮罩：盖在画布上的一层（不进入窗口栈，所以不会触发 onEnter/onExit）
void MultiGameScreen::setupPauseOverlay()
{
	m_pauseOverlay = new QWidget(this);
	m_pauseOverlay->setObjectName("pauseOverlay");
	m_pauseOverlay->setAttribute(Qt::WA_StyledBackground, true);
	m_pauseOverlay->hide();

	QVBoxLayout* outer = new QVBoxLayout(m_pauseOverlay);
	outer->setContentsMargins(24, 24, 24, 24);
	outer->addStretch(1);

	QWidget* panel = new QWidget(m_pauseOverlay);
	panel->setObjectName("pausePanel");
	panel->setAttribute(Qt::WA_StyledBackground, true);
	panel->setMaximumWidth(460);

	QVBoxLayout* panelLayout = new QVBoxLayout(panel);
	panelLayout->setContentsMargins(28, 24, 28, 24);
	panelLayout->setSpacing(12);

	m_pauseTitle = new QLabel(QStringLiteral("已暂停"), panel);
	m_pauseTitle->setObjectName("pauseTitle");
	m_pauseTitle->setAlignment(Qt::AlignCenter);

	m_pauseHint = new QLabel(
		QStringLiteral("按 Esc 或点「继续游戏」回到对局。\n返回大厅会结束本局，并让双方都回到大厅。"),
		panel);
	m_pauseHint->setObjectName("pauseHint");
	m_pauseHint->setWordWrap(true);
	m_pauseHint->setAlignment(Qt::AlignCenter);

	QHBoxLayout* buttonRow = new QHBoxLayout();
	buttonRow->setSpacing(12);
	m_resumeBtn = new QPushButton(QStringLiteral("继续游戏"), panel);
	m_resumeBtn->setObjectName("primaryBtn");
	m_leaveBtn = new QPushButton(QStringLiteral("返回大厅"), panel);
	m_leaveBtn->setObjectName("accentBtn");
	buttonRow->addWidget(m_resumeBtn);
	buttonRow->addWidget(m_leaveBtn);

	panelLayout->addWidget(m_pauseTitle);
	panelLayout->addWidget(m_pauseHint);
	panelLayout->addSpacing(4);
	panelLayout->addLayout(buttonRow);

	QHBoxLayout* center = new QHBoxLayout();
	center->addStretch(1);
	center->addWidget(panel);
	center->addStretch(1);
	outer->addLayout(center);
	outer->addStretch(1);

	connect(m_resumeBtn, &QPushButton::clicked, this, [this]() { resumeMatch(); });
	connect(m_leaveBtn, &QPushButton::clicked, this, &MultiGameScreen::confirmLeaveMatch);
}

void MultiGameScreen::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	if (m_pauseOverlay) { m_pauseOverlay->setGeometry(rect()); }
}

//进入界面：从 NetSession 读取本次对局的全部上下文，然后开局
void MultiGameScreen::onEnter(const QVariantMap& args)
{
	Q_UNUSED(args);

	NetSession& session = NetSession::instance();

	isHost = session.isHost();
	myPlayerId = session.myPlayerId();
	hostAddress = session.hostAddress();
	hostPort = session.hostPort();
	clientEndpoints = session.clientEndpoints();
	socket = session.socket();

	m_input = bp::InputState();
	m_finished = false;
	m_paused = false;
	m_remotePaused = false;
	m_netStepCounter = 0;
	if (m_pauseOverlay) { m_pauseOverlay->hide(); }
	if (m_pauseTitle) { m_pauseTitle->setText(QStringLiteral("已暂停")); }

	m_canvas->setShowGrid(SettingsManager::instance().showGrid());

	//数值配置必须先于 addPlayer：玩家的初始血量/火力取自配置，
	//顺序反了会先按默认值建人、再改配置（模组改过数值时会不一致）。
	m_engine->setConfig(ModManager::instance().gameConfig());

	//参与者：按 id 升序灌进引擎（QMap 本身有序，两端顺序因此一致）
	m_engine->clearPlayers();
	const QMap<int, QString> names = session.playerNames();
	for (auto it = names.constBegin(); it != names.constEnd(); ++it)
	{
		m_engine->addPlayer(it.key(), it.value());
	}
	//兜底：万一玩家表是空的（例如直接从调试入口进来），至少要有自己
	if (m_engine->players().isEmpty())
	{
		m_engine->addPlayer(myPlayerId, QStringLiteral("我"));
	}

	m_engine->setLocalPlayerId(myPlayerId);
	m_engine->setAuthoritative(isHost);

	//对局种子：大厅广播的那个，两端必须一致，否则地图都不一样
	m_engine->setRandomSeed(static_cast<qint64>(session.matchSeed()));

	//道具数据驱动：掉落权重与"道具名 -> 效果"都取自 Data/itemInfo.json
	//放在 onEnter 而不是构造函数：界面是启动时预创建的，那时道具数据还没加载完
	m_engine->setDropTable(ItemDatabase::instance().dropTable());
	m_engine->setEffectResolver([](const QString& name) {
		return ItemDatabase::instance().effectForItem(name);
	});

	m_engine->beginMatch();
	m_hud->refresh();

	//原生模组**只跑在主机**（契约 §7.5）：客户端是镜像，只按快照插值渲染、不跑规则，
	//所以它连逻辑回调都不该有 —— 否则两端规则分叉，表现就是"莫名其妙不同步"
	NativeModHost::instance().setActiveEngine(isHost ? m_engine.get() : nullptr);

	//网络数据监听（socket 是 NetSession 的那个，这里只接 readyRead）
	if (readyReadConnection) { disconnect(readyReadConnection); readyReadConnection = QMetaObject::Connection(); }
	if (socket)
	{
		readyReadConnection = connect(socket, &QUdpSocket::readyRead,
									  this, &MultiGameScreen::readNetworkData);
	}

	// 诊断：如果走到这里 socket 还是没绑定，说明大厅 -> 对战的交接被破坏了。
	// 以前正是 LobbyScreen::onExit 无条件 close() 造成"主机能玩、客户端全静止"。
	if (!socket || !NetSession::instance().isOpen())
	{
		FileManager::instance().log("game",
			QStringLiteral("联机对局警告：socket 未绑定，收发都会失败（大厅交接问题）"));
	}

	m_loop->start();

	//客户端心跳：每 50ms 上报一次输入并强制重画。
	//不依赖引擎逻辑步 —— 客户端是镜像，本地没有"真实推进"，靠逻辑步驱动上报会直接失联。
	if (!isHost) { m_clientTimer.start(); }
	else { m_clientTimer.stop(); }

	FileManager::instance().log("game", QStringLiteral("联机对战开始：主机=%1 玩家=%2 种子=%3")
		.arg(isHost ? QStringLiteral("是") : QStringLiteral("否"))
		.arg(m_engine->players().size())
		.arg(session.matchSeed()));
}

//离开界面：停循环、断开网络监听、必要时通知客户端
void MultiGameScreen::onExit()
{
	m_clientTimer.stop();
	m_loop->stop();

	m_paused = false;
	m_remotePaused = false;
	if (m_pauseOverlay) { m_pauseOverlay->hide(); }

	//归还引擎：离开对局后宿主不能再操作它（模组的调用一律得到"时机不对"）
	NativeModHost::instance().setActiveEngine(nullptr);

	if (readyReadConnection) { disconnect(readyReadConnection); readyReadConnection = QMetaObject::Connection(); }

	//主机中途退出（窗口关闭 / 退出程序）：得告诉客户端"这局不打了"，
	//否则客户端会一直等主机状态，界面像卡住一样。
	//正常"暂停菜单 -> 返回大厅"会先把 m_finished 置 true，不会重复发。
	if (isHost && socket && !m_finished) { sendGameOver(-1); }
}

//Esc：打开/关闭暂停菜单。联机中途直接退局不符合直觉，也容易误触。
bool MultiGameScreen::handleBack()
{
	if (m_finished) { return true; }

	if (m_paused)
	{
		// 主机暂停时，客户端不能自行解除；只能等主机继续或点「返回大厅」
		if (m_remotePaused) { return true; }
		resumeMatch();
	}
	else
	{
		pauseMatch();
	}
	return true;
}

// ---------- 暂停菜单 ----------

void MultiGameScreen::pauseMatch(bool remote)
{
	if (m_finished || m_paused) { return; }

	m_paused = true;
	m_remotePaused = remote;

	//先把"按住的键"清掉：暂停期间收不到 KeyRelease，
	//不清的话继续游戏时角色会一直朝一个方向走。
	m_input = bp::InputState();
	if (isHost) { syncLocalInput(); }
	else if (!remote) { sendInputToHost(); }

	m_loop->pause();
	m_clientTimer.stop();

	if (!remote && isHost) { sendPauseState(true); }

	if (m_pauseTitle) { m_pauseTitle->setText(remote ? QStringLiteral("主机已暂停") : QStringLiteral("已暂停")); }
	if (m_pauseHint)
	{
		m_pauseHint->setText(remote
			? QStringLiteral("主机暂停了本局，等待主机继续…\n你也可以直接返回大厅。")
			: QStringLiteral("按 Esc 或点「继续游戏」回到对局。\n返回大厅会结束本局，并让双方都回到大厅。"));
	}
	if (m_resumeBtn) { m_resumeBtn->setVisible(!remote); }

	if (m_pauseOverlay)
	{
		m_pauseOverlay->setGeometry(rect());
		m_pauseOverlay->raise();
		m_pauseOverlay->show();
	}

	m_canvas->update();
}

void MultiGameScreen::resumeMatch(bool remote)
{
	if (!m_paused) { return; }

	m_paused = false;
	m_remotePaused = false;
	if (m_pauseOverlay) { m_pauseOverlay->hide(); }

	m_loop->resume();
	m_clientTimer.stop();

	if (!remote && isHost) { sendPauseState(false); }
	if (!isHost && !m_finished)
	{
		m_clientTimer.start();
		if (!remote) { sendInputToHost(); }
	}

	m_input = bp::InputState();
	if (isHost) { syncLocalInput(); }

	m_canvas->update();
	m_hud->refresh();
}

void MultiGameScreen::sendPauseState(bool paused)
{
	if (!isHost || !socket) { return; }

	QByteArray packet;
	QDataStream stream(&packet, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_6_0);
	stream << static_cast<quint16>(MSG_GAME_PAUSE) << paused;

	for (auto it = clientEndpoints.constBegin(); it != clientEndpoints.constEnd(); ++it)
	{
		socket->writeDatagram(packet, it.value().address, it.value().port);
	}
}

void MultiGameScreen::confirmLeaveMatch()
{
	if (m_finished)
	{
		leaveMatch();
		return;
	}

	const QMessageBox::StandardButton answer = QMessageBox::question(
		this,
		QStringLiteral("返回大厅"),
		QStringLiteral("确定要离开这一局吗？\n离开后本局立即结束，双方都会回到大厅。"),
		QMessageBox::Yes | QMessageBox::No,
		QMessageBox::No);

	if (answer != QMessageBox::Yes) { return; }

	leaveMatch();
}

void MultiGameScreen::leaveMatch()
{
	m_loop->stop();
	m_clientTimer.stop();
	m_paused = false;
	m_remotePaused = false;
	if (m_pauseOverlay) { m_pauseOverlay->hide(); }

	// 主机离开：告诉客户端这局结束；客户端离开：让主机把留下的玩家判为胜者。
	// 两条都必须赶在 m_finished=true 之前发，否则 onExit 会以为已经收尾过。
	if (socket && !m_finished)
	{
		if (isHost) { sendGameOver(-1); }
		else
		{
			QByteArray packet;
			QDataStream stream(&packet, QIODevice::WriteOnly);
			stream.setVersion(QDataStream::Qt_6_0);
			stream << static_cast<quint16>(MSG_GAME_OVER) << kOpponentLeftWinnerId;
			socket->writeDatagram(packet, hostAddress, hostPort);
		}
	}

	m_finished = true;
	ScreenRouter::instance().replace(ScreenId::Lobby);
}

void MultiGameScreen::onClientHeartbeat()
{
	if (m_finished || m_paused) { return; }

	sendInputToHost();
	m_canvas->update();
	m_hud->refresh();
}

// ---------- 每逻辑步 ----------

void MultiGameScreen::onEngineStepped(int advancedSteps)
{
	if (m_finished || m_paused) { return; }

	m_netStepCounter += advancedSteps;

	//原生模组只跑主机：每个逻辑步调一次（dt 恒为 1/60，与引擎的固定步长一致）。
	//按"推进了几步"循环调用，而不是把 dt 乘上去 —— 模组看到的时间轴必须与单机完全一样
	if (isHost)
	{
		for (int i = 0; i < advancedSteps; ++i)
		{
			NativeModHost::instance().onEngineStepped(1.0 / 60.0);
		}
	}

	if (isHost)
	{
		//主机：自己的输入已经按键时立即交给引擎了，这里只负责按频率广播状态
		if (m_netStepCounter >= kNetEverySteps)
		{
			m_netStepCounter = 0;
			broadcastGameState();
		}
	}
	else if (m_netStepCounter >= kNetEverySteps)
	{
		//客户端：上报输入，位置等主机回包
		m_netStepCounter = 0;
		sendInputToHost();
	}

	m_hud->refresh();
}

//把按键状态交给引擎：主机直接生效；客户端留给上报
void MultiGameScreen::syncLocalInput()
{
	if (isHost) { m_engine->submitRemoteInput(myPlayerId, m_input); }
}

// ---------- 网络：主机 -> 客户端 ----------

GameStateSnapshot MultiGameScreen::makeNetworkSnapshot() const
{
	GameStateSnapshot snap;

	for (const bp::MultiPlayer& player : m_engine->players())
	{
		snap.playerIds.append(player.id);
		//连续格子坐标：(x = 列, y = 行)，客户端收到后还原成 GridPosF
		snap.playerPositions.append(QPointF(player.pos.col(), player.pos.row()));
		snap.playerLives.append(player.lives);
		snap.playerMaxLives.append(player.maxLives);
		snap.playerNames.append(player.name);
		snap.playerRanges.append(player.bombRange);
		snap.playerMaxBombs.append(player.maxBombs);
	}

	for (const bp::Bomb& bomb : m_engine->bombs())
	{
		snap.bombPositions.append(QPointF(bomb.pos.col(), bomb.pos.row()));
		snap.bombOwners.append(bomb.ownerId);
		snap.bombTimers.append(static_cast<float>(bomb.fuse));
		snap.bombFuseTotals.append(static_cast<float>(bomb.fuseTotal));
		snap.bombRanges.append(bomb.range);
	}

	for (const bp::DropItem& drop : m_engine->drops())
	{
		snap.dropPositions.append(QPointF(drop.pos.col(), drop.pos.row()));
		snap.dropNames.append(drop.name);
	}

	snap.gameRunning = m_engine->isRunning();
	snap.winnerId = m_engine->winnerId();
	return snap;
}

void MultiGameScreen::broadcastGameState()
{
	if (!isHost || !socket) { return; }

	const GameStateSnapshot snap = makeNetworkSnapshot();

	QByteArray packet;
	QDataStream stream(&packet, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_6_0);
	stream << static_cast<quint16>(MSG_GAME_STATE) << snap;

	for (auto it = clientEndpoints.constBegin(); it != clientEndpoints.constEnd(); ++it)
	{
		socket->writeDatagram(packet, it.value().address, it.value().port);
	}
}

void MultiGameScreen::sendGameOver(int winnerId)
{
	if (!socket) { return; }

	QByteArray packet;
	QDataStream stream(&packet, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_6_0);
	stream << static_cast<quint16>(MSG_GAME_OVER) << winnerId;

	for (auto it = clientEndpoints.constBegin(); it != clientEndpoints.constEnd(); ++it)
	{
		socket->writeDatagram(packet, it.value().address, it.value().port);
	}
}

// ---------- 网络：客户端 -> 主机 ----------

void MultiGameScreen::sendInputToHost()
{
	if (isHost || !socket) { return; }

	QByteArray packet;
	QDataStream stream(&packet, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_6_0);
	stream << static_cast<quint16>(MSG_PLAYER_INPUT);

	PlayerInput input;
	input.playerId = myPlayerId;
	input.is_moveup = m_input.up;
	input.is_movedown = m_input.down;
	input.is_moveleft = m_input.left;
	input.is_moveright = m_input.right;
	input.is_placeBomb = m_input.placeBomb;
	stream << input;

	socket->writeDatagram(packet, hostAddress, hostPort);
}

// ---------- 网络：收 ----------

void MultiGameScreen::readNetworkData()
{
	if (!socket) { return; }

	while (socket->hasPendingDatagrams())
	{
		QByteArray datagram;
		datagram.resize(static_cast<int>(socket->pendingDatagramSize()));

		QHostAddress sender;
		quint16 senderPort = 0;
		socket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);

		QDataStream stream(datagram);
		stream.setVersion(QDataStream::Qt_6_0);

		quint16 msgType = 0;
		stream >> msgType;

		if (isHost)
		{
			if (msgType == MSG_PLAYER_INPUT)
			{
				PlayerInput input;
				stream >> input;

				bp::InputState state;
				state.up = input.is_moveup;
				state.down = input.is_movedown;
				state.left = input.is_moveleft;
				state.right = input.is_moveright;
				state.placeBomb = input.is_placeBomb;

				m_engine->submitRemoteInput(input.playerId, state);
			}
			else if (msgType == MSG_GAME_OVER)
			{
				// 客户端按「返回大厅」时发来的特殊结束消息：留下的玩家（主机）获胜。
				int winnerId = -1;
				stream >> winnerId;
				if (winnerId == kOpponentLeftWinnerId && !m_finished)
				{
					m_engine->declareWinner(myPlayerId);
				}
			}
			//其它（大厅广播等）忽略
		}
		else
		{
			if (msgType == MSG_GAME_STATE)
			{
				GameStateSnapshot snap;
				stream >> snap;
				applyNetworkSnapshot(snap);
			}
			else if (msgType == MSG_GAME_OVER)
			{
				int winnerId = -1;
				stream >> winnerId;

				//客户端不自己判定胜负：主机说结束了就是结束了
				m_engine->declareWinner(winnerId);
			}
			else if (msgType == MSG_GAME_PAUSE)
			{
				bool paused = false;
				stream >> paused;
				if (paused) { pauseMatch(true); }
				else { resumeMatch(true); }
			}
		}
	}
}

//网络快照 -> 引擎（客户端）
void MultiGameScreen::applyNetworkSnapshot(const GameStateSnapshot& snap)
{
	QList<bp::GridPosF> positions;
	positions.reserve(snap.playerPositions.size());
	for (const QPointF& p : snap.playerPositions)
	{
		positions.append(bp::posFromRowCol(p.y(), p.x()));
	}

	QList<bp::Bomb> bombs;
	bombs.reserve(snap.bombPositions.size());
	for (int i = 0; i < snap.bombPositions.size(); ++i)
	{
		bp::Bomb bomb;
		bomb.pos = bp::posFromRowCol(snap.bombPositions[i].y(), snap.bombPositions[i].x());
		bomb.owner = bp::BombOwner::Player;
		bomb.ownerId = (i < snap.bombOwners.size()) ? snap.bombOwners[i] : 0;
		bomb.range = (i < snap.bombRanges.size()) ? snap.bombRanges[i] : m_engine->config().playerBombRange;

		const double remaining = (i < snap.bombTimers.size()) ? snap.bombTimers[i] : 0.0;
		const double total = (i < snap.bombFuseTotals.size()) ? snap.bombFuseTotals[i] : remaining;
		bomb.fuse = remaining;
		bomb.fuseTotal = qMax(total, 0.0001);

		bombs.append(bomb);
	}

	QList<bp::DropItem> drops;
	drops.reserve(snap.dropPositions.size());
	for (int i = 0; i < snap.dropPositions.size(); ++i)
	{
		bp::DropItem drop;
		drop.pos = bp::posFromRowCol(snap.dropPositions[i].y(), snap.dropPositions[i].x());
		drop.name = (i < snap.dropNames.size()) ? snap.dropNames[i] : QString();
		drops.append(drop);
	}

	m_engine->applyRemotePlayers(snap.playerIds, positions, snap.playerLives,
								 snap.playerRanges, snap.playerMaxBombs, snap.playerMaxLives);
	m_engine->applyRemoteBombs(bombs);
	m_engine->applyRemoteDrops(drops);
	//客户端是靠快照前进的：本地引擎不 tick，所以这里必须主动重画，否则画面就"静止"了
	m_canvas->update();
	m_hud->refresh();

	//UDP 是"尽力而为"：万一"游戏结束"那条消息正好丢了，客户端靠状态快照也能收尾，
	//不至于永远停在"等主机"的画面上
	if (snap.winnerId >= 0 && m_engine->isRunning())
	{
		m_engine->declareWinner(snap.winnerId);
	}

	m_canvas->update();
}

// ---------- 键盘 ----------

void MultiGameScreen::handleKeyPress(QKeyEvent* event)
{
	if (m_finished) { return; }

	if (m_paused)
	{
		// 暂停菜单里回车 = 继续（鼠标点按钮仍然可用；AppHost 会吃掉按键，所以这里显式处理）。
		// 主机暂停时客户端不能自行继续。
		if (!m_remotePaused
			&& (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter))
		{
			resumeMatch();
		}
		return;
	}

	switch (event->key())
	{
	case Qt::Key_W: m_input.up = true; break;
	case Qt::Key_S: m_input.down = true; break;
	case Qt::Key_A: m_input.left = true; break;
	case Qt::Key_D: m_input.right = true; break;
	case Qt::Key_Space: m_input.placeBomb = true; break;
	default: break;
	}

	syncLocalInput();
}

void MultiGameScreen::handleKeyRelease(QKeyEvent* event)
{
	if (m_finished || m_paused) { return; }

	switch (event->key())
	{
	case Qt::Key_W: m_input.up = false; break;
	case Qt::Key_S: m_input.down = false; break;
	case Qt::Key_A: m_input.left = false; break;
	case Qt::Key_D: m_input.right = false; break;
	case Qt::Key_Space: m_input.placeBomb = false; break;
	default: break;
	}

	syncLocalInput();
}

// ---------- 收尾 ----------

void MultiGameScreen::onMatchFinished(bool victory)
{
	Q_UNUSED(victory);

	if (m_finished) { return; }

	m_loop->stop();
	m_clientTimer.stop();
	m_paused = false;
	m_remotePaused = false;
	if (m_pauseOverlay) { m_pauseOverlay->hide(); }
	m_finished = true;

	//主机把"谁赢了"发给客户端
	if (isHost) { sendGameOver(m_engine->winnerId()); }

	//为什么用 singleShot 而不是直接弹框：引擎是在 tick 里回调的，
	//在 tick 中间弹模态框会把这一逻辑步卡住（QMessageBox 会开嵌套事件循环），
	//推迟到本轮事件处理结束后再弹，逻辑步就不会被撕开
	QTimer::singleShot(0, this, &MultiGameScreen::showMatchResult);
}

void MultiGameScreen::showMatchResult()
{
	//已经离开界面（例如玩家抢先按了 Esc / 返回大厅）就不再弹框
	if (!isVisible()) { return; }

	if (m_pauseOverlay) { m_pauseOverlay->hide(); }

	const int winnerId = m_engine->winnerId();

	QString text;
	if (winnerId < 0) { text = QStringLiteral("平局"); }
	else if (winnerId == myPlayerId) { text = QStringLiteral("你赢了！"); }
	else { text = QStringLiteral("%1 获胜").arg(m_engine->playerName(winnerId)); }

	QMessageBox::information(this, QStringLiteral("对局结束"), text);

	//同"返回大厅"：打完一局回大厅，可以立刻再来一局
	ScreenRouter::instance().replace(ScreenId::Lobby);
}
