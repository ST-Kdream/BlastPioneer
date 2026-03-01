#include "BagWindow.h"

//构造函数
BagWindow::BagWindow(PlayerInfo& playerInfo, QWidget* parent, PlayerWindow* playerWin) :QWidget(parent), playerInfo(playerInfo), playerWin(playerWin)
{
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
	{
		restoreGeometry(savedGeo);
	}
	else 
	{
		resize(800, 600);
		move(100, 100);
	}
	//标题
	title = new QLabel("我的背包", this);
	title->setStyleSheet("font-size: 18px; font-weight: bold; color: #333;");

	//使用按钮、返回按钮(链接信号槽)
	useBtn = new QPushButton("使用", this);
	backBtn = new QPushButton("返回", this);
	connect(useBtn, &QPushButton::clicked, this, &BagWindow::useItem);
	connect(backBtn, &QPushButton::clicked, this, &BagWindow::goBack);

	//商品列表
	itemList = new QListWidget(this);

	//布局
	QVBoxLayout* mainLayout = new QVBoxLayout();
	mainLayout->addWidget(title, Qt::AlignLeft);
	mainLayout->addWidget(itemList);

	QHBoxLayout* btnLine = new QHBoxLayout();
	btnLine->addWidget(useBtn);
	btnLine->addWidget(backBtn);
	mainLayout->addLayout(btnLine);
	setLayout(mainLayout);
}

//加载资源图标
void BagWindow::loadItemMap()
{
	QFile file(":/itemInfo.json");
	if (!file.open(QIODevice::ReadOnly))
	{
		QMessageBox::critical(this, "错误", "加载道具资源失败");
		return;
	}
	QByteArray itemData = file.readAll();
	file.close();

	QJsonDocument doc = QJsonDocument::fromJson(itemData);
	if (!doc.isArray()) { return; }

	for (auto val : doc.array())
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
	playerInfo.removeItem(itemName);
	QMessageBox::information(this, "提示", QString("道具：%1 使用成功").arg(itemName));
	refreshItemList();
}

//返回
void BagWindow::goBack()
{
	this->hide();
	// 保存窗口设置
	SettingsManager::instance()->saveWindowGeometry(saveGeometry());

	if (playerWin)
	{
		playerWin->show();
		playerWin->raise();
		playerWin->activateWindow();
	}
}

//关闭窗口保存大小
void BagWindow::closeEvent(QCloseEvent* event)
{
	SettingsManager::instance()->saveWindowGeometry(saveGeometry());
	QWidget::closeEvent(event);
}