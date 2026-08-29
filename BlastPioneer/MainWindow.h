#pragma once
#include "Item.h"
#include "PlayerInfo.h"
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>

class PlayerWindow;
class RulesWindow;
class LevelSelectWindow;
class BagWindow;
class ShopWindow;
class SettingsWindow;
class PlayerInfo;
class LobbyWindow;

class MainWindow :public QWidget
{
	Q_OBJECT

private:

	//主窗口持有相关数据
	PlayerInfo playerInfo;
	QList<Item> itemList;

	//5个按钮和一个标题
	QLabel* mainTitle;
	QPushButton* btn1;
	QPushButton* btn2;
	QPushButton* btn3;
	QPushButton* btn4;
	QPushButton* btn5;
	void setupUI();

	//其他窗口指针
	LevelSelectWindow* levelSelectWin;
	PlayerWindow* playerWin;
	RulesWindow* rulesWin;
	BagWindow* bagWin;
	ShopWindow* shopWin;
	SettingsWindow* settingsWin;
	LobbyWindow* lobbyWin;

	void showEvent(QShowEvent* event) override;

public:
	explicit MainWindow(const PlayerInfo& playerInfo, const QList<Item>& items, QWidget* parent = nullptr);
	void btnConnect();
	void createWindows(); //预创建常用窗口
	//返回预创建窗口
	BagWindow* getBagWin() const { return bagWin; }
	ShopWindow* getShopWin() const { return shopWin; }
	//查找道具
	Item* getItem(const QString& name);
	void savePlayerData();
	PlayerInfo& getPlayerInfo() { return playerInfo; }

private slots:
	//窗口跳转函数
	void GoPlayerWindow();
	void GoSingleGame();
	void GoInternetGame();
	void GoRulesWindow();
	void GoSettingsWindow();
};
