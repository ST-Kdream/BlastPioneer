#pragma once
#include "PlayerInfo.h"
#include "settings.h"
#include "PlayerWindow.h"
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
	PlayerWindow* playerWin;
	PlayerInfo& playerInfo;
	QLabel* title;
	QPushButton* backBtn;
	QPushButton* useBtn;
	QListWidget* itemList;
	QMap<QString, QString> itemIconMap;

	void setupUI();
	void refreshItemList();
	void loadItemMap();

public:
	explicit BagWindow(PlayerInfo& playerInfo, QWidget* parent = nullptr, PlayerWindow* playerWin);

private slots:
	void goBack();
	void useItem();
};