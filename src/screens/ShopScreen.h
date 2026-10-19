#pragma once
#include "ScreenBase.h"
#include "Item.h"      //QList<Item> 是值成员，QList 需要完整类型（否则 qarraydataops.h 的 static_assert 会失败）
#include <QList>

class QLabel;
class QPushButton;
class QListWidget;

//商店界面：原 ShopWindow
//道具列表改由 ItemDatabase 提供，不再自己解析 itemInfo.json
class ShopScreen : public ScreenBase
{
	Q_OBJECT

public:
	explicit ShopScreen(QWidget* parent = nullptr);

	QString screenId() const override { return "Shop"; }
	QString displayTitle() const override { return "商店"; }
	bool persistent() const override { return true; }

	void onEnter(const QVariantMap& args) override;

private:
	void setupUI();
	//刷新商品列表
	void refreshShopList();
	//刷新金币显示
	void updateCoins();

	QList<Item> shopItems;
	QListWidget* shopList = nullptr;
	QLabel* coinsLabel = nullptr;
	//注意：成员名不要叫 title，QWidget 已通过 moc 属性占用了这个名字
	QLabel* titleLabel = nullptr;
	QPushButton* buyBtn = nullptr;
	QPushButton* backBtn = nullptr;

private slots:
	void buyItem();
	void goBack();
};
