#include "LevelSelectWindow.h"
#include "MainWindow.h"
#include "GameWindow.h"
#include "SettingsManager.h"

//构造函数
LevelSelectWindow::LevelSelectWindow(PlayerInfo& playerInfo, QWidget* parent) :playerInfo(playerInfo), QWidget(parent)
{
	setWindowFlags(Qt::Window);
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
        restoreGeometry(savedGeo);
    else {
        resize(800, 600);
        move(100, 100);
    }

    title = new QLabel("关卡选择", this);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("font-size: 32px; font-weight: bold; color: #FFCC00;");

    backBtn = new QPushButton("返回", this);
    backBtn->setObjectName("backBtn");
    backBtn->setStyleSheet(R"(
        QPushButton#backBtn {
            background-color: #E65C00;
            color: white;
            font-size: 18px;
            padding: 8px;
            border-radius: 5px;
        }
        QPushButton#backBtn:hover { background-color: #FF7F00; }
        QPushButton#backBtn:pressed { background-color: #B34700; }
    )");

    connect(backBtn, &QPushButton::clicked, this, &LevelSelectWindow::goBack);

    for (int i = 0; i < 6; ++i) 
    {
        levelBtns[i] = new QPushButton(QString("%1").arg(i + 1), this);
    }

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 40, 20, 50);
    mainLayout->setSpacing(20);

    mainLayout->addWidget(title);

    QGridLayout* levelBtnLayout = new QGridLayout();
    levelBtnLayout->setSpacing(20);
    levelBtnLayout->setColumnStretch(0, 1);
    levelBtnLayout->setColumnStretch(1, 1);
    levelBtnLayout->setColumnStretch(2, 1);
    levelBtnLayout->setRowStretch(0, 1);
    levelBtnLayout->setRowStretch(1, 1);

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

    QHBoxLayout* backLayout = new QHBoxLayout();
    backBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    backLayout->addWidget(backBtn);
    mainLayout->addLayout(backLayout);

    setLayout(mainLayout);
}

//关卡按钮UI
void LevelSelectWindow::setupLevelBtn()
{
    for (int i = 0; i < 6; ++i) 
    {
        levelBtns[i]->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        levelBtns[i]->setMinimumSize(80, 80);
        levelBtns[i]->setMaximumSize(120, 120);
        levelBtns[i]->setObjectName(QString("levelBtn_%1").arg(i));
        levelBtns[i]->setStyleSheet(R"(
            QPushButton {
                background-color: #3399FF;
                color: white;
                font-size: 24px;
                border-radius: 50px;
            }
            QPushButton:hover { background-color: #66B2FF; }
            QPushButton:pressed { background-color: #2673CC; }
            QPushButton:disabled { background-color: #AAAAAA; }
        )");
        connect(levelBtns[i], &QPushButton::clicked, this, [this, i]() { goGameWindow(i + 1); });
    }
}

//界面更新
void LevelSelectWindow::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);
	int passedLevels = playerInfo.getPassedLevels();
	for (int i = 0; i < 6; ++i)
	{
		levelBtns[i]->setEnabled(passedLevels >= i);
	}
}

//两个窗口跳转函数
void LevelSelectWindow::goBack()
{
	SettingsManager::instance()->saveWindowGeometry(saveGeometry());

	if (mainWin) {
		mainWin->show();
		mainWin->raise();
		mainWin->activateWindow();
	}

	hide();
}

void LevelSelectWindow::goGameWindow(int level)
{
	if (gameWin)
	{
		gameWin->setLevel(level);
	}
	else
	{
		gameWin = new GameWindow(level, playerInfo, mainWin,this);
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
