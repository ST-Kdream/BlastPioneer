#pragma once
#include "ScaffoldScreen.h"

class QPushButton;

//主页：整个程序的入口页（窗口栈最底部）
//
// 这一轮的结构变化：
//   原来主页是一排"单人游戏 / 多人游戏 / 规则 / 设置 / 玩家信息"按钮；
//   现在主页只留一个醒目的「开始游戏」，点进去才是**模式选择**（单人 / 联机 / 以后的扩展模式）。
//   理由：模式会越来越多（包括 mod 新增的），把模式列表放在主页会越来越乱；
//   同时「模组」从设置里搬出来，在这里给它一个显眼入口。
class HomeScreen : public ScaffoldScreen
{
	Q_OBJECT

public:
	explicit HomeScreen(QWidget* parent = nullptr);

	QString screenId() const override { return "Home"; }
	QString displayTitle() const override { return "首页"; }
	bool persistent() const override { return true; }
	bool canGoBack() const override { return false; }   //窗口栈最底部

	void onEnter(const QVariantMap& args) override;

private:
	void setupUI();
	//把"谁在玩 / 段位 / 金币 / 经验 / 进度 / 可用关卡 / 已启用模组"这批**实时数据**刷一遍
	//为什么必须单独刷新：界面是启动时**预创建**的，构造函数跑在数据加载之前
	//（存档/关卡/模组都在启动管线的工作线程里读），那时读到的全是初始值 ——
	//于是主页会显示"金币 0、已通关 0 关"，看起来像写死的假数据
	void refreshLiveInfo();
	//加一张入口卡片（标题 + 说明；样式来自 #entryCard）
	QPushButton* addEntryCard(class QGridLayout* grid, int row, int column,
							  const QString& title, const QString& hint, const QString& cardId);

	QPushButton* m_startBtn = nullptr;
	class QLabel* m_who = nullptr;       //右上角：谁在玩 + 版本
	class QLabel* m_stats = nullptr;     //主卡片：段位/金币/经验/进度
	QPushButton* m_modsCard = nullptr;   //模组入口（说明里带"已启用 N 个"）
};
