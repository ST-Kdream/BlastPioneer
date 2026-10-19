#include "SplashScreen.h"
#include "ScreenRouter.h"
#include "SettingsManager.h"
#include "FileManager.h"
#include "FigManager.h"
#include "ThemeManager.h"
#include "LaunchGuard.h"
#include "ModManager.h"
#include <QQuickWidget>
#include <QQmlContext>
#include <QLabel>
#include <QVBoxLayout>
#include <QThread>
#include <QTimer>
#include <QApplication>
#include <QMessageBox>
#include <QPushButton>
#include <QProcess>

//最短显示时间（毫秒）
static const int MIN_SHOW_MS = 3000;
//看门狗超时（毫秒）
static const int MAX_SHOW_MS = 15000;

SplashScreen::SplashScreen(QWidget* parent) : ScreenBase(parent)
{
	//QML 粒子特效：作为本界面的子控件，不再是独立窗口
	m_effect = new QQuickWidget(this);
	m_effect->setResizeMode(QQuickWidget::SizeRootObjectToView);
	m_effect->setSource(QUrl("qrc:/startupEffect.qml"));
	//向 QML 传递特效质量（现在通过 QML 根对象的上下文属性传递）
	m_effect->rootContext()->setContextProperty("effectQuality",
		SettingsManager::instance().effectQuality());

	//启动界面本身是深色的（画面上方是 QML 特效），所以这里显式给个深色底，
	//避免浅色主题下"白字落在浅底上"看不见
	setObjectName("splashRoot");
	setAttribute(Qt::WA_StyledBackground, true);

	//初始化进度与提示文字
	m_stepLabel = new QLabel("正在初始化...", this);
	m_stepLabel->setAlignment(Qt::AlignCenter);
	m_stepLabel->setObjectName("splashStep");

	m_tipLabel = new QLabel("爆破先锋", this);
	m_tipLabel->setAlignment(Qt::AlignCenter);
	m_tipLabel->setObjectName("splashTitle");

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(m_effect, 1);
	layout->addWidget(m_tipLabel, 0);
	layout->addWidget(m_stepLabel, 0);
	setLayout(layout);
}

SplashScreen::~SplashScreen()
{
	stopBootstrap();
}

//进入界面：开始初始化
void SplashScreen::onEnter(const QVariantMap& args)
{
	m_minTimeReached = false;
	m_dataReady = false;
	m_finished = false;
	m_playMode = args.value(QStringLiteral("playMode")).toString().trimmed();

	//最短显示 3 秒
	QTimer* minTimer = new QTimer(this);
	minTimer->setSingleShot(true);
	connect(minTimer, &QTimer::timeout, this, &SplashScreen::onMinTimeReached);
	minTimer->start(MIN_SHOW_MS);

	//看门狗：超时按失败处理，绝不卡在启动界面
	QTimer* maxTimer = new QTimer(this);
	maxTimer->setSingleShot(true);
	connect(maxTimer, &QTimer::timeout, this, &SplashScreen::onTimeout);
	maxTimer->start(MAX_SHOW_MS);

	startBootstrap();
}

//离开界面：确保工作线程已停止
void SplashScreen::onExit()
{
	stopBootstrap();
}

//启动初始化线程
void SplashScreen::startBootstrap()
{
	if (m_thread) { return; }

	m_thread = new QThread(this);
	m_task = new BootstrapTask();      //无父对象，归工作线程所有
	m_task->moveToThread(m_thread);

	connect(m_thread, &QThread::started, m_task, &BootstrapTask::run);
	connect(m_task, &BootstrapTask::progress, this, &SplashScreen::onBootstrapProgress);
	connect(m_task, &BootstrapTask::finished, this, &SplashScreen::onBootstrapFinished);

	m_thread->start();
}

//停止工作线程
void SplashScreen::stopBootstrap()
{
	if (!m_thread) { return; }

	//让工作线程自己清理任务对象，避免"在主线程析构工作线程对象"的未定义行为
	if (m_task)
	{
		m_task->deleteLater();
		m_task = nullptr;
	}

	m_thread->quit();
	m_thread->wait();

	m_thread->deleteLater();
	m_thread = nullptr;
}

//初始化进度
void SplashScreen::onBootstrapProgress(int percent, const QString& stepText)
{
	m_stepLabel->setText(QString("%1（%2%）").arg(stepText).arg(percent));
}

//初始化完成
void SplashScreen::onBootstrapFinished(const BootstrapResult& result)
{
	m_result = result;
	m_dataReady = true;

	//道具图标位图预热：工作线程里已经把 itemInfo.json 的图标读成 QImage 了，
	//这里（GUI 线程）一次性转成 QPixmap，避免第一次捡到道具时才现转造成卡顿
	FigManager::instance().preloadPixmaps();

	//模组是**这一步**才加载完的（启动管线的工作线程里读盘），而界面外观在 main 里
	//就应用过一次了 —— 所以必须在这里重算一次，否则"mod 改的配色/样式表"要等玩家
	//去设置或模组页点一下才生效。（样式表必须在 GUI 线程应用，所以放在这里而不是任务里）
	ThemeManager::instance().reload();

	tryFinish();
}

//最短显示时间到
void SplashScreen::onMinTimeReached()
{
	m_minTimeReached = true;
	tryFinish();
}

//看门狗超时
void SplashScreen::onTimeout()
{
	if (m_finished) { return; }

	FileManager::instance().log("boot", "初始化超时，按失败处理，直接进入界面");
	m_dataReady = true;
	m_minTimeReached = true;
	m_result.playerDataFound = false;
	tryFinish();
}

//两个条件都满足后进入下一个界面
void SplashScreen::tryFinish()
{
	if (m_finished || !m_dataReady || !m_minTimeReached) { return; }

	m_finished = true;
	stopBootstrap();

	if (m_result.errorCount > 0)
	{
		FileManager::instance().log("boot", QString("初始化有 %1 步失败，详情见日志").arg(m_result.errorCount));
	}

	//健康信号：启动管线跑完、马上就能给出一个可用界面 -> 计数清零（契约 §3.3）。
	//**必须在"切界面"之前**记：切界面可能会弹模态框（比如模式打不开的提示），
	//那会把这一行永远挡在门外 —— 于是"模式打不开"会被误算成"上次崩溃"，反复几次就进安全模式了。
	LaunchGuard::instance().markReachedHome();

	//有存档进主页，没存档进注册/欢迎页
	if (!m_playMode.isEmpty())
	{
		//调试开关：直接进某个模组模式（模组已经在上面的启动管线里加载完）
		FileManager::instance().log("boot", QStringLiteral("按 --play-mode 直接进入模组模式：%1").arg(m_playMode));
		ScreenRouter::instance().reset(ScreenId::SingleGame,
									   QVariantMap{ { QStringLiteral("modMode"), m_playMode } });
	}
	else if (m_result.playerDataFound)
	{
		ScreenRouter::instance().reset(ScreenId::Home);
	}
	else
	{
		ScreenRouter::instance().reset(ScreenId::Welcome);
	}

	//健康信号：启动管线跑完、界面可用了 -> 连续异常结束计数清零（契约 §3.3）。
	//为什么"到这里"就算健康：原生模组是在启动管线的第 4 步加载的，
	//能走到这一行就说明那一轮加载没有把进程带走
	LaunchGuard::instance().markReachedHome();

	//可疑状态（上次异常结束过一次）：弹一次"要不要排查"的三选一。
	//为什么只在这一刻弹：那时窗口已经在、原生模组已经加载完，
	//三个选项各自该做的事（重启 / 改开关 / 继续）都做得成
	if (LaunchGuard::instance().consumeCrashPrompt())
	{
		showCrashPrompt();
	}
}

//上次异常结束的提示（契约 §3.3 ③）
//
// 三个按钮对应玩家的三种心态："我要排查"、"我就是想玩"、"我知道风险"。
// 比让玩家自己去翻配置文件与日志友好得多 —— 而原生模组崩溃时，玩家本来就没法进游戏查。
void SplashScreen::showCrashPrompt()
{
	LaunchGuard& guard = LaunchGuard::instance();

	QStringList lines;
	lines.append(guard.notes());
	lines.append(QString());
	lines.append(QStringLiteral("当时正在运行的原生模组："));

	const QStringList ids = guard.lastNativeMods();
	if (ids.isEmpty())
	{
		lines.append(QStringLiteral("    （当时还没有模组加载成功）"));
	}
	for (const QString& id : ids)
	{
		QString name = id;
		QString version;
		for (const ModManager::ModEntry& entry : ModManager::instance().mods())
		{
			if (entry.manifest.id == id)
			{
				name = entry.manifest.name.isEmpty() ? id : entry.manifest.name;
				version = entry.manifest.version;
				break;
			}
		}
		lines.append(QStringLiteral("    %1  v%2").arg(name, version.isEmpty() ? QStringLiteral("?") : version));
	}

	lines.append(QString());
	lines.append(QStringLiteral("「用安全模式启动」本次不加载任何原生模组，会重启游戏；"));
	lines.append(QStringLiteral("「禁用这些模组」把这些模组关掉（重启后生效）；"));
	lines.append(QStringLiteral("「仍然正常启动」什么都不做。"));

	QMessageBox box(this);
	box.setWindowTitle(QStringLiteral("上次启动异常结束"));
	box.setIcon(QMessageBox::Warning);
	box.setText(lines.join(QLatin1Char('\n')));

	FileManager::instance().log("boot", QStringLiteral("弹出崩溃提示：%1（涉及 %2 个原生模组）")
		.arg(guard.notes()).arg(ids.size()));

	QPushButton* safeButton = box.addButton(QStringLiteral("用安全模式启动"), QMessageBox::AcceptRole);
	QPushButton* disableButton = box.addButton(QStringLiteral("禁用这些模组并正常启动"), QMessageBox::ActionRole);
	QPushButton* normalButton = box.addButton(QStringLiteral("仍然正常启动"), QMessageBox::RejectRole);
	box.setDefaultButton(normalButton);
	box.exec();

	if (box.clickedButton() == safeButton)
	{
		guard.noteSafeModeRequest();
		restartInSafeMode();
		return;
	}

	if (box.clickedButton() == disableButton)
	{
		//只动模组开关，**不动存档与设置里的其它项**
		for (const QString& id : ids)
		{
			SettingsManager::instance().setModEnabled(id, false);
		}
		ModManager::instance().reload();

		//DLL 加载过就不能卸载（契约 §3.2），所以"关掉它"只能下次启动才真的生效 —— 如实说
		QMessageBox::information(this, QStringLiteral("已禁用"),
			QStringLiteral("这些原生模组已经关掉了，下次启动游戏就不会再加载它们。"));
	}
}

//按安全模式重启：把 --safe-mode 追加到当前命令行之后
//（保留原来的 --root=<目录> 等参数，否则"用隔离目录调试"的人会突然跑到默认目录去）
void SplashScreen::restartInSafeMode()
{
	QStringList args = QCoreApplication::arguments();
	if (!args.isEmpty()) { args.removeFirst(); }        //去掉 exe 自身
	if (!args.contains(QStringLiteral("--safe-mode"))) { args.append(QStringLiteral("--safe-mode")); }

	FileManager::instance().log("boot", QStringLiteral("玩家选择用安全模式启动，正在重启：%1")
		.arg(args.join(QLatin1Char(' '))));

	if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), args))
	{
		QMessageBox::information(this, QStringLiteral("请手动启动"),
			QStringLiteral("自动重启没有成功。请关掉游戏后按住 Shift 再启动一次，"
						   "或用命令 BlastPioneer.exe --safe-mode 启动。"));
		return;
	}

	QApplication::quit();
}
