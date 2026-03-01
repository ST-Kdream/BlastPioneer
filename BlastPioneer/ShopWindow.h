#pragma once
#include "Item.h"
#include "PlayerInfo.h"
#include "SettingsManager.h"
#include "PlayerWindow.h"
#include <QWidget>
#include <QList>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QCoreApplication>
#include <QIcon>
#include <QDir>
#include <QJsonArray>

class ShopWindow :public QWidget
{
	Q_OBJECT

private:
	PlayerWindow* playerWin;
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
	explicit ShopWindow(PlayerInfo& playerInfo, QWidget* parent = nullptr, PlayerWindow* playerWin);;

private slots:
	void buyItem();
	void goBack();
};
