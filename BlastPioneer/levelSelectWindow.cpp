#include "LevelSelectWindow.h"

//构造函数
LevelSelectWindow::LevelSelectWindow(PlayerInfo& playerInfo, QWidget* parent) :playerInfo(playerInfo), QWidget(parent)
{
	gameWin = nullptr;
	mainWin = qobject_cast<MainWindow*>(parent);
	setupUI();
	setupLevelBtn();
}


//UI设置（包含返回按钮的信号槽链接，关卡按钮另外设置）
void LevelSelectWindow::setupUI()
{
	setWindowTitle("难度选择");

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

	//标题
	title = new QLabel("关卡选择", this);
	title->setAlignment(Qt::AlignCenter);
	title->setStyleSheet("font-size: 32px; font-weight: bold; color: #FFCC00;");

	//返回按钮
	backBtn = new QPushButton("返回", this);
	backBtn->setStyleSheet(
								"QPushButton { background-color: #E65C00; color: white; font-size: 18px; padding: 8px; border-radius: 5px; }"
								"QPushButton:hover { background-color: #FF7F00; }"
								"QPushButton:pressed { background-color: #B34700; }"
							);
	connect(backBtn, &QPushButton::clicked, this, &LevelSelectWindow::goBack);

	//创建关卡选择按钮
	for (int i = 0; i < 6; ++i)
	{
		levelBtns[i] = new QPushButton(QString("%1").arg(i+1), this);
	}

	//布局
	QVBoxLayout* mainLayout = new QVBoxLayout();
	mainLayout->addWidget(title);

	QGridLayout* levelBtnLayout = new QGridLayout();
	int cnt = 0;
	for (int i = 0; i < 2; ++i)
	{
		for (int j = 0; j < 3; ++j)
		{
			levelBtnLayout->addWidget(levelBtns[cnt], i, j);
			++cnt;
		}
	}
	mainLayout->addLayout(levelBtnLayout);

	mainLayout->addWidget(backBtn, Qt::AlignRight);
	setLayout(mainLayout);
}

//按钮设置（样式及初始化）
void LevelSelectWindow::setupLevelBtn()
{
	for (int i = 0; i < 6; ++i) 
	{
		levelBtns[i]->setStyleSheet(
										"QPushButton { background-color: #3399FF; color: white; font-size: 18px; border-radius: 75px; }"
										"QPushButton:hover { background-color: #66B2FF; }"
										"QPushButton:pressed { background-color: #2673CC; }"
										"QPushButton:disabled { background-color: #AAAAAA; }" 
									);
		connect(levelBtns[i], &QPushButton::clicked, this, [this, i]() {goGameWindow(i + 1); });
	}
}

//界面更新
void LevelSelectWindow::showEvent(QShowEvent* event)
{
	//未实现
	QWidget::showEvent(event);
}

//两个窗口跳转函数
void LevelSelectWindow::goBack()
{
	this->hide();
	// 保存窗口设置
	SettingsManager::instance()->saveWindowGeometry(saveGeometry());

	if (mainWin)
	{
		mainWin->show();
		mainWin->raise();
		mainWin->activateWindow();
	}
}

void LevelSelectWindow::goGameWindow(int level)
{
	if (gameWin)
	{
		gameWin->setLevel(level);
	}
	else
	{
		gameWin = new GameWindow(level, playerInfo, this);   //这里有bug，记得修
		gameWin->setAttribute(Qt::WA_DeleteOnClose);
		connect(gameWin, &QObject::destroyed, this, [this]() 
			{
				gameWin = nullptr; 
				this->show();
				this->raise();
				this->activateWindow();
			});
	}
	
	this->hide();
	// 保存窗口设置
	SettingsManager::instance()->saveWindowGeometry(saveGeometry());

	gameWin->show();
	gameWin->raise();
	gameWin->activateWindow();
}
