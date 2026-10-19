#include "SingleGameScreen.h"
#include "GameCanvas.h"
#include "GameHud.h"
#include "BagScreen.h"
#include "ScreenRouter.h"
#include "PlayerProfile.h"
#include "SettingsManager.h"
#include "FileManager.h"
#include "ItemDatabase.h"
#include "NativeModHost.h"
#include "LevelDatabase.h"
#include "ModManager.h"

#include <QVBoxLayout>
#include <QApplication>
#include <QKeyEvent>
#include <QMessageBox>

SingleGameScreen::SingleGameScreen(QWidget* parent) : ScreenBase(parent)
{
	//画面与 HUD
	m_canvas = new GameCanvas(this);
	m_canvas->setShowGrid(SettingsManager::instance().showGrid());

	m_hud = new GameHud(this);

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(m_hud);
	layout->addWidget(m_canvas, 1);
	setLayout(layout);

	//引擎与帧循环（本屏常驻复用，每次进入用 startLevel 开新局）
	bp::GameConfig config;
	m_engine = std::make_unique<bp::SingleGameEngine>(config);
	m_loop = std::make_unique<bp::GameLoop>(m_engine.get());

	//视图只持有两个"最小只读接口"：快照来源 + 插值来源
	//于是"界面能不能改游戏状态"在类型层面就没有答案（它拿不到那些方法）
	m_canvas->setSnapshotSource(m_engine.get());
	m_canvas->setInterpolationSource(m_loop.get());
	m_hud->setSnapshotSource(m_engine.get());

	//引擎每步推进后刷新画面与 HUD
	m_engine->setStateChangedCallback([this]() { m_canvas->update(); });

	//本局结束（胜利/失败）交给界面做结算与提示
	m_engine->setLevelFinishedCallback([this](bool victory) { onLevelFinished(victory); });

	//帧循环每推进一步就刷新 HUD（HUD 是纯展示，跟着逻辑走即可）
	m_loop->setSteppedCallback([this](int) { onEngineStepped(); });
}

SingleGameScreen::~SingleGameScreen()
{
	//先停循环、断开引擎回调，再销毁，避免回调访问已释放对象
	if (m_loop) { m_loop->stop(); }
	if (m_engine)
	{
		m_engine->setStateChangedCallback(nullptr);
		m_engine->setLevelFinishedCallback(nullptr);
	}
}

QString SingleGameScreen::displayTitle() const
{
	//模组模式没有"第几关"：标题就是模式名（它自己决定内部怎么分阶段）
	if (!m_modeKey.isEmpty())
	{
		return m_modeName.isEmpty() ? QStringLiteral("模组模式") : m_modeName;
	}
	return QStringLiteral("关卡-%1").arg(m_level);
}

//进入界面：开一局（普通关卡，或者一个模组模式）
void SingleGameScreen::onEnter(const QVariantMap& args)
{
	//背包覆盖层（第一次进入时创建，之后复用）
	if (!m_bag)
	{
		m_bag = new BagScreen(this);
		m_bag->setGameScreen(this);
		//换成"遮罩"这套样式：同一个 BagScreen 类，正式页面用 #bagPanel（不透明），
		//游戏内覆盖层用 #bagOverlay（半透明，让玩家还能隐约看到游戏画面）
		m_bag->setObjectName(QStringLiteral("bagOverlay"));
		m_bag->hide();
		connect(m_bag, &BagScreen::requestBack, this, &SingleGameScreen::closeBag);
	}

	m_input = bp::InputState();
	m_settled = false;
	m_modeKey.clear();
	m_modeName.clear();

	//模组模式：args["modMode"] = "modId/modeId"（「选择模式」里的卡片点进来的）
	const QString modeKey = args.value(QStringLiteral("modMode")).toString();
	if (!modeKey.isEmpty())
	{
		startModMode(modeKey);
		return;
	}

	m_level = qMax(1, args.value(QStringLiteral("level"), 1).toInt());
	startLevel(m_level);
}

//开始一个模组模式
//
// 与关卡的三个区别：
//   ① 没有"第几关"（模式内部怎么推进由模组自己设计），引擎固定从 1 开始；
//   ② 不结算关卡奖励（金币/经验是关卡的东西；模式要奖励请自己在模组里做）；
//   ③ 地图/敌人/胜负可能都由模组接管（按模式声明的 flags）。
bool SingleGameScreen::startModMode(const QString& key)
{
	const int slash = key.indexOf(QLatin1Char('/'));
	if (slash <= 0) { return false; }

	const QString modId = key.left(slash);
	const QString modeId = key.mid(slash + 1);

	NativeModHost& host = NativeModHost::instance();
	const NativeModHost::ModMode* mode = host.findMode(modId, modeId);
	if (!mode)
	{
		//排查用：把"我手里有哪些模式"也记下来（这类问题十有八九是 id 写错或模组没加载）
		QStringList available;
		for (const NativeModHost::ModMode& item : host.modes())
		{
			available.append(item.key());
		}
		FileManager::instance().log("game", QStringLiteral("模组模式不存在：%1（当前有：%2）")
			.arg(key, available.isEmpty() ? QStringLiteral("无") : available.join(QStringLiteral("、"))));
		QMessageBox::warning(this, QStringLiteral("打不开"),
			QStringLiteral("这个模式已经不在了（提供它的模组可能被关掉了）。"));
		emit requestBack();
		return false;
	}

	//数值配置：先灌默认值 + L1 数据覆盖，再让模式声明的那几项盖上去
	//（顺序有意义：模式是"更具体的一层"，应该盖在数据模组之上）
	m_engine->setConfig(ModManager::instance().gameConfig());
	m_engine->setDropTable(ItemDatabase::instance().dropTable());
	m_engine->setEffectResolver([](const QString& name) {
		return ItemDatabase::instance().effectForItem(name);
	});

	QString reason;
	if (!host.beginMode(modId, modeId, m_engine.get(), &reason))
	{
		FileManager::instance().log("game", QStringLiteral("模组模式启动失败：%1（%2）").arg(key, reason));
		QMessageBox::warning(this, QStringLiteral("打不开"), reason);
		emit requestBack();
		return false;
	}

	m_modeKey = key;
	m_modeName = mode->name;
	m_level = 1;

	//地图：模式自己铺就交给它，否则回落到默认随机地图
	m_engine->setMapGenerator(host.takeModeMapGenerator());

	//时间倍率：模组要能改时间轴，落点在帧循环上
	host.setTimeScaleSink([this](double scale) { m_loop->setTimeScale(scale); });

	m_engine->startLevel(1);
	m_hud->refresh();

	//引擎"借"给宿主：只读查询与写请求都作用在这一局的引擎上
	host.setActiveEngine(m_engine.get());

	m_loop->start();

	FileManager::instance().log("game", QStringLiteral("开始模组模式：%1（%2）").arg(mode->name, key));
	return true;
}

//离开界面：停循环、收尾
void SingleGameScreen::onExit()
{
	m_loop->stop();
	m_loop->setTimeScale(1.0);

	//模组模式要收尾：调模组的 on_mode_stop，并把钩子从引擎上摘掉
	if (!m_modeKey.isEmpty())
	{
		NativeModHost::instance().endMode();
		NativeModHost::instance().setTimeScaleSink(nullptr);
		m_modeKey.clear();
		m_modeName.clear();
	}

	//归还引擎：离开关卡后宿主不能再操作它（模组的相关调用一律得到"时机不对"）
	NativeModHost::instance().setActiveEngine(nullptr);

	//把奖励等变化落盘
	PlayerProfile::instance().save();
}

bool SingleGameScreen::handleBack()
{
	//背包覆盖层开着时，Esc 先关背包（而不是直接退出本局）
	//为什么必须先判断：Esc 走的是 handleBack，不经过 handleKeyPress，
	//所以"背包可见就把按键转给它"那条分支根本管不到 Esc
	if (m_bag && m_bag->isVisible())
	{
		closeBag();
		return true;
	}

	//Esc 一律返回上一屏（关卡选择）。
	emit requestBack();
	return true;
}

//开一局：恢复设置里的显示项，然后让引擎初始化关卡
void SingleGameScreen::startLevel(int level)
{
	m_canvas->setShowGrid(SettingsManager::instance().showGrid());

	//关卡数据化：Data/levels 里有这一关就用它（可以是数值，也可以是手写地图）；
	//没有就退回程序化随机关卡 —— 老存档、缺目录、坏文件都不会让关卡打不开
	const bp::LevelDefinition* definition = LevelDatabase::instance().find(level);
	if (definition)
	{
		m_engine->setMapGenerator(std::make_unique<bp::JsonMapGenerator>(*definition));
	}
	else
	{
		m_engine->setMapGenerator(nullptr);   //nullptr = 恢复默认的随机生成器
	}

	//数值配置：默认值 + 所有生效 mod 的玩法覆盖
	//（每次开局都重算，所以在设置里开关一个 mod 之后立刻开局就能看到效果）
	m_engine->setConfig(ModManager::instance().gameConfig());

	//道具数据驱动：掉落权重与"道具名 -> 效果"都取自 Data/itemInfo.json
	//放在开局这一层而不是构造函数：界面是启动时预创建的，那时道具数据还没加载完
	m_engine->setDropTable(ItemDatabase::instance().dropTable());
	m_engine->setEffectResolver([](const QString& name) {
		return ItemDatabase::instance().effectForItem(name);
	});

	m_engine->startLevel(level);
	m_hud->refresh();

	//把这一局的引擎"借"给原生模组宿主：它的只读查询与写请求都作用在这个引擎上。
	//（宿主不持有所有权；离开关卡时传 nullptr，见 onExit）
	NativeModHost::instance().setActiveEngine(m_engine.get());

	m_loop->start();

	FileManager::instance().log("game", QStringLiteral("开始关卡 %1（%2）")
		.arg(level)
		.arg(definition ? definition->name : QStringLiteral("程序化地图")));
}

//每步推进后刷新 HUD，并把这一步交给原生模组（事件分发 + on_tick）
void SingleGameScreen::onEngineStepped()
{
	m_hud->refresh();

	//1/60 秒：引擎是固定步长，模组拿到的 dt 与它一致（不跟渲染帧率走）
	NativeModHost::instance().onEngineStepped(1.0 / 60.0);
}

// ---------- 键盘（由 AppHost 统一分发） ----------

void SingleGameScreen::handleKeyPress(QKeyEvent* event)
{
	//背包覆盖层打开时：B 或 Esc 关闭它，其余按键不给游戏（游戏已经暂停了）
	if (m_bag && m_bag->isVisible())
	{
		if (event->key() == Qt::Key_B) { closeBag(); }
		return;
	}

	switch (event->key())
	{
	case Qt::Key_W: m_input.up = true; break;
	case Qt::Key_S: m_input.down = true; break;
	case Qt::Key_A: m_input.left = true; break;
	case Qt::Key_D: m_input.right = true; break;
	case Qt::Key_Space:
		m_input.placeBomb = true;
		break;
	case Qt::Key_B:
		openBag();
		return;
	default:
		break;
	}

	//输入直接交给引擎：画布不参与输入转发（它只负责画）
	m_engine->setInput(m_input);
}

void SingleGameScreen::handleKeyRelease(QKeyEvent* event)
{
	switch (event->key())
	{
	case Qt::Key_W: m_input.up = false; break;
	case Qt::Key_S: m_input.down = false; break;
	case Qt::Key_A: m_input.left = false; break;
	case Qt::Key_D: m_input.right = false; break;
	case Qt::Key_Space: m_input.placeBomb = false; break;
	default:
		break;
	}

	m_engine->setInput(m_input);
}

// ---------- 背包覆盖层 ----------

void SingleGameScreen::openBag()
{
	if (!m_bag) { return; }

	//暂停逻辑（引擎冻结规则，但爆炸痕这类表现计时继续）
	m_loop->pause();

	m_bag->setGameScreen(this);
	m_bag->setGeometry(rect());
	m_bag->raise();
	m_bag->show();

	//关键：覆盖层不走窗口栈，**永远不会收到 onEnter**，
	//所以背包列表必须在这里显式刷新 —— 漏了这句的表现就是"按 B 出来的背包永远是空的"
	m_bag->refresh();
}

void SingleGameScreen::closeBag()
{
	if (!m_bag) { return; }

	m_bag->hide();

	if (m_engine->phase() == bp::Phase::Paused)
	{
		m_loop->resume();
	}

	//把焦点收回游戏界面：隐藏一个持有焦点的控件时，焦点会回退到窗口本身。
	//按键分发已经不再依赖焦点（见 AppHost::eventFilter），但"焦点在哪"清楚一点更好排查
	setFocus(Qt::OtherFocusReason);

	//留一条日志：这类"关掉覆盖层之后键盘失灵"的问题，焦点信息是最直接的线索
	FileManager::instance().log("bag", QStringLiteral("关闭背包：阶段=%1 焦点=%2")
		.arg(m_engine->phase() == bp::Phase::Running ? QStringLiteral("Running") : QStringLiteral("其它"))
		.arg(QApplication::focusWidget() ? QApplication::focusWidget()->metaObject()->className()
										 : "(无)"));
}

// ---------- 结算 ----------

void SingleGameScreen::onLevelFinished(bool victory)
{
	if (m_settled) { return; }
	m_settled = true;

	m_loop->stop();

	//模组模式：不结算关卡奖励（金币/经验是关卡的东西），结果文案优先用模组给的那句话
	if (!m_modeKey.isEmpty())
	{
		const QString reason = NativeModHost::instance().modeFinishReason();
		const QString text = reason.isEmpty()
			? (victory ? QStringLiteral("这一局你赢了。") : QStringLiteral("这一局输了。"))
			: reason;

		PlayerProfile::instance().save();
		m_hud->refresh();

		QMessageBox::information(this, victory ? QStringLiteral("结束") : QStringLiteral("结束"), text);
		emit requestBack();
		return;
	}

	PlayerProfile& profile = PlayerProfile::instance();

	if (victory)
	{
		//奖励数值：关卡文件写了就用它的，没写就用 GameConfig 的公式
		//（"数据优先、公式兜底"这条在这一阶段到处都在用）
		const bp::LevelDefinition* definition = LevelDatabase::instance().find(m_level);
		const int coins = (definition && definition->coins >= 0)
			? definition->coins
			: m_engine->config().coinsForLevel(m_level);
		const int ep = (definition && definition->ep >= 0)
			? definition->ep
			: m_engine->config().epForLevel(m_level);

		if (m_level > profile.info().getPassedLevels())
		{
			profile.info().setPassedLevels(m_level);
		}
		profile.info().setCoins(profile.info().getCoins() + coins);
		profile.info().setEP(profile.info().getEP() + ep);
		profile.save();

		const QString title = (definition && !definition->name.isEmpty())
			? definition->name
			: QStringLiteral("关卡 %1").arg(m_level);

		QMessageBox::information(this, QStringLiteral("胜利"),
			QStringLiteral("恭喜通过「%1」！\n获得 %2 金币、%3 经验").arg(title).arg(coins).arg(ep));
	}
	else
	{
		profile.save();
		QMessageBox::information(this, QStringLiteral("失败"), QStringLiteral("你被炸死了..."));
	}

	m_hud->refresh();

	//回到关卡选择
	emit requestBack();
}
