#pragma once
#include "ScaffoldScreen.h"

#include <QList>

class QPushButton;
class QGridLayout;

//关卡选择界面：原 LevelSelectWindow
//选中关卡后不再自己创建游戏窗口，而是把窗口栈栈顶替换为游戏界面
//
// 关卡按钮不写死：从 Data/levels 里读（LevelDatabase）。
// 往那个目录丢一个 level_08.json，再进这个界面就多一个按钮 —— 不用重新编译。
// 一个关卡文件都读不到时退回 6 关程序化关卡，所以缺数据也不会开不了界面。
class LevelSelectScreen : public ScaffoldScreen
{
	Q_OBJECT

public:
	explicit LevelSelectScreen(QWidget* parent = nullptr);

	QString screenId() const override { return "LevelSelect"; }
	QString displayTitle() const override { return "难度选择"; }
	bool persistent() const override { return true; }

	void onEnter(const QVariantMap& args) override;

private:
	void setupUI();
	//按 LevelDatabase 的内容重建关卡按钮（每次进入都重建，改完 JSON 不用重启）
	void rebuildLevelButtons();
	//按存档进度设置"哪几关是解锁的"
	void applyUnlockState();

	//读不到关卡数据时的兜底关卡数
	static const int kFallbackLevelCount = 6;
	//每行几个按钮
	static const int kColumns = 3;

	QGridLayout* levelGrid = nullptr;

	//按钮与它们对应的关卡号（关卡号来自数据，不一定是 1,2,3……）
	QList<QPushButton*> levelBtns;
	QList<int> levelIds;

private slots:
	void goGameWindow(int level);
};
