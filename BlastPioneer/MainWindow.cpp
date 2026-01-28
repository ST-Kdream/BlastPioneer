#include "MainWindow.h"

//构造函数
MainWindow::MainWindow(QWidget* parent)
{
	setWindowTitle("爆破先锋-首页");
	setFixedSize(800, 600);
	btnConnect();

	//创建控件
	btn1 = new QPushButton("玩家信息", this);
	btn2 = new QPushButton("开始游戏", this);
	btn3 = new QPushButton("游戏规则", this);
	btn4 = new QPushButton("⚙",this);
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
	btn3->setObjectName("rulesBtn");

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
						   )";					//前三个按钮样式表

	btn1->setStyleSheet(mainStyle);
	btn2->setStyleSheet(mainStyle);
	btn3->setStyleSheet(mainStyle);

	btn4->setFixedSize(60, 60);
	btn4->setStyleSheet(R"(
								QPushButton{color:#000000; border-radius: 30px; background-color: #CC66FF;}
								QPushButton:hover{background-color: #D999FF;}
								QPushButton:pressed{background-color: #994DCC;}
						   )");

	//布局（标题最上面，3个按钮垂直排布，btn4在右下角）
	QVBoxLayout* mainLayout = new QVBoxLayout(this);
	mainLayout->addWidget(mainTitle, 2);
	mainLayout->addWidget(btn1, 1);
	mainLayout->addWidget(btn2, 1);
	mainLayout->addWidget(btn3, 1);

	QHBoxLayout* settingLayout = new QHBoxLayout(this);  //第四个设置按钮布局
	settingLayout->addStretch();
	settingLayout->addWidget(btn4);

	mainLayout->addLayout(settingLayout);
	setLayout(mainLayout);
}

//信号槽链接函数
void MainWindow::btnConnect()
{
	connect(btn1, &QPushButton::clicked, this, &MainWindow::GoPlayerWindow);
	connect(btn2, &QPushButton::clicked, this, &MainWindow::GoGamemodeWindow);
	connect(btn3, &QPushButton::clicked, this, &MainWindow::GoRulesWindow);
	connect(btn4, &QPushButton::clicked, this, &MainWindow::GoSettingsWindow);
}

//4个切换窗口的函数
void MainWindow::GoPlayerWindow()
{

}

void MainWindow::GoGamemodeWindow()
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