#pragma once
#include "ScaffoldScreen.h"

class SideTabWidget;

//游戏规则界面：左侧选节 + 右侧内容
//
// 左侧的节**不是写死的**：按 Data/gamerules.txt 里的【小节】标题自动生成。
// 于是"加一节规则" = 往文本里加一段，不用改 C++（和关卡、道具、模组是同一个思路）。
// 文件里没有【】标题时退化成单页，不会因为文件格式变化就打不开。
class RulesScreen : public ScaffoldScreen
{
	Q_OBJECT

public:
	explicit RulesScreen(QWidget* parent = nullptr);

	QString screenId() const override { return "Rules"; }
	QString displayTitle() const override { return "游戏规则"; }
	bool persistent() const override { return true; }

	void onEnter(const QVariantMap& args) override;

private:
	void setupUI();
	//读取规则文件并按【小节】切分
	void rebuildTabs();

	SideTabWidget* m_tabs = nullptr;
};
