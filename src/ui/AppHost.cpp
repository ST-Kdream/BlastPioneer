#include "AppHost.h"
#include <QAbstractSpinBox>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QLineEdit>
#include "ScreenRouter.h"
#include "ScreenBase.h"
#include "SettingsManager.h"
#include "PlayerProfile.h"
#include "FileManager.h"
#include "FigManager.h"
#include "LaunchGuard.h"
#include <QApplication>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QEvent>
#include <QMessageBox>

AppHost::AppHost(QWidget* parent) : QWidget(parent)
{
	//唯一的顶层窗口：标题、大小、图标
	setWindowTitle("爆破先锋");
	setMinimumSize(800, 600);

	//窗口位置与大小：整个程序只有这一个顶层窗口，因此只保存一份几何
	QByteArray savedGeo = SettingsManager::instance().screenGeometry("AppHost");
	if (!savedGeo.isEmpty())
	{
		restoreGeometry(savedGeo);
	}
	else
	{
		resize(800, 600);
		move(100, 100);
	}

	//窗口栈
	m_stack = new QStackedWidget(this);
	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(m_stack);
	setLayout(layout);
}

//连接窗口栈信号
void AppHost::initialize()
{
	ScreenRouter& router = ScreenRouter::instance();
	router.setHost(m_stack);

	connect(&router, &ScreenRouter::screenChanged, this, &AppHost::onScreenChanged);
	connect(&router, &ScreenRouter::quitRequested, this, &AppHost::requestExit);

	//键盘分发：Qt 的按键事件只会送给有焦点的子控件，父窗口不一定收得到，
	//所以这里装全局事件过滤器，把按键统一交给"当前界面"处理
	qApp->installEventFilter(this);
	//过滤器不生效时的兜底路径（焦点在窗口本身时）
	setFocusPolicy(Qt::StrongFocus);

	//安全模式横幅：要在第一个界面显示之前就准备好，否则玩家会先看到一屏没有提示的界面
	setupSafeModeBanner();

	//正常退出 = 崩溃保护里的"clean"（契约 §3.3）。
	//挂 aboutToQuit 而不是 closeEvent：退出确认被取消时不该记成"正常退出"，
	//而 aboutToQuit 只在真的开始退出时才会来
	connect(qApp, &QCoreApplication::aboutToQuit, this, []() {
		LaunchGuard::instance().markCleanExit();
	});

	//图片位图缓存预热：放在 GUI 线程、进入主页之前，
	//避免"第一次打开商店/背包"时才逐张转换造成卡顿
	FigManager::instance().preloadPixmaps();
}

//安全模式横幅
//
// 为什么必须"说出来"：安全模式下原生模组一个都没加载，玩家如果不知道这件事，
// 会以为"模组装坏了"或者"游戏坏了"。横幅就是那句"这是有意的、以及怎么退出安全模式"。
void AppHost::setupSafeModeBanner()
{
	if (!LaunchGuard::instance().blockNativeMods()) { return; }

	m_safeBanner = new QLabel(QStringLiteral("安全模式：原生模组已全部禁用"), this);
	m_safeBanner->setObjectName("safeModeBanner");
	//自定义 QWidget 子类（含 QLabel）要显示 QSS 背景必须开这个属性
	m_safeBanner->setAttribute(Qt::WA_StyledBackground, true);
	m_safeBanner->setAlignment(Qt::AlignCenter);
	m_safeBanner->setToolTip(QStringLiteral("按住 Shift 启动，或加启动参数 --safe-mode 可再次进入安全模式\n"
											"本次运行有效，重启后恢复；在模组页可以逐个启用原生模组"));

	//插到最上面（窗口栈之上）
	QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(this->layout());
	if (layout) { layout->insertWidget(0, m_safeBanner); }

	//记一条：玩家在界面上看到横幅这件事本身也是排障信息
	FileManager::instance().log("ui", QStringLiteral("安全模式：顶部横幅已显示"));
}

//全局事件过滤：键盘统一分发给当前界面
bool AppHost::eventFilter(QObject* watched, QEvent* event)
{
	//只关心按键
	if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease)
	{
		return QWidget::eventFilter(watched, event);
	}

	//只处理真实（操作系统投递）的按键：
	//sendEvent/postEvent 产生的人工按键会沿着控件树被投递多次，
	//不判断就会出现"按一次键、界面收到三次"
	if (!event->spontaneous())
	{
		return QWidget::eventFilter(watched, event);
	}

	//模态对话框（消息框、确认框）自己要用键盘：Esc 关闭、回车/空格点默认按钮、
	//方向键在列表里选。这类事件必须放行，否则"弹窗只能用鼠标操作"
	if (QApplication::activeModalWidget())
	{
		return QWidget::eventFilter(watched, event);
	}

	QWidget* widget = qobject_cast<QWidget*>(watched);
	if (!widget)
	{
		return QWidget::eventFilter(watched, event);
	}

	//两种情况都要接：
	//  ① 目标是本窗口的后代控件（正常情况：焦点在某个子控件上）
	//  ② 目标就是本窗口本身 —— 没有任何子控件持有焦点时，Qt 把按键送给窗口。
	//     以前这种情况被"跳过"，于是只要焦点落到窗口上，整个键盘就失灵了；
	//     最容易触发它的操作就是"关掉一个覆盖层"：持有焦点的按钮被隐藏，
	//     焦点回退到窗口，接下来 WASD/空格全部没反应（用户报的就是这个）。
	//     "按一次键被分发多次"不用靠这里防：spontaneous 已经挡住了人工事件，
	//     而真事件只会送给一个目标，且我们在下面 return true 把它吃掉。
	const bool targetsWindow = (widget == this);
	if (!targetsWindow && !isAncestorOf(widget))
	{
		return QWidget::eventFilter(watched, event);
	}

	ScreenBase* screen = ScreenRouter::instance().currentScreen();
	if (!screen)
	{
		return QWidget::eventFilter(watched, event);
	}

	QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

	//文本输入控件必须拿到原始按键：这里以前无差别 return true，导致设置页里
	//QLineEdit 收不到字符（"更新源"那一栏完全打不了字）。Esc 例外 —— 它始终是"返回"。
	//为什么用 focusWidget 判断而不是 watched：按键可能投递给父控件，真正要吃字的是当前焦点控件。
	QWidget* focus = QApplication::focusWidget();
	const bool textInput = qobject_cast<QLineEdit*>(focus) || qobject_cast<QTextEdit*>(focus)
		|| qobject_cast<QPlainTextEdit*>(focus) || qobject_cast<QAbstractSpinBox*>(focus);
	if (textInput && keyEvent->key() != Qt::Key_Escape)
	{
		return QWidget::eventFilter(watched, event);
	}

	//统一分发（bp::dispatchKey）：Esc 走 handleBack，其余走 handleKeyPress/Release
	//注意：这里只做一层分发 —— 游戏界面内部会在自己的 handleKeyPress 里
	//把按键再转给背包覆盖层，往下传是界面自己的事
	dispatchKey(screen, keyEvent);

	//无论界面是否消费，都不再让事件继续传播给目标控件。
	//为什么：目标控件（QStackedWidget、AppHost 自己）拿到未处理的 Esc 后
	//会走 QWidget 默认处理，主窗口可能被关闭 —— 这正是之前"按 Esc 弹退出确认"的根因
	return true;
}

//界面切换后更新窗口标题
void AppHost::onScreenChanged(const QString& id, const QString& title)
{
	m_currentId = id;
	if (title.isEmpty())
	{
		setWindowTitle("爆破先锋");
	}
	else
	{
		setWindowTitle(QString("爆破先锋 - %1").arg(title));
	}
}

//保存数据与窗口位置
void AppHost::saveAll()
{
	PlayerProfile::instance().save();
	SettingsManager::instance().setScreenGeometry("AppHost", saveGeometry());
	FileManager::instance().log("app", "退出前已保存玩家数据与窗口位置");
}

//退出确认：返回是否可以继续退出
bool AppHost::confirmExit()
{
	if (!SettingsManager::instance().confirmOnExit()) { return true; }

	QMessageBox::StandardButton answer = QMessageBox::question(
		this,
		"退出确认",
		"确认退出游戏吗？\n当前进度会自动保存。",
		QMessageBox::Yes | QMessageBox::No,
		QMessageBox::No);

	return answer == QMessageBox::Yes;
}

//请求退出程序
void AppHost::requestExit()
{
	if (!confirmExit()) { return; }

	saveAll();
	qApp->quit();
}

//点 × 关闭窗口
void AppHost::closeEvent(QCloseEvent* event)
{
	if (!confirmExit())
	{
		event->ignore();
		return;
	}

	saveAll();
	QWidget::closeEvent(event);
}

//按键兜底
//注意：这里【绝对不能再处理 Esc】。Esc 已经在 eventFilter 里交给当前界面处理过，
//而"返回上一屏"会把当前界面换掉（往往换到主页）。若这里再处理一次，
//第二次拿到的界面是主页：它的 handleBack() 返回 false，事件就会落到底层
//QWidget::keyPressEvent，触发主窗口关闭流程 —— 表现就是"按 Esc 弹出退出确认框"。
//这里只把未被处理的按键交给基类。
void AppHost::keyPressEvent(QKeyEvent* event)
{
	QWidget::keyPressEvent(event);
}
