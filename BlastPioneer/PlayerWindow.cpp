#include "PlayerWindow.h"
#include "MainWindow.h"
#include "PlayerInfo.h"
#include "BagWindow.h"
#include "ShopWindow.h"
#include "SettingsManager.h"
#include <QApplication>

//构造函数
PlayerWindow::PlayerWindow(PlayerInfo& playerInfo, QWidget* parent) : QWidget(parent), playerInfo(playerInfo)
{
	setupUI();
	connectBtn();
	setWindowFlags(Qt::Window);
	setAttribute(Qt::WA_ShowWithoutActivating, false);
}

//UI设置
void PlayerWindow::setupUI()
{
    setWindowTitle("玩家信息");

    QByteArray savedGeo = SettingsManager::instance()->loadWindowGeometry();
    if (!savedGeo.isEmpty())
        restoreGeometry(savedGeo);
    else {
        resize(800, 600);
        move(100, 100);
    }

    // 控件创建
    backBtn = new QPushButton("返回", this);
    bagBtn = new QPushButton("背包", this);
    shopBtn = new QPushButton("商店", this);
    newNameBtn = new QPushButton("更改名字", this);

    QLabel* userNameLabel = new QLabel("id：", this);
    QLabel* coinsLabel = new QLabel("金币：", this);
    QLabel* rankLabel = new QLabel("段位：", this);
    QLabel* EPLabel = new QLabel("经验：", this);
    QLabel* passedLevelLabel = new QLabel("已通过关卡数：", this);

    userName = new QLabel(playerInfo.getUserName(), this);
    coins = new QLabel(QString::number(playerInfo.getCoins()), this);
    rank = new QLabel(playerInfo.getRank(), this);
    EP = new QLabel(QString::number(playerInfo.getEP()), this);
    passedLevels = new QLabel(QString::number(playerInfo.getPassedLevels()), this);

    // 设置对象名
    backBtn->setObjectName("backBtn");
    bagBtn->setObjectName("bagBtn");
    shopBtn->setObjectName("shopBtn");
    newNameBtn->setObjectName("newNameBtn");

    // 统一样式（使用对象名选择器）
    QString btnStyle = R"(
        QPushButton {
            font-size: 16px;
            padding: 8px 16px;
            border: none;
            border-radius: 6px;
            color: #000000;
        }
        #backBtn { background-color: #E65C00; }
        #backBtn:hover { background-color: #FF7F00; }
        #backBtn:pressed { background-color: #B34700; }
        #bagBtn { background-color: #FFCC00; }
        #bagBtn:hover { background-color: #FFF000; }
        #bagBtn:pressed { background-color: #CC9900; }
        #shopBtn { background-color: #3399FF; }
        #shopBtn:hover { background-color: #66B2FF; }
        #shopBtn:pressed { background-color: #2673CC; }
        #newNameBtn { background-color: #CC66FF; }
        #newNameBtn:hover { background-color: #D999FF; }
        #newNameBtn:pressed { background-color: #994DCC; }
    )";

    backBtn->setStyleSheet(btnStyle);
    bagBtn->setStyleSheet(btnStyle);
    shopBtn->setStyleSheet(btnStyle);
    newNameBtn->setStyleSheet(btnStyle);

    // 布局
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 40, 20, 50);
    mainLayout->setSpacing(15);

    QHBoxLayout* userNameLine = new QHBoxLayout();
    userNameLine->addWidget(userNameLabel);
    userNameLine->addWidget(userName);
    userNameLine->addWidget(newNameBtn);
    userNameLine->setStretch(0, 0);
    userNameLine->setStretch(1, 1);
    mainLayout->addLayout(userNameLine);

    QHBoxLayout* coinsLine = new QHBoxLayout();
    coinsLine->addWidget(coinsLabel);
    coinsLine->addWidget(coins);
    coinsLine->setStretch(0, 0);
    coinsLine->setStretch(1, 1);
    mainLayout->addLayout(coinsLine);

    QHBoxLayout* rankLine = new QHBoxLayout();
    rankLine->addWidget(rankLabel);
    rankLine->addWidget(rank);
    rankLine->setStretch(0, 0);
    rankLine->setStretch(1, 1);
    mainLayout->addLayout(rankLine);

    QHBoxLayout* EPLine = new QHBoxLayout();
    EPLine->addWidget(EPLabel);
    EPLine->addWidget(EP);
    EPLine->setStretch(0, 0);
    EPLine->setStretch(1, 1);
    mainLayout->addLayout(EPLine);

    QHBoxLayout* passedLevelsLine = new QHBoxLayout();
    passedLevelsLine->addWidget(passedLevelLabel);
    passedLevelsLine->addWidget(passedLevels);
    passedLevelsLine->setStretch(0, 0);
    passedLevelsLine->setStretch(1, 1);
    mainLayout->addLayout(passedLevelsLine);

    QHBoxLayout* btnLine = new QHBoxLayout();
    btnLine->setSpacing(20);
    for (auto btn : { bagBtn, shopBtn, backBtn }) {
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        btnLine->addWidget(btn);
    }
    mainLayout->addLayout(btnLine);

    setLayout(mainLayout);
}

//链接信号槽函数
void PlayerWindow::connectBtn()
{
	connect(backBtn, &QPushButton::clicked, this, &PlayerWindow::goBack);
	connect(bagBtn, &QPushButton::clicked, this, &PlayerWindow::goBag);
	connect(shopBtn, &QPushButton::clicked, this, &PlayerWindow::goShopping);
	connect(newNameBtn, &QPushButton::clicked, this, &PlayerWindow::changeName);
}

//让MainWindow传入指针
void PlayerWindow::setMainWin(MainWindow* mainWin)
{
	this->mainWin = mainWin;
}

//保存玩家信息函数
void PlayerWindow::savePlayerInfo()
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

//展示窗口时更新数据
void PlayerWindow::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);
	updateUI();
}

//关闭窗口时保存玩家信息
void PlayerWindow::closeEvent(QCloseEvent* event)
{
	SettingsManager::instance()->saveWindowGeometry(saveGeometry());
	savePlayerInfo();
	QWidget::closeEvent(event);
}

//UI更新函数
void PlayerWindow::updateUI()
{
	userName->setText(playerInfo.getUserName());
	coins->setText(QString::number(playerInfo.getCoins()));
	rank->setText(playerInfo.getRank());
	EP->setText(QString::number(playerInfo.getEP()));
	passedLevels->setText(QString::number(playerInfo.getPassedLevels()));
}


//更改玩家姓名函数
void PlayerWindow::changeName()
{
	QDialog newNameInput(this);
	newNameInput.setWindowTitle("更改姓名");
	newNameInput.setFixedSize(300, 150);

	QVBoxLayout* mainLayout = new QVBoxLayout(&newNameInput);
	QLineEdit* nameInput = new QLineEdit(&newNameInput);
	nameInput->setPlaceholderText("输入新姓名");
	QDialogButtonBox* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &newNameInput);

	connect(btnBox, &QDialogButtonBox::accepted, &newNameInput, &QDialog::accept);
	connect(btnBox, &QDialogButtonBox::rejected, &newNameInput, &QDialog::reject);

	mainLayout->addWidget(nameInput);
	mainLayout->addWidget(btnBox);
	newNameInput.setLayout(mainLayout);

	if (newNameInput.exec() == QDialog::Accepted) 
	{
		QString newName = nameInput->text().trimmed();
		if (newName.isEmpty()) 
		{
			QMessageBox::warning(this, "不合法的姓名", "姓名不能为空");
		}
		else 
		{
			playerInfo.setUserName(newName);
			userName->setText(newName);
			savePlayerInfo();
		}
	}
}



//3个窗口跳转函数
void PlayerWindow::goBack()
{
	// 保存数据
	savePlayerInfo();
	SettingsManager::instance()->saveWindowGeometry(saveGeometry());

	if (mainWin) 
	{
		mainWin->show();
		mainWin->raise();
		mainWin->activateWindow();
	}

	hide();
}

void PlayerWindow::goBag()
{
	this->hide();
	// 保存窗口设置
	SettingsManager::instance()->saveWindowGeometry(saveGeometry());

	bagWin = mainWin ? mainWin->getBagWin() : nullptr;
	if (!bagWin)
	{
		bagWin = new BagWindow(playerInfo, this, this);
		bagWin->setAttribute(Qt::WA_DeleteOnClose);
		connect(bagWin, &QObject::destroyed, this, [this]() {bagWin = nullptr; });
	}
	bagWin->show();
	bagWin->raise();
	bagWin->activateWindow();
}

void PlayerWindow::goShopping()
{
	this->hide();
	SettingsManager::instance()->saveWindowGeometry(saveGeometry());

	if (!shopWin) 
	{
		shopWin = new ShopWindow(playerInfo, this, this);
		shopWin->setAttribute(Qt::WA_DeleteOnClose);
		shopWin->setMainWin(mainWin);
		connect(shopWin, &QObject::destroyed, this, [this]() { shopWin = nullptr; });
	}
	shopWin->show();
	shopWin->raise();
	shopWin->activateWindow();
}
