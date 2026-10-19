#include "ScreenRouter.h"
#include "ScreenBase.h"
#include "SettingsManager.h"
#include "FileManager.h"
#include <QStackedWidget>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>

//页面切换淡入时长（毫秒）
//为什么这么短：淡入是为了"切页不生硬"，不是动画表演；超过 250ms 就开始影响操作感了
static const int kPageFadeMs = 160;

//哪些界面不做淡入
//游戏画面（单人/联机）是 60Hz 自绘画布：给它挂 QGraphicsOpacityEffect 会让每帧多一次
//离屏合成，纯属白花钱；而且打游戏时"画面淡入"反而像卡了一下
static bool skipsFade(const QString& screenId)
{
	return screenId == ScreenId::SingleGame || screenId == ScreenId::MultiGame;
}

//淡入一个界面
//注意动画结束必须把 effect 摘掉：留着的话每次重绘都要走一次离屏合成
static void fadeIn(QWidget* widget, int durationMs)
{
	if (!widget || durationMs <= 0) { return; }

	QGraphicsOpacityEffect* effect = new QGraphicsOpacityEffect(widget);
	effect->setOpacity(0.0);
	widget->setGraphicsEffect(effect);

	QPropertyAnimation* animation = new QPropertyAnimation(effect, "opacity", widget);
	animation->setDuration(durationMs);
	animation->setStartValue(0.0);
	animation->setEndValue(1.0);
	animation->setEasingCurve(QEasingCurve::OutCubic);

	QObject::connect(animation, &QPropertyAnimation::finished, widget, [widget]()
	{
		//setGraphicsEffect(nullptr) 会删掉旧的 effect
		widget->setGraphicsEffect(nullptr);
	});

	animation->start(QAbstractAnimation::DeleteWhenStopped);
}

//单例访问
ScreenRouter& ScreenRouter::instance()
{
	static ScreenRouter s_instance;
	return s_instance;
}

//绑定宿主窗口里的 QStackedWidget
void ScreenRouter::setHost(QStackedWidget* stack)
{
	m_host = stack;
}

//注册界面工厂
void ScreenRouter::registerScreen(const QString& id, Factory factory)
{
	if (id.isEmpty() || !factory)
	{
		FileManager::instance().log("router", QString("注册界面失败（id 或工厂为空）: %1").arg(id));
		return;
	}
	m_factories.insert(id, factory);
}

//预先创建所有已注册界面
void ScreenRouter::precreateAll()
{
	const QList<QString> ids = m_factories.keys();
	int created = 0;

	for (const QString& id : ids)
	{
		//已缓存的跳过
		if (m_cache.contains(id)) { continue; }

		bool reused = false;
		ScreenBase* screen = createScreen(id, &reused);
		if (screen) { ++created; }
	}

	FileManager::instance().log("router", QString("界面预创建完成: %1 个").arg(created));
}

//按工厂创建界面（常驻界面只创建一次）
ScreenBase* ScreenRouter::createScreen(const QString& id, bool* reused)
{
	if (reused) { *reused = false; }

	//常驻界面复用
	if (m_cache.contains(id))
	{
		if (reused) { *reused = true; }
		return m_cache.value(id);
	}

	if (!m_factories.contains(id))
	{
		FileManager::instance().log("router", QString("未注册的界面: %1").arg(id));
		return nullptr;
	}

	if (!m_host)
	{
		FileManager::instance().log("router", "尚未绑定宿主窗口，无法创建界面");
		return nullptr;
	}

	ScreenBase* screen = m_factories.value(id)(m_host);
	if (!screen)
	{
		FileManager::instance().log("router", QString("界面创建失败: %1").arg(id));
		return nullptr;
	}

	//加入窗口栈（页面归属由 QStackedWidget 管理，不再用 WA_DeleteOnClose）
	m_host->addWidget(screen);

	//界面层统一接线：界面的返回/退出请求都由窗口栈处理，界面自己不需要知道"上一个是谁"
	connect(screen, &ScreenBase::requestBack, this, [this]() { pop(); });
	connect(screen, &ScreenBase::requestQuit, this, &ScreenRouter::quitRequested);

	//常驻界面进入缓存
	if (screen->persistent()) { m_cache.insert(id, screen); }

	return screen;
}

//把界面设为当前页
//resume=true 表示这是"从栈顶恢复"的场景，需要补一次 onEnter；
//新入栈的界面由 push/reset 自己调用 onEnter，这里不重复调用
void ScreenRouter::activate(ScreenBase* screen, const QVariantMap& args, bool resume)
{
	if (!screen || !m_host) { return; }

	if (resume) { screen->onEnter(args); }
	m_host->setCurrentWidget(screen);

	//页面切换淡入（设置里可关；游戏画面恒不淡入，见 skipsFade 的说明）
	if (SettingsManager::instance().pageFade() && !skipsFade(screen->screenId()))
	{
		fadeIn(screen, kPageFadeMs);
	}

	emit screenChanged(screen->screenId(), screen->displayTitle());
	FileManager::instance().log("router", QString("进入界面: %1（栈深 %2）").arg(screen->screenId()).arg(m_stack.size()));
}

//离开栈顶：调用 onExit 并按需销毁
void ScreenRouter::leaveTop()
{
	if (m_stack.isEmpty() || !m_host) { return; }

	ScreenBase* top = m_stack.takeLast();
	const QString id = top ? top->screenId() : QString();

	if (top) { top->onExit(); }

	//非常驻界面：移出窗口栈并销毁（deleteLater 保证不会在信号处理中间被析构）
	if (top && !top->persistent())
	{
		m_host->removeWidget(top);
		top->deleteLater();
	}

	FileManager::instance().log("router", QString("离开界面: %1（栈深 %2）").arg(id).arg(m_stack.size()));
}

//让栈顶界面转入后台（保留在栈里，之后返回时会重新 onEnter）
void ScreenRouter::suspendTop()
{
	if (m_stack.isEmpty()) { return; }

	ScreenBase* top = m_stack.last();
	if (!top) { return; }

	top->onExit();
	FileManager::instance().log("router", QString("界面转入后台: %1").arg(top->screenId()));
}

//清空整栈
void ScreenRouter::clearStack()
{
	while (!m_stack.isEmpty())
	{
		leaveTop();
	}
}

//清空窗口栈并显示指定界面
void ScreenRouter::reset(const QString& id, const QVariantMap& args)
{
	clearStack();

	bool reused = false;
	ScreenBase* screen = createScreen(id, &reused);
	if (!screen) { return; }

	m_stack.append(screen);
	screen->onEnter(args);
	activate(screen, args, false);
}

//入栈并显示
void ScreenRouter::push(const QString& id, const QVariantMap& args)
{
	//当前界面转入后台（重要：否则大厅会把 socket 和广播定时器一直留在后台跑）
	suspendTop();

	bool reused = false;
	ScreenBase* screen = createScreen(id, &reused);
	if (!screen) { return; }

	m_stack.append(screen);
	screen->onEnter(args);
	activate(screen, args, false);
}

//替换栈顶
void ScreenRouter::replace(const QString& id, const QVariantMap& args)
{
	//先离开栈顶（非常驻界面会被销毁，常驻界面保留在缓存里）
	leaveTop();

	push(id, args);
}

//出栈
bool ScreenRouter::pop()
{
	//栈底不允许再出栈，交给 AppHost 去处理退出流程
	if (m_stack.size() <= 1)
	{
		emit quitRequested();
		return false;
	}

	leaveTop();

	ScreenBase* top = m_stack.last();
	if (top && m_host)
	{
		//回到上一界面：补一次 onEnter，界面自己决定是否刷新
		activate(top, QVariantMap(), true);
	}
	return true;
}

//当前界面 id
QString ScreenRouter::currentId() const
{
	ScreenBase* screen = currentScreen();
	return screen ? screen->screenId() : QString();
}

//当前界面指针
ScreenBase* ScreenRouter::currentScreen() const
{
	if (m_stack.isEmpty()) { return nullptr; }
	return m_stack.last();
}
