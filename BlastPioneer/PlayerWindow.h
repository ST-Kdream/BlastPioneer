#pragma once
#include <vector>
#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QDialog>
#include <QLineEdit>
#include <QFile>
#include <QDir>
#include <QByteArray>
#include <QShowEvent>
#include <QCloseEvent>
#include <QMessageBox>

class MainWindow;
class BagWindow;
class ShopWindow;
class PlayerInfo;

class PlayerWindow :public QWidget
{
	Q_OBJECT

private:

	//数据
	PlayerInfo& playerInfo;

	//控件
	QPushButton* backBtn;
	QPushButton* bagBtn;
	QPushButton* shopBtn;
	QPushButton* newNameBtn;

	//变量
	QLabel* userName;
	QLabel* coins;
	QLabel* rank;
	QLabel* EP;
	QLabel* passedLevels;

	//其他窗口指针
	MainWindow* mainWin;
	BagWindow* bagWin;
	ShopWindow* shopWin;

	//函数
	void savePlayerInfo();

public:
	explicit PlayerWindow(PlayerInfo& playerInfo, QWidget* parent = nullptr);
	void setupUI();
	void updateUI();
	void connectBtn();
	void setMainWin(MainWindow* mainWin);
	void showEvent(QShowEvent* event) override;
	void closeEvent(QCloseEvent* event) override;

private slots:
	void changeName();
	void goBack();
	void goBag();
	void goShopping();
};
