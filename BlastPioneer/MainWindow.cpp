#include "MainWindow.h"

//构造函数
MainWindow::MainWindow(QWidget* parent)
{
	setupUI();
	btnConnect();
}

//UI设置函数
void MainWindow::setupUI()
{
	setWindowTitle("爆破先锋-首页");
	setFixedSize(800, 600);

	//创建控件
	btn1 = new QPushButton("玩家信息", this);
	btn2 = new QPushButton("单人游戏", this);
	btn3 = new QPushButton("多人游戏", this);
	btn4 = new QPushButton("游戏规则", this);
	btn5 = new QPushButton("⚙", this);
	mainTitle = new QLabel("爆破先锋", this);

	//样式设置
	mainTitle->setStyleSheet(R"(color: #FFCC00; font-weight: bold; text-align: center;)");    //标题样式
	mainTitle->setToolTip("欢迎来到爆破先锋小游戏！");

	btn1->setToolTip("点击此处查看玩家个人信息");        //设置悬浮提示
	btn2->setToolTip("点击此处开始游戏并选择游戏模式");
	btn3->setToolTip("点击此处查看游戏规则");
	btn4->setToolTip("设置");

	btn1->setObjectName("playerBtn");
	btn2->setObjectName("startBtn");
	btn3->setObjectName("startBtn");
	btn4->setObjectName("rulesBtn");

	QString mainStyle = R"(
								QPushButton{color: #000000; border-radius: 3px;}
								#playerBtn{background-color: #33CCFF;}
								#playerBtn:hover{background-color: #66D9FF;}
								#playerBtn:pressed{background-color: #2699CC;}
								#startBtn{background-color: #FFCC00;}
								#startBtn:hover{background-color: #FFF000;}
								#startBtn:pressed{background-color: #CC9900;}
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

	//布局（标题最上面，3行按钮垂直排布（按钮二、三在同一行），btn4在右下角）

	QHBoxLayout* settingLayout = new QHBoxLayout(this);  //第五个设置按钮布局
	settingLayout->addStretch();
	settingLayout->addWidget(btn5);

	QHBoxLayout* startLayout = new QHBoxLayout(this);
	startLayout->addWidget(btn2);
	startLayout->addWidget(btn3);

	QVBoxLayout* mainLayout = new QVBoxLayout(this);
	mainLayout->addWidget(mainTitle, 2);
	mainLayout->addWidget(btn1, 1);
	mainLayout->addLayout(startLayout, 2);
	mainLayout->addWidget(btn4, 1);
	mainLayout->addLayout(settingLayout);
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

//5个切换窗口的函数
void MainWindow::GoPlayerWindow()
{

}

void MainWindow::GoSingleGame()
{

}

void MainWindow::GoInternetGame()
{

}

void MainWindow::GoRulesWindow()
{
	this->hide();
	RulesWindow* rulesWin = new RulesWindow(this);
	rulesWin->setAttribute(Qt::WA_DeleteOnClose);
	if (rulesWin)
	{
		rulesWin->show();
		rulesWin->raise();
		rulesWin->activateWindow();
	}
}

void MainWindow::GoSettingsWindow()
{

}