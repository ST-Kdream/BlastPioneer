#include <PlayerWindow.h>

//构造函数
PlayerWindow::PlayerWindow(MainWindow* mainWin, QWidget* parent): MainWindow(parent), mainWin(mainWin)
{
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
	QPoint savedPos = settings->value("RulesWindow/position", defaultPos).toPoint();
	move(savedPos);

	//3个按钮
	backBtn = new QPushButton("返回", this);
	bagBtn = new QPushButton("背包", this);
	shopBtn = new QPushButton("商店", this);

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

	//3个按钮设置样式
	backBtn->setObjectName("backBtn");
	bagBtn->setObjectName("bagBtn");
	shopBtn->setObjectName("shopBtn");

	QString mainStyle = R"(
								QPushButton { font-size: 16px; color: #000000;padding: 8px 16px;border: none;
											  border-radius: 6px; text-shadow: 1px 1px 0 #ffffff33; }
								#btnBack { background-color: #E65C00; }
								#btnBack:hover { background-color: #FF7F00; }
								#btnBack:pressed { background-color: #B34700; }
								#btnBag { background-color: #FFCC00; }
								#btnBag:hover { background-color: #FFF000; }
								#btnBag:pressed { background-color: #CC9900; }
								#btnShop { background-color: #3399FF; }
								#btnShop:hover { background-color: #66B2FF; }
								#btnShop:pressed { background-color: #2673CC; }
						)";

	backBtn->setStyleSheet(mainStyle);
	bagBtn->setStyleSheet(mainStyle);
	shopBtn->setStyleSheet(mainStyle);

	//布局
	QVBoxLayout* mainLayout = new QVBoxLayout(this);

}

//链接信号槽函数
void PlayerWindow::connectBtn()
{
	connect(backBtn, &QPushButton::clicked, this, &PlayerWindow::goBack);
	connect(bagBtn, &QPushButton::clicked, this, &PlayerWindow::goBag);
	connect(shopBtn, &QPushButton::clicked, this, &PlayerWindow::goShopping);
}

//获取玩家信息函数
void PlayerWindow::getPlayerInfo()
{
	QString filePath = "playerData.json";
	QFile file(filePath);

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

//展示窗口时更新数据
void PlayerWindow::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);
	updateUI();
}

void PlayerWindow::updateUI()
{
	userName->setText(playerInfo.getUserName());
	coins->setText(QString::number(playerInfo.getCoins()));
	rank->setText(playerInfo.getRank());
	EP->setText(QString::number(playerInfo.getEP()));
	passedLevels->setText(QString::number(playerInfo.getPassedLevels()));
}

//3个窗口跳转函数
void PlayerWindow::goBack()
{
	this->hide();
	if (mainWin)
	{
		mainWin->show();
		mainWin->raise();
		mainWin->activateWindow();
	}
}

void PlayerWindow::goBag()
{

}

void PlayerWindow::goShopping()
{

}