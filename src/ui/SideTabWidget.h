#pragma once
#include <QWidget>

class QListWidget;
class QStackedWidget;

// ============================================================
// 左侧导航 + 右侧内容（替代 QTabWidget::West）
//
// 为什么不用 QTabWidget::West：三个都是实测出来的毛病 ——
//   1. 它把选项卡文字**竖排**（中文竖着读很难受）；
//   2. 页数一多就出现**上下两个箭头**（滚动按钮），既不好看也不好点；
//   3. 滚轮悬停在选项卡上会**直接切换页**，而不是滚动列表 —— 在规则/设置这种
//      长列表上，用户想滚动却把页切走了。
//
// 这里改成 QListWidget（左）+ QStackedWidget（右）的组合：
//   - 文字自然横排；滚轮 = 滚动列表（符合直觉）；列表自己会滚，不需要滚动箭头；
//   - 外观完全交给 QSS（#sideTabsList / #sideTabsPane），不需要自绘 QTabBar。
// ============================================================
class SideTabWidget : public QWidget
{
	Q_OBJECT

public:
	explicit SideTabWidget(QWidget* parent = nullptr);

	//加一页（page 的所有权交给本控件），返回索引
	int addPage(QWidget* page, const QString& title);

	void setCurrentIndex(int index);
	int currentIndex() const;
	int count() const;

	//清空所有页（QListWidget 的条目 + QStackedWidget 的页面一起删）
	void clearPages();

private:
	QListWidget* m_list = nullptr;
	QStackedWidget* m_stack = nullptr;
};
