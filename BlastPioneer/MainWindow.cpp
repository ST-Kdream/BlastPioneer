#include "MainWindow.h"
#include "RulesWindow.h"
#include "PlayerWindow.h"
#include "LevelSelectWindow.h"
#include "SettingsManager.h"
#include "SettingsWindow.h"
#include "BagWindow.h"
#include "ShopWindow.h"
#include "LobbyWindow.h"
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
    lobbyWin = nullptr;
	setupUI();
	btnConnect();
	createWindows();
}

//UI设置函数
void MainWindow::setupUI()
{
    setWindowTitle("爆破先锋-首页");

    QByteArray savedGeo = SettingsManager::instance()->loadWindowGeometry();
    if (!savedGeo.isEmpty()) restoreGeometry(savedGeo);
    else { resize(800, 600); move(100, 100); }

    // 创建控件
    btn1 = new QPushButton("玩家信息", this);
    btn2 = new QPushButton("单人游戏", this);
    btn3 = new QPushButton("多人游戏", this);
    btn4 = new QPushButton("游戏规则", this);
    btn5 = new QPushButton("设置", this);
    mainTitle = new QLabel("爆破先锋", this);

    btn1->setObjectName("playerBtn");
    btn2->setObjectName("singleBtn");
    btn3->setObjectName("multiBtn");
    btn4->setObjectName("rulesBtn");
    btn5->setObjectName("settingsBtn");
    mainTitle->setObjectName("titleLabel");

    // 标题样式
    mainTitle->setStyleSheet(R"(
        QLabel#titleLabel {
            font-size: 28px;
            font-weight: bold;
            color: #2C3E50;
            padding: 10px 0 5px 0;
            qproperty-alignment: AlignCenter;
        }
    )");

    // 按钮样式
    btn1->setStyleSheet(R"(
        QPushButton#playerBtn {
            font-size: 18px;
            padding: 12px 24px;
            min-width: 160px;
            border: none;
            border-radius: 10px;
            background-color: #33CCFF;
            color: white;
        }
        QPushButton#playerBtn:hover { background-color: #66D9FF; }
        QPushButton#playerBtn:pressed { background-color: #2699CC; }
    )");

    btn2->setStyleSheet(R"(
        QPushButton#singleBtn {
            font-size: 18px;
            padding: 12px 24px;
            min-width: 160px;
            border: none;
            border-radius: 10px;
            background-color: #FFCC00;
            color: white;
        }
        QPushButton#singleBtn:hover { background-color: #FFE066; }
        QPushButton#singleBtn:pressed { background-color: #CC9900; }
    )");

    btn3->setStyleSheet(R"(
        QPushButton#multiBtn {
            font-size: 18px;
            padding: 12px 24px;
            min-width: 160px;
            border: none;
            border-radius: 10px;
            background-color: #66FF66;
            color: #333;
        }
        QPushButton#multiBtn:hover { background-color: #99FF99; }
        QPushButton#multiBtn:pressed { background-color: #66CC66; }
    )");

    btn4->setStyleSheet(R"(
        QPushButton#rulesBtn {
            font-size: 18px;
            padding: 12px 24px;
            min-width: 160px;
            border: none;
            border-radius: 10px;
            background-color: #CC66FF;
            color: white;
        }
        QPushButton#rulesBtn:hover { background-color: #D999FF; }
        QPushButton#rulesBtn:pressed { background-color: #994DCC; }
    )");

    btn5->setStyleSheet(R"(
        QPushButton#settingsBtn {
            font-size: 18px;
            padding: 12px 24px;
            min-width: 160px;
            border: none;
            border-radius: 10px;
            background-color: #999999;
            color: white;
        }
        QPushButton#settingsBtn:hover { background-color: #AAAAAA; }
        QPushButton#settingsBtn:pressed { background-color: #777777; }
    )");

    // 布局
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 40, 20, 50);
    mainLayout->setSpacing(20);

    mainLayout->addWidget(mainTitle, 0, Qt::AlignHCenter);

    // 第一行：玩家信息
    QHBoxLayout* row1 = new QHBoxLayout();
    btn1->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    row1->addWidget(btn1);
    mainLayout->addLayout(row1);

    // 第二行：单人 + 多人
    QHBoxLayout* row2 = new QHBoxLayout();
    row2->setSpacing(40);
    btn2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    btn3->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    row2->addWidget(btn2);
    row2->addWidget(btn3);
    mainLayout->addLayout(row2);

    // 第三行：规则 + 设置
    QHBoxLayout* row3 = new QHBoxLayout();
    row3->setSpacing(40);
    btn4->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    btn5->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    row3->addWidget(btn4);
    row3->addWidget(btn5);
    mainLayout->addLayout(row3);

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
	playerWin->setMainWin(this);
	playerWin->hide();
	playerWin->setAttribute(Qt::WA_DeleteOnClose);
	//关卡选择窗口
	levelSelectWin = new LevelSelectWindow(playerInfo, this);
	levelSelectWin->hide();
	levelSelectWin->setAttribute(Qt::WA_DeleteOnClose);
	//背包窗口
	bagWin = new BagWindow(playerInfo, this, playerWin);
	bagWin->setMainWin(this);
	bagWin->hide();
	bagWin->setAttribute(Qt::WA_DeleteOnClose);
	//商店窗口
	shopWin = new ShopWindow(playerInfo, this, playerWin);
	shopWin->setMainWin(this);
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

void MainWindow::savePlayerData()
{
	QString filePath = QCoreApplication::applicationDirPath() + "/Data/playerData.json";
	QFile file(filePath);
	if (file.open(QIODevice::WriteOnly))
	{
		QJsonDocument doc(playerInfo.toJson());
		file.write(doc.toJson());
		file.close();
	}
}

//5个切换窗口的函数
void MainWindow::GoPlayerWindow()
{
	this->hide();

	if (!playerWin)
	{
		playerWin = new PlayerWindow(playerInfo, nullptr);
		playerWin->setAttribute(Qt::WA_DeleteOnClose);
		playerWin->setMainWin(this); 

		connect(playerWin, &QObject::destroyed, this, [this]() {playerWin = nullptr; });
	}

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
    if (!lobbyWin) {
        lobbyWin = new LobbyWindow(this, this);
        lobbyWin->setAttribute(Qt::WA_DeleteOnClose);
        // 当大厅窗口关闭时，重新显示主窗口
        connect(lobbyWin, &QObject::destroyed, this, [this]() {
            lobbyWin = nullptr;
            this->show();
            });
    }
    this->hide();           // 隐藏主窗口
    lobbyWin->show();
    lobbyWin->raise();
    lobbyWin->activateWindow();
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
