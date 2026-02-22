#pragma once
#include "Item.h"
#include "PlayerInfo.h"
#include "settings.h"
#include <QWidget>
#include <QList>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QVBoxLayout>

class ShopWindow :public QWidget
{
	Q_OBJECT

private:
	PlayerInfo& playerInfo;
	QList<Item> shopItems;
	QListWidget* shopList;
	QLabel* coinsLabel;
	QLabel* title;
	QPushButton* buyBtn;
	QPushButton* backBtn;

	void loadShopItems();
	void refreshShopList();
	void updateCoins();
	void setupUI();

public:
	explicit ShopWindow(PlayerInfo& playerInfo, QWidget* parent = nullptr);

private slots:
	void buyItem();
	void goBack();
};
