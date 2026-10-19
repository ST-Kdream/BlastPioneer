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
	m_netStepCounter = 0;

	m_canvas->setShowGrid(SettingsManager::instance().showGrid());

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

	//数值配置：默认值 + 所有生效 mod 的玩法覆盖（两端各算一次，结果一致）
	m_engine->setConfig(ModManager::instance().gameConfig());

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
	if (socket)
	{
		readyReadConnection = connect(socket, &QUdpSocket::readyRead,
									  this, &MultiGameScreen::readNetworkData);
	}

	m_loop->start();

	FileManager::instance().log("game", QStringLiteral("联机对战开始：主机=%1 玩家=%2 种子=%3")
		.arg(isHost ? QStringLiteral("是") : QStringLiteral("否"))
		.arg(m_engine->players().size())
		.arg(session.matchSeed()));
}

//离开界面：停循环、断开网络监听、必要时通知客户端
void MultiGameScreen::onExit()
{
	m_loop->stop();

	//归还引擎：离开对局后宿主不能再操作它（模组的调用一律得到"时机不对"）
	NativeModHost::instance().setActiveEngine(nullptr);

	if (readyReadConnection) { disconnect(readyReadConnection); readyReadConnection = QMetaObject::Connection(); }

	//主机中途退出（按 Esc 或退出程序）：得告诉客户端"这局不打了"，
	//否则客户端会一直等主机状态，界面像卡住一样
	if (isHost && socket && !m_finished) { sendGameOver(-1); }
}

//Esc：通知对手并回大厅
bool MultiGameScreen::handleBack()
{
	m_loop->stop();

	if (isHost && socket && !m_finished) { sendGameOver(-1); }
	m_finished = true;

	//回大厅而不是"回上一屏"：对战界面是 replace 上来的，栈里下面是主页，
	//直接 pop 会回主页；而这一局的联机会话既然结束，玩家最需要的是"大厅里再来一局"
	//（大厅的 onEnter 会重置 NetSession 并重新绑定端口）
	ScreenRouter::instance().replace(ScreenId::Lobby);
	return true;
}

// ---------- 每逻辑步 ----------

void MultiGameScreen::onEngineStepped(int advancedSteps)
{
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
			//主机只关心客户端输入；其它（大厅广播等）忽略
			if (msgType != MSG_PLAYER_INPUT) { continue; }

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

	m_engine->applyRemotePlayers(snap.playerIds, positions, snap.playerLives);
	m_engine->applyRemoteBombs(bombs);
	m_engine->applyRemoteDrops(drops);

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

	m_loop->stop();
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
	//已经离开界面（例如玩家抢先按了 Esc）就不再弹框
	if (!isVisible()) { return; }

	const int winnerId = m_engine->winnerId();

	QString text;
	if (winnerId < 0) { text = QStringLiteral("平局"); }
	else if (winnerId == myPlayerId) { text = QStringLiteral("你赢了！"); }
	else { text = QStringLiteral("%1 获胜").arg(m_engine->playerName(winnerId)); }

	QMessageBox::information(this, QStringLiteral("对局结束"), text);

	//同 handleBack：打完一局回大厅，可以立刻再来一局
	ScreenRouter::instance().replace(ScreenId::Lobby);
}
