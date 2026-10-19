#pragma once
#include "ScreenBase.h"

class QLabel;
class QVBoxLayout;
class QHBoxLayout;
class QPushButton;

// ============================================================
// 页面骨架（美化体系 L2 层，见 docs/界面体系与美化接口.md 第 3 节）
//
// 目的：让十几个界面"看起来是同一个产品"。
// 以前每个界面各自 setContentsMargins、各自摆标题与按钮条，于是间距/层级/按钮位置各不相同 ——
// 这不是"不好看"的根因，但它是"看着不专业"的根因。
//
// 子类只做三件事：
//     MyScreen::MyScreen(QWidget* p) : ScaffoldScreen(p)
//     {
//         setPageTitle(QStringLiteral("我的背包"), QStringLiteral("游戏内按 B 打开"));
//         contentLayout()->addWidget(m_list);                    // 往内容区塞控件
//         addFooterButton(QStringLiteral("返回"), "accentBtn");  // 底部按钮
//     }
//
// 约定：这里**不写任何内联样式**，颜色/字号/间距全部由 objectName 交给 ui/*.qss
// （#pageRoot / #pageHeader / #pageTitle / #pageSubtitle / #pageContent / #pageFooter）。
// ============================================================
class ScaffoldScreen : public ScreenBase
{
	Q_OBJECT

public:
	explicit ScaffoldScreen(QWidget* parent = nullptr);

	//标题与副标题（副标题可留空；重复调用会覆盖）
	void setPageTitle(const QString& title, const QString& subtitle = QString());

	//内容区布局：子类往里加控件
	QVBoxLayout* contentLayout() const { return m_contentLayout; }

	//底部按钮：objectName 决定样式（primaryBtn 主色 / accentBtn 强调色 / ghostBtn 次要）
	//所有按钮都设为"横向可拉伸"，于是只有一个按钮时它会铺满，三个按钮时它们等分
	QPushButton* addFooterButton(const QString& text, const QString& objectName);
	//在按钮之前插一个弹性空隙（把按钮推到右侧用）
	void addFooterStretch();
	//底部放一个标准的「返回」按钮（内部就是 accentBtn + requestBack）
	QPushButton* addBackButton(const QString& text = QStringLiteral("返回"));

	//右上角操作区（可选）：需要放"重新加载""打开目录"这类小按钮时用
	QHBoxLayout* headerExtraLayout() const { return m_headerExtra; }

private:
	QVBoxLayout* m_pageLayout = nullptr;      //整页：header / content / footer
	QVBoxLayout* m_contentLayout = nullptr;   //内容区
	QHBoxLayout* m_footerLayout = nullptr;    //底部按钮条
	QHBoxLayout* m_headerExtra = nullptr;     //右上角
	QLabel* m_titleLabel = nullptr;
	QLabel* m_subtitleLabel = nullptr;
};
