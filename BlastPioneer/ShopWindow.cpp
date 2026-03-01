#include "ShopWindow.h"

//构造函数
ShopWindow::ShopWindow(PlayerInfo& playerInfo, QWidget* parent, PlayerWindow* playerWin) :QWidget(parent), playerInfo(playerInfo), playerWin(playerWin)
{
	loadShopItems();
	refreshShopList();
	setupUI();
}

//UI设置
void ShopWindow::setupUI()
{
	setWindowTitle("商店");

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
	title = new QLabel("商店", this);
	title->setStyleSheet("font-size: 18px; font-weight: bold; color: #333;");
	//金币标签
	coinsLabel = new QLabel(QString("金币: %1").arg(playerInfo.getCoins()), this);
	coinsLabel->setStyleSheet("font-size: 16px; color: gold;");

	//商品列表
	shopList = new QListWidget();

	//按钮（包括信号槽链接）
	buyBtn = new QPushButton("购买", this);
	backBtn = new QPushButton("返回", this);
	connect(buyBtn, &QPushButton::clicked, this, &ShopWindow::buyItem);
	connect(backBtn, &QPushButton::clicked, this, &ShopWindow::goBack);

	//布局
	QVBoxLayout* mainLayout = new QVBoxLayout();
	mainLayout -> addWidget(coinsLabel);
	mainLayout->addWidget(shopList);

	QHBoxLayout* btnLine = new QHBoxLayout();
	btnLine->addWidget(buyBtn);
	btnLine->addWidget(backBtn);
	mainLayout->addLayout(btnLine);
	setLayout(mainLayout);
}

//更新道具列表
void ShopWindow::refreshShopList()
{
	shopList->clear();
	//获取可执行路径，防止获取资源失败
	QString appDir = QCoreApplication::applicationDirPath();

	for (const Item& item : shopItems)
	{
		QString displayText= QString("%1 - %2 金币\n   %3")
									.arg(item.getName()).arg(item.getPrice()).arg(item.getDescription());

		QListWidgetItem* itemList = new QListWidgetItem(displayText, shopList);

		//加载图片资源
		QString iconPath = item.getIconPath();
		if (!iconPath.isEmpty())
		{
			QString fullPath = appDir + '/' + iconPath;
			QPixmap pixmap(fullPath);
			if (!pixmap.isNull())
			{
				pixmap = pixmap.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation);
				itemList->setIcon(QIcon(pixmap));
			}
			else
			{
				QMessageBox::critical(this, "错误", "无法找到图片资源");
			}
		}
		else
		{
			QMessageBox::critical(this, "错误", "资源路径加载错误");
		}
	}
}

//从json文件读取道具
void ShopWindow::loadShopItems()
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

	QJsonArray itemArray = doc.array();
	for (const QJsonValue& val : itemArray)
	{
		QJsonObject obj = val.toObject();
		QString name = obj["name"].toString();
		QString description = obj["description"].toString();
		int price = obj["price"].toInt();
		QString iconPath = obj["iconPath"].toString();

		if (name.isEmpty()) { continue; }
		shopItems.append(Item(name, description, price, iconPath));
	}
}

//购买商品
void ShopWindow::buyItem()
{
	int currentRow = shopList->currentRow();
	if (currentRow < 0 || currentRow >= shopItems.size())
	{
		QMessageBox::information(this, "提示", "请先选择要购买的商品");
		return;
	}

	const Item& selectedItem = shopItems.at(currentRow);
	int price = selectedItem.getPrice();
	int coins = playerInfo.getCoins();

	if (coins < price)
	{
		QMessageBox::warning(this, "购买失败", "金币不足！");
		return;
	}

	playerInfo.setCoins(coins - price);
	playerInfo.addItem(selectedItem.getName());
	updateCoins();
	QMessageBox::information(this, "购买成功", QString("你购买了 %1").arg(selectedItem.getName()));
}

//更新金币标签
void ShopWindow::updateCoins()
{
	coinsLabel->setText(QString("金币: %1").arg(playerInfo.getCoins()));
}

//返回
void ShopWindow::goBack()
{
	this->hide();
	if (playerWin)
	{
		playerWin->show();
		playerWin->raise();
		playerWin->activateWindow();
	}
	// 保存窗口设置
	SettingsManager::instance()->saveWindowGeometry(saveGeometry());
}