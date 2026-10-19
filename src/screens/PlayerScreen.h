#pragma once
#include "ScaffoldScreen.h"

class QLabel;
class QPushButton;

//玩家信息界面：原 PlayerWindow
//不持有其它窗口指针，数据统一来自 PlayerProfile 单例；
//外观走页面骨架 + objectName（#statKey / #statValue + 三种按钮角色），不再写内联样式
class PlayerScreen : public ScaffoldScreen
{
	Q_OBJECT

public:
	explicit PlayerScreen(QWidget* parent = nullptr);

	QString screenId() const override { return "Player"; }
	QString displayTitle() const override { return "玩家信息"; }
	bool persistent() const override { return true; }

	void onEnter(const QVariantMap& args) override;

private:
	void setupUI();
	void connectBtn();
	//刷新界面数据
	void updateUI();

	//控件
	QPushButton* newNameBtn = nullptr;

	QLabel* userName = nullptr;
	QLabel* coins = nullptr;
	QLabel* rank = nullptr;
	QLabel* EP = nullptr;
	QLabel* passedLevels = nullptr;

private slots:
	void changeName();
	void goBag();
	void goShopping();
};
