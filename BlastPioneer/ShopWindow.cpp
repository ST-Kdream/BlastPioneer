#include "ShopWindow.h"

//构造函数
ShopWindow::ShopWindow(PlayerInfo& playerInfo, QWidget* parent) :QWidget(parent), playerInfo(playerInfo)
{
	loadShopItems();
	setupUI();
}

//UI设置
void ShopWindow::setupUI()
{
	setWindowTitle("商店");

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

	//标题
	title = new QLabel("商店", this);
	title->setStyleSheet("font-size: 18px; font-weight: bold; color: #333;");
	//金币标签
	coinsLabel = new QLabel(QString("金币: %1").arg(playerInfo.getCoins()), this);
	coinsLabel->setStyleSheet("font-size: 16px; color: gold;");

	//商品列表
	shopList = new QListWidget(this);

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
	for (const Item& item : shopItems)
	{
		QString displayText= QString("%1 - %2 金币\n   %3")
									.arg(item.getName()).arg(item.getPrice()).arg(item.getDescription());
		shopList->addItem(displayText);
	}
}

//从json文件读取道具
void ShopWindow::loadShopItems()
{

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
	close();
}