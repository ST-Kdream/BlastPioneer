#include "MainWindow.h"
#include "RulesWindow.h"
#include "PlayerWindow.h"
#include "LevelSelectWindow.h"
#include "SettingsManager.h"
#include "SettingsWindow.h"
#include "BagWindow.h"
#include "ShopWindow.h"
#include <QApplication>

//构造函数（初始化）
MainWindow::MainWindow(const PlayerInfo& playerInfo, const QList<Item>& items, QWidget* parent) :
	QWidget(parent), playerInfo(playerInfo), itemList(items)
{
	playerWin = nullptr;
	levelSelectWin = nullptr;
	rulesWin = nullptr;
	bagWin = nullptr;
	shopWin = nullptr;
	settingsWin = nullptr;
	setupUI();
	btnConnect();
	createWindows();
}

//UI设置函数
void MainWindow::setupUI()
{
	setWindowTitle("爆破先锋-首页");
	QByteArray savedGeo = SettingsManager::instance()->loadWindowGeometry();
	if (!savedGeo.isEmpty())
	{
		restoreGeometry(savedGeo);
	}
	else
	{
		resize(800, 600);
		move(100, 100);
	}

	//创建控件
	btn1 = new QPushButton("玩家信息", this);
	btn2 = new QPushButton("单人游戏", this);
	btn3 = new QPushButton("多人游戏", this);
	btn4 = new QPushButton("游戏规则", this);
	btn5 = new QPushButton("设置", this);
	mainTitle = new QLabel("爆破先锋", this);

	//样式设置
	mainTitle->setStyleSheet(R"(color: #FFCC00; font-weight: bold; text-align: center;)");    //标题样式
	mainTitle->setToolTip("欢迎来到爆破先锋小游戏！");

	btn1->setToolTip("点击此处查看玩家个人信息");        //设置悬浮提示
	btn2->setToolTip("点击此处开始单人游戏");
	btn3->setToolTip("点击此处开始多人游戏");
	btn4->setToolTip("点击此处查看游戏规则");
	btn5->setToolTip("设置");

	btn1->setObjectName("playerBtn");
	btn2->setObjectName("startBtn1");
	btn3->setObjectName("startBtn2");
	btn4->setObjectName("rulesBtn");

	
	QString mainStyle = R"(
								QPushButton{color: #000000; border-radius: 3px;}
								#playerBtn{background-color: #33CCFF;}
								#playerBtn:hover{background-color: #66D9FF;}
								#playerBtn:pressed{background-color: #2699CC;}
								#startBtn1{background-color: #FFCC00;}
								#startBtn1:hover{background-color: #FFF000;}
								#startBtn1:pressed{background-color: #CC9900;}
								#startBtn2{background-color: #FFCC00;}
								#startBtn2:hover{background-color: #FFF000;}
								#startBtn2:pressed{background-color: #CC9900;}
								#rulesBtn{background-color: #33FF66;}
								#rulesBtn:hover{background-color: #66FF99;}
								#rulesBtn:pressed{background-color: #26CC66;}
						   )";					//前四个按钮样式表（按钮二、三一致）

	btn1->setStyleSheet(mainStyle);
	btn2->setStyleSheet(mainStyle);
	btn3->setStyleSheet(mainStyle);
	

	btn4->setFixedSize(60, 60);
	
	btn4->setStyleSheet(R"(
								QPushButton{color:#000000; border-radius: 30px; background-color: #CC66FF;}
								QPushButton:hover{background-color: #D999FF;}
								QPushButton:pressed{background-color: #994DCC;}
						   )");
	

	//布局（标题最上面，3行按钮垂直排布（按钮二、三在同一行），btn5在右下角）
	
	QHBoxLayout* startLayout = new QHBoxLayout();
	startLayout->addWidget(btn2);
	startLayout->addWidget(btn3);

	QVBoxLayout* mainLayout = new QVBoxLayout(this);
	mainLayout->addWidget(mainTitle);
	mainLayout->addWidget(btn1);
	mainLayout->addLayout(startLayout);
	mainLayout->addWidget(btn4);
	mainLayout->addWidget(btn5);
	setLayout(mainLayout);
	
}

//信号槽链接函数
void MainWindow::btnConnect()
{
	connect(btn1, &QPushButton::clicked, this, &MainWindow::GoPlayerWindow);
	connect(btn2, &QPushButton::clicked, this, &MainWindow::GoSingleGame);
	connect(btn3, &QPushButton::clicked, this, &MainWindow::GoInternetGame);
	connect(btn4, &QPushButton::clicked, this, &MainWindow::GoRulesWindow);
	connect(btn5, &QPushButton::clicked, this, &MainWindow::GoSettingsWindow);
}

//创建常用窗口
void MainWindow::createWindows()
{
	//玩家窗口
	playerWin = new PlayerWindow(playerInfo, this);
	playerWin->hide();
	playerWin->setAttribute(Qt::WA_DeleteOnClose);
	//关卡选择窗口
	levelSelectWin = new LevelSelectWindow(playerInfo, this);
	levelSelectWin->hide();
	levelSelectWin->setAttribute(Qt::WA_DeleteOnClose);
	//背包窗口
	bagWin = new BagWindow(playerInfo, this, playerWin);
	bagWin->hide();
	bagWin->setAttribute(Qt::WA_DeleteOnClose);
	//商店窗口
	shopWin = new ShopWindow(playerInfo, this, playerWin);
	shopWin->hide();
	shopWin->setAttribute(Qt::WA_DeleteOnClose);
}

//查找道具
Item* MainWindow::getItem(const QString& name)
{
	for (auto& item : itemList)
	{
		if (item.getName() == name)
		{
			return &item;
		}
	}
	return nullptr;
}

//5个切换窗口的函数
void MainWindow::GoPlayerWindow()
{
	this->hide();

	playerWin = new PlayerWindow(playerInfo, nullptr);
	playerWin->setAttribute(Qt::WA_DeleteOnClose);
	playerWin->setMainWin(this); // 关键：传入主窗口指针

	connect(playerWin, &QObject::destroyed, this, [this]() {playerWin = nullptr;});

	playerWin->show();
	playerWin->raise();
	playerWin->activateWindow();

}

void MainWindow::GoSingleGame()
{
	this->hide();
	if (!levelSelectWin)
	{
		levelSelectWin = new LevelSelectWindow(playerInfo,this);
		levelSelectWin->setAttribute(Qt::WA_DeleteOnClose);
		connect(levelSelectWin, &QObject::destroyed, this, [this]() {levelSelectWin = nullptr; this->show(); });
	}
	levelSelectWin->show();
	levelSelectWin->raise();
	levelSelectWin->activateWindow();
}

void MainWindow::GoInternetGame()
{
	QMessageBox::information(this, "提示", "暂未实现");
}

void MainWindow::GoRulesWindow()
{
	this->hide();
	if (!rulesWin)
	{
		rulesWin = new RulesWindow(this, this);
		rulesWin->setAttribute(Qt::WA_DeleteOnClose);
		connect(rulesWin, &QObject::destroyed, this, [this]() {rulesWin = nullptr; this->show(); });
	}
	rulesWin->show();
	rulesWin->raise();
	rulesWin->activateWindow();
}

void MainWindow::GoSettingsWindow()
{
	if (!settingsWin)
	{
		settingsWin = new SettingsWindow(this);
		settingsWin->setAttribute(Qt::WA_DeleteOnClose);
		connect(settingsWin, &QObject::destroyed, this, [this]() {settingsWin = nullptr; });
	}
	settingsWin->show();
	settingsWin->raise();
	settingsWin->activateWindow();
}


void MainWindow::showEvent(QShowEvent* event)
{
	QByteArray savedGeo = SettingsManager::instance()->loadWindowGeometry();
	if (!savedGeo.isEmpty())
	{
		restoreGeometry(savedGeo);
	}
	else
	{
		resize(800, 600);
		move(100, 100);
	}
	QWidget::showEvent(event);
}
