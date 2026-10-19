#pragma once
#include "ScreenBase.h"
#include <QMap>
#include <QString>

class QLabel;
class QPushButton;
class QListWidget;
class SingleGameScreen;

//背包界面：原 BagWindow
//两种用途：
//  1. Mode::Normal  —— 从玩家信息界面进入，走窗口栈，只能看
//  2. Mode::Ingame  —— 游戏中按 B 打开的覆盖层，可以使用道具
//覆盖层模式不走窗口栈（它是游戏界面内部的一层），因此它不参与返回栈，
//也**不会收到 onEnter** —— 列表刷新必须由游戏界面显式调 refresh()。
class BagScreen : public ScreenBase
{
	Q_OBJECT

public:
	enum class Mode { Normal, Ingame };

	explicit BagScreen(QWidget* parent = nullptr);

	QString screenId() const override { return "Bag"; }
	QString displayTitle() const override { return "玩家背包"; }
	bool persistent() const override { return true; }

	//设置为游戏内覆盖层模式（注入游戏界面，内部会取到它的引擎）
	void setGameScreen(SingleGameScreen* gameScreen);

	void onEnter(const QVariantMap& args) override;

	//刷新标题与道具列表
	//为什么要单独暴露：覆盖层不走窗口栈、不会收到 onEnter，
	//必须由游戏界面在 show() 之后显式调一次（漏了它的表现就是"背包永远是空的"）
	void refresh();

private:
	void setupUI();
	//重建列表项（只被 refresh() 调用）
	void refreshItemList();

	//控件（成员名不要叫 title，QWidget 已通过 moc 属性占用了这个名字）
	QLabel* titleLabel = nullptr;
	QPushButton* backBtn = nullptr;
	QPushButton* useBtn = nullptr;
	QListWidget* itemList = nullptr;

	//游戏内模式：由游戏界面注入
	SingleGameScreen* gameScreen = nullptr;
	bool isGameRunning = false;

private slots:
	void goBack();
	void useItem();
};
