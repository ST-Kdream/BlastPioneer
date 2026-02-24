#include "PlayerWindow.h"

//构造函数
PlayerWindow::PlayerWindow(MainWindow* mainWin, QWidget* parent): QWidget(parent), mainWin(mainWin)
{
	bagBtn = nullptr;
	shopWin = nullptr;
	getPlayerInfo();
	setupUI();
	connectBtn();
}

//UI设置
void PlayerWindow::setupUI()
{
	setWindowTitle("玩家信息");

	// 设置默认大小，如果QSettings中没有保存的值则使用默认值
	int defaultWidth = 800;
	int defaultHeight = 600;
	int width = settings->value("windows/width", defaultWidth).toInt();
	int height = settings->value("windows/height", defaultHeight).toInt();
	resize(width, height);

	// 恢复窗口位置
	QPoint defaultPos(100, 100);  // 默认位置
	QPoint savedPos = settings->value("windows/position", defaultPos).toPoint();
	move(savedPos);

	//3+1个按钮
	backBtn = new QPushButton("返回", this);
	bagBtn = new QPushButton("背包", this);
	shopBtn = new QPushButton("商店", this);
	newNameBtn = new QPushButton("更改名字", this);

	//玩家信息（标题加数据）
	QLabel* userNameLabel=new QLabel("id：", this);
	QLabel* coinsLabel = new QLabel("金币：", this);
	QLabel* rankLabel = new QLabel("段位：", this);
	QLabel* EPLabel = new QLabel("经验：", this);
	QLabel* passedLevelLabel = new QLabel("已通过关卡数：", this);

	userName = new QLabel(playerInfo.getUserName(), this);
	coins = new QLabel(QString::number(playerInfo.getCoins()), this);
	rank = new QLabel(playerInfo.getRank(), this);
	EP = new QLabel(QString::number(playerInfo.getEP()), this);
	passedLevels = new QLabel(QString::number(playerInfo.getPassedLevels()), this);

	//4个按钮设置样式
	backBtn->setObjectName("backBtn");
	bagBtn->setObjectName("bagBtn");
	shopBtn->setObjectName("shopBtn");

	QString mainStyle = R"(
								QPushButton { font-size: 16px; color: #000000;padding: 8px 16px;border: none;
											  border-radius: 6px; text-shadow: 1px 1px 0 #ffffff33; }
								#backBtn { background-color: #E65C00; }
								#backBtn:hover { background-color: #FF7F00; }
								#backBtn:pressed { background-color: #B34700; }
								#bagBtn { background-color: #FFCC00; }
								#bagBtn:hover { background-color: #FFF000; }
								#bagBtn:pressed { background-color: #CC9900; }
								#shopBtn { background-color: #3399FF; }
								#shopBtn:hover { background-color: #66B2FF; }
								#shopBtn:pressed { background-color: #2673CC; }
						)";   //前3个按钮样式表

	backBtn->setStyleSheet(mainStyle);
	bagBtn->setStyleSheet(mainStyle);
	shopBtn->setStyleSheet(mainStyle);
	newNameBtn->setStyleSheet(	R"(QPushButton { font-size: 16px; color: #000000; border-radius: 6px; background-color: #CC66FF; }
								   QPushButton:hover { background-color: #D999FF; }
								   QPushButton:pressed { background-color: #994DCC; }
								)");

	//布局
	QVBoxLayout* mainLayout = new QVBoxLayout();

	QHBoxLayout* userNameLine = new QHBoxLayout();
	userNameLine->addWidget(userNameLabel);
	userNameLine->addWidget(userName);
	userNameLine->addWidget(newNameBtn);

	QHBoxLayout* coinsLine = new QHBoxLayout();
	coinsLine->addWidget(coinsLabel);
	coinsLine->addWidget(coins);

	QHBoxLayout* rankLine = new QHBoxLayout();
	rankLine->addWidget(rankLabel);
	rankLine->addWidget(rank);

	QHBoxLayout* EPLine = new QHBoxLayout();
	EPLine->addWidget(EPLabel);
	EPLine->addWidget(EP);

	QHBoxLayout* passedLevelsLine = new QHBoxLayout();
	passedLevelsLine->addWidget(passedLevelLabel);
	passedLevelsLine->addWidget(passedLevels);

	QHBoxLayout* btnLine = new QHBoxLayout();
	btnLine->addWidget(bagBtn);
	btnLine->addWidget(shopBtn);
	btnLine->addWidget(backBtn);

	mainLayout->addLayout(userNameLine);
	mainLayout->addLayout(coinsLine);
	mainLayout->addLayout(rankLine);
	mainLayout->addLayout(EPLine);
	mainLayout->addLayout(passedLevelsLine);
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

//获取玩家信息函数
void PlayerWindow::getPlayerInfo()
{
	QFile file("playerData.json");
	if (file.exists() && file.open(QIODevice::ReadOnly))
	{
		QByteArray data = file.readAll();
		file.close();

		QJsonDocument doc = QJsonDocument::fromJson(data);
		if (!doc.isNull() && doc.isObject())
		{
			playerInfo.loadFromJson(doc.object());
		}
		else
		{
			playerInfo.setDefaults();
		}
	}
	else
	{
		playerInfo.setDefaults();
	}
}

//保存玩家信息函数
void PlayerWindow::savePlayerInfo()
{
	QFile file("playerData.json");
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
	//窗口设置
	QDialog newNameInput(this);
	newNameInput.setAttribute(Qt::WA_DeleteOnClose);
	newNameInput.setWindowTitle("更改姓名");
	newNameInput.setFixedSize(90, 70);

	//变量与控件
	QString name;
	QLineEdit* nameInput = new QLineEdit(&newNameInput);
	nameInput->setPlaceholderText("输入新姓名");
	QDialogButtonBox* btn = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &newNameInput);

	//链接信号槽
	QObject::connect(btn, &QDialogButtonBox::accepted, &newNameInput, &QDialog::accept);
	QObject::connect(btn, &QDialogButtonBox::rejected, &newNameInput, &QDialog::reject);

	//布局
	QVBoxLayout* mainLayout = new QVBoxLayout(&newNameInput);
	mainLayout->addWidget(nameInput);
	mainLayout->addWidget(btn);

	//监听事件并等待输入
	if (newNameInput.exec() == QDialog::Accepted)
	{
		name = nameInput->text().trimmed();
		if (name.isEmpty())
		{
			QMessageBox::warning(nullptr, "不合法的姓名", "姓名不能为空");
		}
		else
		{
			//若得到姓名且不为空，更新玩家信息并重新渲染UI
			userName->setText(name);
			playerInfo.toJson();
			savePlayerInfo();
		}
	}

}

//3个窗口跳转函数
void PlayerWindow::goBack()
{
	savePlayerInfo();
	this->hide();
	// 保存窗口设置
	settings->setValue("windows/width", width());
	settings->setValue("windows/height", height());
	settings->setValue("windows/position", pos());

	if (mainWin)
	{
		mainWin->show();
		mainWin->raise();
		mainWin->activateWindow();
	}
}

void PlayerWindow::goBag()
{
	this->hide();
	// 保存窗口设置
	settings->setValue("windows/width", width());
	settings->setValue("windows/height", height());
	settings->setValue("windows/position", pos());

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
	// 保存窗口设置
	settings->setValue("windows/width", width());
	settings->setValue("windows/height", height());
	settings->setValue("windows/position", pos());

	if (!shopWin)
	{
		shopWin = new ShopWindow(playerInfo, this, this);
		shopWin->setAttribute(Qt::WA_DeleteOnClose);
		connect(shopWin, &QObject::destroyed, this, [this]() {shopWin = nullptr; });
	}
	shopWin->show();
	shopWin->raise();
	shopWin->activateWindow();
}