#include "ShopScreen.h"
#include "Item.h"
#include "ItemDatabase.h"
#include "PlayerProfile.h"
#include "FigManager.h"
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QIcon>
#include <QSize>

ShopScreen::ShopScreen(QWidget* parent) : ScreenBase(parent)
{
	setupUI();
}

//UI设置
void ShopScreen::setupUI()
{
	setWindowTitle("商店");

	//创建控件
	titleLabel = new QLabel("商店", this);
	titleLabel->setObjectName("pageTitle");
	titleLabel->setAlignment(Qt::AlignCenter);
	coinsLabel = new QLabel(QString("金币: %1").arg(PlayerProfile::instance().info().getCoins()), this);
	coinsLabel->setObjectName("coinLabel");
	coinsLabel->setAlignment(Qt::AlignCenter);

	shopList = new QListWidget(this);

	buyBtn = new QPushButton("购买", this);
	backBtn = new QPushButton("返回", this);
	connect(buyBtn, &QPushButton::clicked, this, &ShopScreen::buyItem);
	connect(backBtn, &QPushButton::clicked, this, &ShopScreen::goBack);

	//样式走 objectName（主色 / 强调色），不在这里写颜色
	buyBtn->setObjectName("primaryBtn");
	backBtn->setObjectName("accentBtn");

	//布局
	QVBoxLayout* mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(20, 40, 20, 50);
	mainLayout->setSpacing(15);

	mainLayout->addWidget(titleLabel, 0, Qt::AlignHCenter);
	mainLayout->addWidget(coinsLabel, 0, Qt::AlignHCenter);
	mainLayout->addWidget(shopList);

	QHBoxLayout* btnLine = new QHBoxLayout();
	btnLine->setSpacing(20);
	for (auto btn : { buyBtn, backBtn }) {
		btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		btnLine->addWidget(btn);
	}
	mainLayout->addLayout(btnLine);

	setLayout(mainLayout);
}

//进入界面时刷新
void ShopScreen::onEnter(const QVariantMap& args)
{
	Q_UNUSED(args);

	//道具数据来自唯一的数据源
	shopItems = ItemDatabase::instance().all();

	refreshShopList();
	updateCoins();
}

//更新道具列表
void ShopScreen::refreshShopList()
{
	shopList->clear();

	for (const Item& item : shopItems)
	{
		QString displayText = QString("%1 - %2 金币\n   %3")
			.arg(item.getName()).arg(item.getPrice()).arg(item.getDescription());

		QListWidgetItem* itemList = new QListWidgetItem(displayText, shopList);

		//图标走 FigManager 缓存（启动时已预热），不再每个道具读一次盘
		Fig fig;
		if (FigCatalog::itemFig(item.getName(), fig))
		{
			itemList->setIcon(QIcon(FigManager::instance().scaled(fig, QSize(32, 32))));
		}
	}
}

//购买商品
void ShopScreen::buyItem()
{
	int currentRow = shopList->currentRow();
	if (currentRow < 0 || currentRow >= shopItems.size())
	{
		QMessageBox::information(this, "提示", "请先选择要购买的商品");
		return;
	}

	const Item& selectedItem = shopItems.at(currentRow);
	int price = selectedItem.getPrice();

	PlayerProfile& profile = PlayerProfile::instance();
	int coins = profile.info().getCoins();

	if (coins < price)
	{
		QMessageBox::warning(this, "购买失败", "金币不足！");
		return;
	}

	profile.info().setCoins(coins - price);
	profile.info().addItem(selectedItem.getName());
	profile.save();

	updateCoins();
	QMessageBox::information(this, "购买成功", QString("你购买了 %1").arg(selectedItem.getName()));
}

//更新金币标签
void ShopScreen::updateCoins()
{
	coinsLabel->setText(QString("金币: %1").arg(PlayerProfile::instance().info().getCoins()));
}

//返回
void ShopScreen::goBack()
{
	emit requestBack();
}
