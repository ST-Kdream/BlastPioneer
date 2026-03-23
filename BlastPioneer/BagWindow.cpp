#include "BagWindow.h"
#include "SettingsManager.h"
#include "PlayerWindow.h"
#include "MainWindow.h"
#include "GameWindow.h"

//构造函数
BagWindow::BagWindow(PlayerInfo& playerInfo, QWidget* parent, PlayerWindow* playerWin) :QWidget(parent), playerInfo(playerInfo), 
	playerWin(playerWin),gameWin(nullptr)
{
	setWindowFlags(Qt::Window);
	isGameRunning = false;

	//遍历父窗口链找到主窗口
	mainWin = qobject_cast<MainWindow*>(parent);
	if (!mainWin && playerWin)
	{
		mainWin = qobject_cast<MainWindow*>(playerWin->parent());
	}
	if (!mainWin)
	{
		QWidget* p = parentWidget();
		while (p)
		{
			mainWin = qobject_cast<MainWindow*>(p);
			if (mainWin) { break; }
			p = p->parentWidget();
		}
	}

	loadItemMap();
	setupUI();
	refreshItemList();
}

//UI设置（包括信号槽链接）
void BagWindow::setupUI()
{
	setWindowTitle("玩家背包");

	QByteArray savedGeo = SettingsManager::instance()->loadWindowGeometry();
	if (!savedGeo.isEmpty())
		restoreGeometry(savedGeo);
	else {
		resize(800, 600);
		move(100, 100);
	}

	title = new QLabel("我的背包", this);
	title->setStyleSheet("font-size: 18px; font-weight: bold; color: #333;");

	useBtn = new QPushButton("使用", this);
	backBtn = new QPushButton("返回", this);
	connect(useBtn, &QPushButton::clicked, this, &BagWindow::useItem);
	connect(backBtn, &QPushButton::clicked, this, &BagWindow::goBack);

	itemList = new QListWidget(this);

	useBtn->setObjectName("useBtn");
	backBtn->setObjectName("backBtn");

	//按钮统一样式表
	QString btnStyle = R"(
        QPushButton {
            font-size: 16px;
            padding: 8px 16px;
            border: none;
            border-radius: 6px;
            color: #000000;
        }
        #useBtn { background-color: #3399FF; }
        #useBtn:hover { background-color: #66B2FF; }
        #useBtn:pressed { background-color: #2673CC; }
        #backBtn { background-color: #E65C00; }
        #backBtn:hover { background-color: #FF7F00; }
        #backBtn:pressed { background-color: #B34700; }
    )";

	useBtn->setStyleSheet(btnStyle);
	backBtn->setStyleSheet(btnStyle);

	//布局
	QVBoxLayout* mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(20, 40, 20, 50);
	mainLayout->setSpacing(15);

	mainLayout->addWidget(title, 0, Qt::AlignHCenter);
	mainLayout->addWidget(itemList);

	QHBoxLayout* btnLine = new QHBoxLayout();
	btnLine->setSpacing(20);
	for (auto btn : { useBtn, backBtn }) {
		btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		btnLine->addWidget(btn);
	}
	mainLayout->addLayout(btnLine);

	setLayout(mainLayout);
}

//加载资源图标
void BagWindow::loadItemMap()
{
	QFile file(":/Data/itemInfo.json");
	if (!file.open(QIODevice::ReadOnly))
	{
		QMessageBox::critical(this, "错误", "加载道具资源失败");
		return;
	}
	QByteArray itemData = file.readAll();
	file.close();

	QJsonDocument doc = QJsonDocument::fromJson(itemData);
	if (!doc.isArray()) { return; }

	QJsonArray array = doc.array();
	for (const auto& val : array)
	{
		QJsonObject obj = val.toObject();
		QString name = obj["name"].toString();
		QString iconPath = obj["iconPath"].toString();
		if ((!name.isEmpty()) && (!iconPath.isEmpty()))
		{
			itemIconMap[name] = iconPath;
		}
	}
}

//更新道具列表
void BagWindow::refreshItemList()
{
	itemList->clear();
	QString appDir = QCoreApplication::applicationDirPath();
	QMap<QString, int> inventory = playerInfo.getInventory();

	for (auto it = inventory.begin(); it != inventory.end(); ++it)
	{
		QString displayText = QString("%1：%2").arg(it.key()).arg(it.value());
		QListWidgetItem* item = new QListWidgetItem(displayText, itemList);

		if (itemIconMap.contains(it.key()))
		{
			QString iconPath = itemIconMap[it.key()];
			QString fullPath = appDir + '/' + iconPath;
			QPixmap pixmap(fullPath);
			if (!pixmap.isNull())
			{
				pixmap = pixmap.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation);
				item->setIcon(QIcon(pixmap));
			}
		}
		//商店窗口已经报过错了，这里就不报了
	}
	if (inventory.empty())
	{
		itemList->addItem("背包空空如也~~");
	}
}

//使用道具
void BagWindow::useItem()
{
	QListWidgetItem* currentItem = itemList->currentItem();
	if (!currentItem)
	{
		QMessageBox::information(this, "提示", "请选择一个道具");
		return;
	}

	QString itemText = currentItem->text();
	QString itemName = itemText.section("：", 0, 0);

	if (!isGameRunning)
	{
		QMessageBox::information(this, "提示", "道具只能在游戏运行时才能使用（游戏时按B键打开）");
		return;
	}

	Item* item = mainWin->getItem(itemName);
	if (item->use(gameWin))
	{
		playerInfo.removeItem(itemName);
		refreshItemList();
		if (mainWin) mainWin->savePlayerData();
		QMessageBox::information(this, "提示", QString("使用道具%1成功").arg(itemName));
	}
}

//通过是否存在游戏窗口判断游戏是否正在运行
void BagWindow::setGameWindow(GameWindow* gameWin)
{
	this->gameWin = gameWin;
	isGameRunning = (gameWin != nullptr);
}

//展示窗口时刷新道具和窗口大小
void BagWindow::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);
	refreshItemList();
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
}

void BagWindow::goBack()
{
	// 保存窗口设置
	SettingsManager::instance()->saveWindowGeometry(saveGeometry());

	QWidget* target = nullptr;
	if (gameWin)
	{
		target = gameWin;
		emit backToGame();
	}
	else if (playerWin)
	{
		target = playerWin;
	}

	if (target)
	{
		target->show();
		target->raise();
		target->activateWindow();
	}

	hide();
}

//关闭窗口保存大小
void BagWindow::closeEvent(QCloseEvent* event)
{
	SettingsManager::instance()->saveWindowGeometry(saveGeometry());
	QWidget::closeEvent(event);
}