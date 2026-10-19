#include "WelcomeScreen.h"
#include "ScreenRouter.h"
#include "PlayerProfile.h"
#include "FileManager.h"
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>

WelcomeScreen::WelcomeScreen(QWidget* parent) : ScreenBase(parent)
{
	setupUI();
}

//UI设置
void WelcomeScreen::setupUI()
{
	m_title = new QLabel("欢迎来到爆破先锋", this);
	m_title->setAlignment(Qt::AlignCenter);
	m_title->setObjectName("welcomeTitle");

	m_tip = new QLabel(
		"还没有存档。点击下面的按钮创建一份新的存档：\n"
		"默认昵称 Bommer，初始金币 100，并附带新手礼包"
		"（生命药水×3、迅猛之靴×1、改造扳手×1）。",
		this);
	m_tip->setAlignment(Qt::AlignCenter);
	m_tip->setWordWrap(true);
	m_tip->setObjectName("welcomeTip");

	m_startBtn = new QPushButton("开始新手存档", this);
	m_quitBtn = new QPushButton("退出游戏", this);

	//样式走 objectName：开始=主色，退出=次要
	m_startBtn->setObjectName("primaryBtn");
	m_quitBtn->setObjectName("ghostBtn");

	connect(m_startBtn, &QPushButton::clicked, this, &WelcomeScreen::startNewProfile);
	//退出统一交给 AppHost（弹确认框 + 存档）
	connect(m_quitBtn, &QPushButton::clicked, this, &WelcomeScreen::requestQuit);

	//布局
	QVBoxLayout* mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(40, 60, 40, 40);
	mainLayout->setSpacing(24);
	mainLayout->addWidget(m_title);
	mainLayout->addWidget(m_tip, 1);

	QHBoxLayout* btnLine = new QHBoxLayout();
	btnLine->setSpacing(20);
	for (auto btn : { m_startBtn, m_quitBtn })
	{
		btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		btnLine->addWidget(btn);
	}
	mainLayout->addLayout(btnLine);

	setLayout(mainLayout);
}

//进入界面
void WelcomeScreen::onEnter(const QVariantMap& args)
{
	Q_UNUSED(args);
}

//创建新手存档
void WelcomeScreen::startNewProfile()
{
	PlayerProfile& profile = PlayerProfile::instance();

	//默认昵称 / 金币 / 新手礼包都在 PlayerInfo::setDefaults 里
	profile.info().setDefaults();

	if (!profile.save())
	{
		QMessageBox::warning(this, "保存失败", "存档写入失败，请检查游戏目录是否有写入权限。");
		return;
	}

	FileManager::instance().log("profile", "已创建新手存档");
	ScreenRouter::instance().reset(ScreenId::Home);
}
