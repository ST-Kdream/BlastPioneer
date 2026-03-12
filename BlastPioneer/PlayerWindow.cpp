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
	qDebug() << "PlayerWindow constructor start";
	try {
		setupUI();
		connectBtn();
		qDebug() << "PlayerWindow constructor end";
	}
	catch (const std::exception& e) {
		qDebug() << "PlayerWindow constructor exception:" << e.what();
		throw;
	}
	catch (...) {
		qDebug() << "PlayerWindow constructor unknown exception";
		throw;
	}
	setWindowFlags(Qt::Window); // 确保是顶级窗口
	setAttribute(Qt::WA_ShowWithoutActivating, false); // 允许激活
}

//UI设置
void PlayerWindow::setupUI()
{
	setWindowTitle("玩家信息");

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
											  border-radius: 6px; }
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
	QVBoxLayout* mainLayout = new QVBoxLayout(this);

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
	qDebug() << "PlayerWindow showEvent start";
	QWidget::showEvent(event);
	try {
		updateUI();
		qDebug() << "PlayerWindow updateUI done";
	}
	catch (const std::exception& e) {
		qDebug() << "PlayerWindow showEvent exception:" << e.what();
	}
	catch (...) {
		qDebug() << "PlayerWindow showEvent unknown exception";
	}
	qDebug() << "PlayerWindow showEvent end";
}

//关闭窗口时保存玩家信息
void PlayerWindow::closeEvent(QCloseEvent* event)
{
	qDebug() << "closeEvent: entered";
	SettingsManager::instance()->saveWindowGeometry(saveGeometry());
	savePlayerInfo();
	QWidget::closeEvent(event);
}

//UI更新函数
void PlayerWindow::updateUI()
{
	qDebug() << "PlayerWindow updateUI: userName =" << playerInfo.getUserName();
	userName->setText(playerInfo.getUserName());
	qDebug() << "PlayerWindow updateUI: coins =" << playerInfo.getCoins();
	coins->setText(QString::number(playerInfo.getCoins()));
	qDebug() << "PlayerWindow updateUI: rank =" << playerInfo.getRank();
	rank->setText(playerInfo.getRank());
	qDebug() << "PlayerWindow updateUI: EP =" << playerInfo.getEP();
	EP->setText(QString::number(playerInfo.getEP()));
	qDebug() << "PlayerWindow updateUI: passedLevels =" << playerInfo.getPassedLevels();
	passedLevels->setText(QString::number(playerInfo.getPassedLevels()));
}


//更改玩家姓名函数
void PlayerWindow::changeName()
{
	qDebug() << "changeName start";
	try {
		QDialog newNameInput(this);
		newNameInput.setWindowTitle("更改姓名");
		newNameInput.setFixedSize(300, 150);  // 增大尺寸，避免窗口管理器问题

		QVBoxLayout* mainLayout = new QVBoxLayout(&newNameInput);
		QLineEdit* nameInput = new QLineEdit(&newNameInput);
		nameInput->setPlaceholderText("输入新姓名");
		QDialogButtonBox* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &newNameInput);

		connect(btnBox, &QDialogButtonBox::accepted, &newNameInput, &QDialog::accept);
		connect(btnBox, &QDialogButtonBox::rejected, &newNameInput, &QDialog::reject);

		mainLayout->addWidget(nameInput);
		mainLayout->addWidget(btnBox);
		newNameInput.setLayout(mainLayout);

		if (newNameInput.exec() == QDialog::Accepted) {
			QString newName = nameInput->text().trimmed();
			if (newName.isEmpty()) {
				QMessageBox::warning(this, "不合法的姓名", "姓名不能为空");
			}
			else {
				playerInfo.setUserName(newName);  // 直接修改，无需 toJson()
				userName->setText(newName);
				savePlayerInfo();
			}
		}
	}
	catch (const std::exception& e) {
		qDebug() << "changeName exception:" << e.what();
		QMessageBox::critical(this, "错误", QString("更改姓名时发生异常: %1").arg(e.what()));
	}
	catch (...) {
		qDebug() << "changeName unknown exception";
		QMessageBox::critical(this, "错误", "更改姓名时发生未知异常");
	}
	qDebug() << "changeName end";
}


//3个窗口跳转函数
void PlayerWindow::goBack()
{
	qDebug() << "goBack: start";

	// 保存数据
	savePlayerInfo();
	SettingsManager::instance()->saveWindowGeometry(saveGeometry());

	// 如果 mainWin 存在，立即显示并激活
	if (mainWin) {
		qDebug() << "goBack: showing mainWin";
		mainWin->show();
		mainWin->raise();
		mainWin->activateWindow();
		QApplication::processEvents(); // 确保窗口立即显示
	}
	else {
		qDebug() << "goBack: mainWin is null, cannot return";
		// 如果 mainWin 为空，尝试通过父窗口链查找（备选方案）
	}

	// 最后关闭自己
	qDebug() << "goBack: closing self";
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

	if (!shopWin) {
		shopWin = new ShopWindow(playerInfo, nullptr, this); // 独立窗口
		shopWin->setAttribute(Qt::WA_DeleteOnClose);
		shopWin->setMainWin(mainWin); // 传入主窗口指针
		connect(shopWin, &QObject::destroyed, this, [this]() { shopWin = nullptr; });
	}
	shopWin->show();
	shopWin->raise();
	shopWin->activateWindow();
}
