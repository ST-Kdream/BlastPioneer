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

	void setupUI();
	void refreshItemList();

public:
	explicit BagWindow(PlayerInfo& playerInfo, QWidget* parent = nullptr);

private slots:
	void goBack();
	void useItem();
};