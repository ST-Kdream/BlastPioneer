#include "ScaffoldScreen.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>

ScaffoldScreen::ScaffoldScreen(QWidget* parent) : ScreenBase(parent)
{
	//根容器也要画背景色：自定义 QWidget 子类必须开这个属性，
	//QSS 里的 background-color 才会真的被绘制（这个坑在 HUD 上踩过一次）
	setObjectName("pageRoot");
	setAttribute(Qt::WA_StyledBackground, true);

	m_pageLayout = new QVBoxLayout(this);
	m_pageLayout->setContentsMargins(0, 0, 0, 0);
	m_pageLayout->setSpacing(0);

	// ---------- 顶部标题栏 ----------
	QWidget* header = new QWidget(this);
	header->setObjectName("pageHeader");
	header->setAttribute(Qt::WA_StyledBackground, true);

	QHBoxLayout* headerLayout = new QHBoxLayout(header);
	headerLayout->setContentsMargins(20, 12, 20, 12);
	headerLayout->setSpacing(12);

	QVBoxLayout* titleBox = new QVBoxLayout();
	titleBox->setContentsMargins(0, 0, 0, 0);
	titleBox->setSpacing(2);

	m_titleLabel = new QLabel(QString(), header);
	m_titleLabel->setObjectName("pageTitle");
	m_subtitleLabel = new QLabel(QString(), header);
	m_subtitleLabel->setObjectName("pageSubtitle");

	titleBox->addWidget(m_titleLabel);
	titleBox->addWidget(m_subtitleLabel);

	headerLayout->addLayout(titleBox);
	headerLayout->addStretch(1);

	m_headerExtra = new QHBoxLayout();
	m_headerExtra->setSpacing(8);
	headerLayout->addLayout(m_headerExtra);

	header->setLayout(headerLayout);
	m_pageLayout->addWidget(header);

	// ---------- 内容区 ----------
	QWidget* content = new QWidget(this);
	content->setObjectName("pageContent");
	content->setAttribute(Qt::WA_StyledBackground, true);

	m_contentLayout = new QVBoxLayout(content);
	m_contentLayout->setContentsMargins(20, 16, 20, 16);
	m_contentLayout->setSpacing(14);
	content->setLayout(m_contentLayout);

	m_pageLayout->addWidget(content, 1);   //内容区吃掉所有多余空间

	// ---------- 底部按钮条 ----------
	QWidget* footer = new QWidget(this);
	footer->setObjectName("pageFooter");
	footer->setAttribute(Qt::WA_StyledBackground, true);

	m_footerLayout = new QHBoxLayout(footer);
	m_footerLayout->setContentsMargins(20, 10, 20, 10);
	m_footerLayout->setSpacing(14);
	footer->setLayout(m_footerLayout);

	m_pageLayout->addWidget(footer);

	setLayout(m_pageLayout);
}

void ScaffoldScreen::setPageTitle(const QString& title, const QString& subtitle)
{
	m_titleLabel->setText(title);
	m_subtitleLabel->setText(subtitle);
	m_subtitleLabel->setVisible(!subtitle.isEmpty());
}

QPushButton* ScaffoldScreen::addFooterButton(const QString& text, const QString& objectName)
{
	QPushButton* button = new QPushButton(text, this);
	if (!objectName.isEmpty()) { button->setObjectName(objectName); }

	//横向可拉伸：一个按钮时铺满、多个按钮时等分（与旧的"一行三个等宽按钮"一致）
	button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	button->setMinimumHeight(40);

	m_footerLayout->addWidget(button);
	return button;
}

void ScaffoldScreen::addFooterStretch()
{
	m_footerLayout->addStretch(1);
}

QPushButton* ScaffoldScreen::addBackButton(const QString& text)
{
	QPushButton* button = addFooterButton(text, QStringLiteral("accentBtn"));
	connect(button, &QPushButton::clicked, this, [this]() { emit requestBack(); });
	return button;
}
