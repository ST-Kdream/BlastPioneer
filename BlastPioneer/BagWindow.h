#pragma once
#include "PlayerInfo.h"
#include <QWidget>
#include <QMap>
#include <QListWidget>
#include <QPushButton>
#include <QJsonArray>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QCoreApplication>
#include <QIcon>
#include <QDir>

class MainWindow;
class PlayerWindow;
class GameWindow;
class PlayerInfo;

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
	void showEvent(QShowEvent* event) override;

public:
	explicit BagWindow(PlayerInfo& playerInfo, QWidget* parent = nullptr, PlayerWindow* playerWin = nullptr);
	void setGameWindow(GameWindow* gameWin);
	void setGameWin(GameWindow* gameWin) { this->gameWin = gameWin; }
	void setMainWin(MainWindow* mainWin) { this->mainWin = mainWin; }

private slots:
	void goBack();
	void useItem();

signals:
	void backToGame();
};