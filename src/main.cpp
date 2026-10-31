#include <QApplication>
#include <QCommandLineParser>
#include <QLoggingCategory>
#include <QGuiApplication>
#include <QThread>
#include <QFileInfo>
#include <QIcon>
#include <QQuickWindow>
#include <QSGRendererInterface>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shobjidl.h>
#pragma comment(lib, "shell32.lib")
#endif

#include "DirectoryLayout.h"
#include "FileManager.h"
#include "SettingsManager.h"
#include "PlayerProfile.h"
#include "ItemDatabase.h"
#include "LevelDatabase.h"
#include "FigManager.h"
#include "ThemeManager.h"
#include "ModManager.h"
#include "NativeModHost.h"
#include "LaunchGuard.h"

#include "version.h"

#include "AppHost.h"
#include "ScreenRouter.h"

#include "SplashScreen.h"
#include "WelcomeScreen.h"
#include "HomeScreen.h"
#include "PlayerScreen.h"
#include "BagScreen.h"
#include "ShopScreen.h"
#include "LevelSelectScreen.h"
#include "ModeSelectScreen.h"
#include "ModsScreen.h"
#include "RulesScreen.h"
#include "SettingsScreen.h"
#include "LobbyScreen.h"
#include "SingleGameScreen.h"
#include "MultiGameScreen.h"

int main(int argc, char* argv[])
{
	//图形后端：**只支持 D3D11**（发布策略，不是优化）
	//
	//  ① 发布包不带 opengl32sw.dll（省 19.7 MB）：那是"没有显卡驱动时用 CPU 画"的兜底，
	//     带上它等于默许某台机器退到软渲染 —— 帧率掉到个位数，比"起不来"更难排查；
	//  ② 这里以前叠着一串兜底（AA_UseSoftwareOpenGL / QT_OPENGL=angle|software /
	//     QT_QUICK_BACKEND=software / QSG_RENDERER_LOOP=basic）。它们会**互相覆盖**，
	//     最终跑在哪个后端取决于设置顺序，是典型的"改了没反应、不成就找不到原因"。
	//     全部删掉，只留一条明确的后端选择；
	//  ③ 必须在创建 QApplication / 第一个 QQuickWindow 之前调用（SplashScreen 会建 QQuickWidget）。
	QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);

	QApplication app(argc, argv);
	app.setOrganizationName("ST-Kdream");
	app.setApplicationName(QStringLiteral("BlastPioneer"));
	app.setApplicationVersion(QStringLiteral(BP_VERSION_STRING));

#ifdef Q_OS_WIN
	//任务栏分组/图标归属：必须在创建第一个窗口之前设置。
	//不设时 Windows 会把进程归到一个临时 AppID 下，任务栏有时拿不到 exe 的图标
	//（桌面/资源管理器正常、任务栏却是默认图标，就是这一类问题）。
	SetCurrentProcessExplicitAppUserModelID(L"ST-Kdream.BlastPioneer");
#endif

	// 解析启动参数（--root=<目录> 可以在开发时把数据目录指到源码树）
	QCommandLineParser parser;
	parser.setApplicationDescription("BlastPioneer");
	parser.addHelpOption();
	QCommandLineOption rootOption("root", "指定运行时根目录（默认与 exe 同级）", "dir");
	parser.addOption(rootOption);
	//界面自检：构造全部界面后直接退出（给构建/CI 用，不弹窗口）
	QCommandLineOption uiSelftestOption("ui-selftest", "构造所有界面并立即退出（界面结构自检）");
	parser.addOption(uiSelftestOption);
	//安全模式：本次运行不加载任何原生模组（游戏打不开时用得上，见 ABI 契约 §3.3）
	QCommandLineOption safeModeOption("safe-mode", "安全模式：本次不加载原生模组");
	parser.addOption(safeModeOption);
	//直接以一个模组模式开局（开发调试用：省得每次都点"开始游戏 → 选择模式"）
	QCommandLineOption playModeOption("play-mode", "直接进入一个模组模式（格式 <modId>/<modeId>）", "key");
	parser.addOption(playModeOption);
	//界面截图（开发/设计自查用）：把每个界面画出来存成 PNG 后立即退出
	QCommandLineOption uiShotOption("ui-shot", "把每个界面截图存到指定目录后退出（设计自查）", "dir");
	parser.addOption(uiShotOption);
	parser.process(app);

	//目录布局：必须在任何数据访问之前解析
	DirectoryLayout::instance().resolve(parser.value(rootOption));
	DirectoryLayout::instance().ensureRuntimeDirs();

	// 应用图标：必须在解析根目录之后读 —— 原来它写在 resolve() 之前，
	// root() 还是空串，路径变成 "/Assets/picture/..."，文件当然不存在；
	// 结果 exe 的内嵌图标（资源管理器/桌面）正常，窗口/任务栏却退回 Qt 默认图标。
	// qrc 里另存了一份兜底，Assets 被删掉也不会出现默认图标。
	{
		QIcon icon;
		const QString iconPath = DirectoryLayout::instance().root()
			+ QStringLiteral("/Assets/picture/BlastPioneer.ico");
		if (QFileInfo::exists(iconPath)) { icon = QIcon(iconPath); }
		if (icon.availableSizes().isEmpty())
		{
			icon = QIcon(QStringLiteral(":/Assets/picture/BlastPioneer.ico"));
		}
		if (!icon.availableSizes().isEmpty()) { app.setWindowIcon(icon); }
	}

	//界面截图目录（--ui-shot）：留空表示不自检截图
	const QString shotDir = parser.value(QStringLiteral("ui-shot")).trimmed();

	//界面外观：越早越好 —— 它决定基座风格与调色板，
	//在创建任何控件之前应用可以省掉一次全量重绘
	ThemeManager::instance().applyFromSettings();

	//安全模式：命令行开关，或**启动时按住 Shift**。
	//为什么给两种方式：原生模组崩溃后游戏可能再也起不来，玩家得有一条不查文档也能走的路
	//（启动时按住 Shift 是很多软件的惯例；这两条也会写在"首次允许原生模组"的提示里）
	//
	//判定必须在这里做完：原生模组的加载发生在启动管线的工作线程里（BootstrapTask 第 4 步），
	//所以"本次要不要加载原生模组"要在那之前就定下来（契约 §3.3）
	{
		const bool shiftHeld = QGuiApplication::queryKeyboardModifiers().testFlag(Qt::ShiftModifier);
		const bool manualSafeMode = parser.isSet(safeModeOption) || shiftHeld;

		LaunchGuard& guard = LaunchGuard::instance();
		guard.startup(manualSafeMode);
		NativeModHost::instance().setSafeMode(guard.blockNativeMods());

		if (guard.blockNativeMods())
		{
			FileManager::instance().log("boot", QStringLiteral("安全模式启动：本次不加载任何原生模组（%1）")
				.arg(guard.lastExitText()));
		}
	}

	// 预创建核心单例（真正的加载在启动界面的初始化线程里做）
	FileManager::instance();
	SettingsManager::instance();
	PlayerProfile::instance();
	ItemDatabase::instance();
	FigManager::instance();

	// 唯一的顶层窗口
	AppHost host;
	host.initialize();

	// 注册所有界面：跳转方只认 id，不需要知道目标界面的类型
	ScreenRouter& router = ScreenRouter::instance();
	router.registerScreen(ScreenId::Splash, [](QWidget* parent) -> ScreenBase* { return new SplashScreen(parent); });
	router.registerScreen(ScreenId::Welcome, [](QWidget* parent) -> ScreenBase* { return new WelcomeScreen(parent); });
	router.registerScreen(ScreenId::Home, [](QWidget* parent) -> ScreenBase* { return new HomeScreen(parent); });
	router.registerScreen(ScreenId::Player, [](QWidget* parent) -> ScreenBase* { return new PlayerScreen(parent); });
	router.registerScreen(ScreenId::Bag, [](QWidget* parent) -> ScreenBase* { return new BagScreen(parent); });
	router.registerScreen(ScreenId::Shop, [](QWidget* parent) -> ScreenBase* { return new ShopScreen(parent); });
	router.registerScreen(ScreenId::LevelSelect, [](QWidget* parent) -> ScreenBase* { return new LevelSelectScreen(parent); });
	router.registerScreen(ScreenId::ModeSelect, [](QWidget* parent) -> ScreenBase* { return new ModeSelectScreen(parent); });
	router.registerScreen(ScreenId::Mods, [](QWidget* parent) -> ScreenBase* { return new ModsScreen(parent); });
	router.registerScreen(ScreenId::Rules, [](QWidget* parent) -> ScreenBase* { return new RulesScreen(parent); });
	router.registerScreen(ScreenId::Settings, [](QWidget* parent) -> ScreenBase* { return new SettingsScreen(parent); });
	router.registerScreen(ScreenId::Lobby, [](QWidget* parent) -> ScreenBase* { return new LobbyScreen(parent); });
	router.registerScreen(ScreenId::SingleGame, [](QWidget* parent) -> ScreenBase* { return new SingleGameScreen(parent); });
	router.registerScreen(ScreenId::MultiGame, [](QWidget* parent) -> ScreenBase* { return new MultiGameScreen(parent); });

	// 预先创建所有界面：避免第一次进入某界面时才构造，切换更顺滑
	// （图片/位图缓存由 AppHost::initialize 与启动初始化管线负责预热）
	router.precreateAll();

	// 界面自检：--ui-selftest 会把所有界面构造一遍然后直接退出，不进入事件循环。
	// 为什么需要它：界面代码（setupUI、objectName、布局）以前只能靠手点验证，
	// 而"构造时崩溃/漏控件"这类问题完全可以自动发现 —— 构建脚本里跑一次就知道。
	//
	// --ui-shot=<目录>：自检的"看得见"版本 —— 把每个界面依次显示并截图存成 PNG。
	// 为什么需要它：配色/对比度这类问题（"关卡按钮几乎看不见"）构造完是发现不了的，
	// 必须真的画出来看一眼；有了它，改主题或样式表之后可以立刻自查，不用手动点十几个界面。
	if (parser.isSet(QStringLiteral("ui-selftest")) || !shotDir.isEmpty())
	{
		//自检也把模组读一遍，并重新应用一次外观 ——
		//这样"mod 能否改界面"这条链路也能被自动验证（否则它要等启动管线跑完才生效）
		ModManager::instance().load();
		ThemeManager::instance().reload();

		if (!shotDir.isEmpty())
		{
			DirectoryLayout::instance().ensureDir(shotDir);

			//截图要"看起来就像玩家看到的那样"：所以把数据读进来、把淡入动画关掉
			//（不关的话截到的是动画第一帧 —— 整页半透明，什么都看不清）
			SettingsManager::instance().setPageFade(false);
			PlayerProfile::instance().load();
			ItemDatabase::instance().reload();
			LevelDatabase::instance().reload();

			//窗口要真的显示出来才有正确尺寸（布局需要一次真实的 resize/polish）
			host.resize(1000, 900);
			host.show();
			QCoreApplication::processEvents();

			const QStringList shotScreens = {
				ScreenId::Home, ScreenId::ModeSelect, ScreenId::LevelSelect, ScreenId::Player,
				ScreenId::Bag, ScreenId::Shop, ScreenId::Rules, ScreenId::Settings,
				ScreenId::Mods, ScreenId::Lobby, ScreenId::Welcome
			};
			int shots = 0;
			for (const QString& id : shotScreens)
			{
				if (!router.hasScreen(id)) { continue; }
				router.reset(id);

				//让布局、样式表与"待删除的旧控件"都落地：
				//界面里的 rebuild* 用的是 deleteLater，纯 processEvents 不会真的执行删除，
				//截出来的图上会叠着上一轮留下的旧按钮（调试这个工具时踩过）
				for (int pass = 0; pass < 4; ++pass)
				{
					QCoreApplication::processEvents();
					QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
					QThread::msleep(25);
				}

				const QPixmap shot = host.grab();
				const QString path = shotDir + "/" + id + ".png";
				if (shot.save(path))
				{
					++shots;
					FileManager::instance().log("selftest", QStringLiteral("界面截图：%1").arg(path));
				}
			}
			FileManager::instance().log("selftest", QStringLiteral("界面截图完成：%1 张").arg(shots));
		}

		FileManager::instance().log("selftest", QStringLiteral("界面自检：全部 %1 个界面构造完成")
			.arg(ScreenRouter::instance().registeredCount()));

		//自检是"正常结束"：不收尾的话，启动记录会留下 running=true，
		//下一次真正启动就会被判成"上次异常结束"，白白弹一次崩溃提示
		LaunchGuard::instance().markCleanExit();
		return 0;
	}

	// 启动界面强制显示：它会自己做完初始化，再决定进主页还是注册欢迎页
	// --play-mode 会把它捎给启动界面：初始化完成后直接进那个模组模式（调试用）
	{
		const QString playMode = parser.value(playModeOption).trimmed();
		QVariantMap splashArgs;
		if (!playMode.isEmpty())
		{
			splashArgs.insert(QStringLiteral("playMode"), playMode);
			FileManager::instance().log("boot", QStringLiteral("启动参数 --play-mode=%1：初始化后直接进这个模式")
				.arg(playMode));
		}
		router.reset(ScreenId::Splash, splashArgs);
	}
	host.show();

	FileManager::instance().log("app", "程序启动");

	return app.exec();
}
