#pragma once
#include "RulesWindow.h"
#include "PlayerWindow.h"
#include "LevelSelectWindow.h"
#include "SettingsManager.h"
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>

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
	RulesWindow* rulesWin;
	PlayerWindow* playerWin;
	LevelSelectWindow* levelSelectWin;

public:
	explicit MainWindow(QWidget* parent = nullptr);
	MainWindow(const PlayerInfo& playerInfo, const QList<Item>& items, QWidget* parent = nullptr);
	void btnConnect();

private slots:
	//窗口跳转函数
	void GoPlayerWindow();
	void GoSingleGame();
	void GoInternetGame();
	void GoRulesWindow();
	void GoSettingsWindow();
};
