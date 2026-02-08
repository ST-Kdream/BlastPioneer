#pragma once
#include "MainWindow.h"
#include "PlayerInfo.h"
#include <vector>
#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QDialog>
#include <QLineEdit>
#include <QFile>
#include <QByteArray>
#include <QShowEvent>
#include <QMessageBox>

class PlayerWindow :public MainWindow
{
	Q_OBJECT

private:
	//控件
	MainWindow* mainWin;
	QPushButton* backBtn;
	QPushButton* bagBtn;
	QPushButton* shopBtn;
	QPushButton* newNameBtn;

	//变量
	PlayerInfo playerInfo;
	QLabel* userName;
	QLabel* coins;
	QLabel* rank;
	QLabel* EP;
	QLabel* passedLevels;

public:
	explicit PlayerWindow(MainWindow* mainWin, QWidget* parent = nullptr);
	void setupUI();
	void updateUI();
	void connectBtn();
	void getPlayerInfo();
	void showEvent(QShowEvent* event) override;

private slots:
	void changeName();
	void goBack();
	void goBag();
	void goShopping();
};
