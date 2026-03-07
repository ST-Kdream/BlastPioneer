#pragma once
#include "PlayerInfo.h"
#include "SettingsManager.h"
#include "PlayerWindow.h"
#include "MainWindow.h"
#include "GameWindow.h"
#include <QWidget>
#include <QMap>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QCoreApplication>
#include <QIcon>
#include <QDir>

class BagWindow :public QWidget
{
	Q_OBJECT

private:
	MainWindow* mainWin;
	PlayerWindow* playerWin;
	PlayerInfo& playerInfo;
	GameWindow* gameWin;

	QLabel* title;
	QPushButton* backBtn;
	QPushButton* useBtn;
	QListWidget* itemList;
	QMap<QString, QString> itemIconMap;

	bool isGameRunning;

	void setupUI();
	void refreshItemList();
	void loadItemMap();
	void closeEvent(QCloseEvent* event) override;

public:
	explicit BagWindow(PlayerInfo& playerInfo, QWidget* parent = nullptr, PlayerWindow* playerWin);
	void setGameWindow(GameWindow* gameWin);

private slots:
	void goBack();
	void useItem();
};