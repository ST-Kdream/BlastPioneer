#include "SideTabWidget.h"

#include <QHBoxLayout>
#include <QListWidget>
#include <QStackedWidget>

SideTabWidget::SideTabWidget(QWidget* parent) : QWidget(parent)
{
	setObjectName("sideTabs");
	setAttribute(Qt::WA_StyledBackground, true);

	m_list = new QListWidget(this);
	m_list->setObjectName("sideTabsList");
	m_list->setFixedWidth(176);
	//不要横向滚动条：条目标题长了应该省略号，而不是出现一条横向滚动条
	m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	m_list->setFocusPolicy(Qt::NoFocus);   //导航不抢焦点，键盘留给界面本身的快捷键

	m_stack = new QStackedWidget(this);
	m_stack->setObjectName("sideTabsPane");
	m_stack->setAttribute(Qt::WA_StyledBackground, true);

	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(m_list);
	layout->addWidget(m_stack, 1);

	//选中哪一项就显示哪一页（单向同步：列表是唯一入口，避免两边互相触发）
	connect(m_list, &QListWidget::currentRowChanged, this, [this](int row)
	{
		if (row >= 0 && row < m_stack->count()) { m_stack->setCurrentIndex(row); }
	});
}

int SideTabWidget::addPage(QWidget* page, const QString& title)
{
	if (!page) { return -1; }

	m_stack->addWidget(page);

	QListWidgetItem* item = new QListWidgetItem(title, m_list);
	item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);

	//第一页自动选中（否则初始状态是"左空白右空白"）
	if (m_list->count() == 1) { m_list->setCurrentRow(0); }

	return m_stack->count() - 1;
}

void SideTabWidget::setCurrentIndex(int index)
{
	if (index < 0 || index >= m_stack->count()) { return; }
	m_list->setCurrentRow(index);
}

int SideTabWidget::currentIndex() const
{
	return m_stack->currentIndex();
}

int SideTabWidget::count() const
{
	return m_stack->count();
}

void SideTabWidget::clearPages()
{
	m_list->clear();
	while (m_stack->count() > 0)
	{
		QWidget* page = m_stack->widget(0);
		m_stack->removeWidget(page);
		page->deleteLater();
	}
}
