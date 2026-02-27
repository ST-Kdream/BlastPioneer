#include "StartupEffectWindow.h"

//构造函数
StartupEffectWindow::StartupEffectWindow(QWidget* parent) :
	QQuickWidget(parent), minTimer(nullptr), maxTimer(nullptr),
	loaderThread(nullptr), dataLoader(nullptr), isDataLoaded(false), isMinTimePassed(false)
{
	setSource(QUrl("qrc:/startupEffect.qml"));
	setResizeMode(QQuickWidget::SizeRootObjectToView);
	setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
	setFixedSize(800, 600);

	//向QML传递特效质量
	int quality = SettingsManager::instance()->effectQuality();
	rootContext()->setContextProperty("effectQuality", quality);

	//创建计时器
	minTimer = new QTimer(this);
	minTimer->setSingleShot(true);
	connect(minTimer, &QTimer::timeout, this, &StartupEffectWindow::minTimeout);
	maxTimer = new QTimer(this);
	maxTimer->setSingleShot(true);
	connect(maxTimer, &QTimer::timeout, this, &StartupEffectWindow::maxTimeout);

	//创建新线程并加载数据
	loaderThread = new QThread(this);
	dataLoader = new DataLoader();
	dataLoader->moveToThread(loaderThread);
	connect(loaderThread, &QThread::started, dataLoader, &DataLoader::load);
	connect(dataLoader, &DataLoader::finished, &StartupEffectWindow::onDataLoaded);
}

//析构函数
StartupEffectWindow::~StartupEffectWindow()
{
	if (loaderThread->isRunning())
	{
		loaderThread->quit();
		loaderThread->wait();
	}
	delete dataLoader;
}

//开始显示
void StartupEffectWindow::start()
{
	show();
	loaderThread->start();
	minTimer->start(5000);
	maxTimer->start(15000);
}

//检查数据是否加载完毕
void StartupEffectWindow::onDataLoaded(const PlayerInfo& playerInfo, const QList<Item>& items)
{
	emit dataLoaded(playerInfo, items);
	isDataLoaded = true;
	if (isMinTimePassed) { closeWindow(); }
}

//超过最短显示时间
void StartupEffectWindow::minTimeout()
{
	isMinTimePassed = true;
	if (isDataLoaded) { closeWindow(); }
}

//超过最大显示时间
void StartupEffectWindow::maxTimeout()
{
	closeWindow();
}

//关闭窗口
void StartupEffectWindow::closeWindow()
{
	if (loaderThread->isRunning())
	{
		loaderThread->quit();
		loaderThread->wait();
	}
	emit finished();
	close();
}