#include "HomeScreen.h"
#include "ScreenRouter.h"
#include "PlayerProfile.h"
#include "LevelDatabase.h"
#include "ModManager.h"

#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFrame>

//主页
//
// 上一版是"一个大按钮 + 一排小按钮 + 一大片空白"，看着像占位图。
// 这一版的原则是：**用真实信息把版面填满**，而不是靠加大间距和留白 ——
//   · 顶部一张主视觉卡片：游戏名 + 一句话定位 + 玩家当前状态（段位/金币/经验/进度）
//     + 醒目的「开始游戏」（固定宽度、居左，而不是铺满整行的巨大色块）
//   · 下面四个入口做成"标题 + 一句说明"的卡片，2×2 排布（卡片本身带信息量）
//   · 颜色/圆角/间距全部来自 ui/*.qss 的令牌，这里只写 objectName
HomeScreen::HomeScreen(QWidget* parent) : ScaffoldScreen(parent)
{
	setupUI();
}

void HomeScreen::setupUI()
{
	setPageTitle(QStringLiteral("爆破先锋"), QStringLiteral("炸弹人风格的闯关与局域网对战"));

	//右上角：这是谁在玩、什么版本
	m_who = new QLabel(this);
	m_who->setObjectName("headerMeta");
	headerExtraLayout()->addWidget(m_who);

	// ---------- 主视觉卡片 ----------
	QFrame* hero = new QFrame(this);
	hero->setObjectName("heroCard");
	hero->setAttribute(Qt::WA_StyledBackground, true);

	QVBoxLayout* heroLayout = new QVBoxLayout(hero);
	heroLayout->setContentsMargins(28, 24, 28, 24);
	heroLayout->setSpacing(10);

	QLabel* heroTitle = new QLabel(QStringLiteral("爆破先锋"), hero);
	heroTitle->setObjectName("heroTitle");

	QLabel* heroTagline = new QLabel(
		QStringLiteral("放下炸弹、炸开砖块、清掉所有敌人 —— 关卡可以改，玩法也可以改"), hero);
	heroTagline->setObjectName("heroTagline");
	heroTagline->setWordWrap(true);

	//玩家状态：这一行是"用真实信息填满版面"的主力（内容由 refreshLiveInfo 填）
	m_stats = new QLabel(hero);
	m_stats->setObjectName("heroStats");
	m_stats->setWordWrap(true);

	m_startBtn = new QPushButton(QStringLiteral("开始游戏"), hero);
	m_startBtn->setObjectName("startBtn");
	m_startBtn->setMinimumSize(240, 56);
	m_startBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	m_startBtn->setCursor(Qt::PointingHandCursor);
	m_startBtn->setToolTip(QStringLiteral("选择模式：单人闯关 / 联机对战"));

	//按钮靠左、不铺满整行：右边留白比"一个占满屏幕的色块"舒服得多
	QHBoxLayout* heroButtons = new QHBoxLayout();
	heroButtons->setSpacing(12);
	heroButtons->addWidget(m_startBtn);
	heroButtons->addStretch(1);

	heroLayout->addWidget(heroTitle);
	heroLayout->addWidget(heroTagline);
	heroLayout->addWidget(m_stats);
	heroLayout->addSpacing(6);
	heroLayout->addLayout(heroButtons);

	contentLayout()->addWidget(hero);

	// ---------- 四个入口：2×2 卡片（标题 + 说明） ----------
	//"已启用 N 个"也在 refreshLiveInfo 里填：构造函数跑在模组加载之前
	QGridLayout* grid = new QGridLayout();
	grid->setSpacing(14);

	QPushButton* playerCard = addEntryCard(grid, 0, 0, QStringLiteral("玩家信息"),
		QStringLiteral("段位、金币、背包与道具图鉴"), QStringLiteral("playerCard"));
	m_modsCard = addEntryCard(grid, 0, 1, QStringLiteral("模组"),
		QStringLiteral("改数值、加关卡、换配色"), QStringLiteral("modsCard"));
	QPushButton* rulesCard = addEntryCard(grid, 1, 0, QStringLiteral("游戏规则"),
		QStringLiteral("操作、机制、道具与联机说明"), QStringLiteral("rulesCard"));
	QPushButton* settingsCard = addEntryCard(grid, 1, 1, QStringLiteral("设置"),
		QStringLiteral("外观与主题、帧率与特效、音量"), QStringLiteral("settingsCard"));

	contentLayout()->addLayout(grid);
	contentLayout()->addStretch(1);

	refreshLiveInfo();

	// ---------- 跳转 ----------
	connect(m_startBtn, &QPushButton::clicked, this,
			[this]() { ScreenRouter::instance().push(ScreenId::ModeSelect); });
	connect(playerCard, &QPushButton::clicked, this,
			[this]() { ScreenRouter::instance().push(ScreenId::Player); });
	connect(m_modsCard, &QPushButton::clicked, this,
			[this]() { ScreenRouter::instance().push(ScreenId::Mods); });
	connect(rulesCard, &QPushButton::clicked, this,
			[this]() { ScreenRouter::instance().push(ScreenId::Rules); });
	connect(settingsCard, &QPushButton::clicked, this,
			[this]() { ScreenRouter::instance().push(ScreenId::Settings); });
}

//进入主页：刷一遍实时数据
//
// 这条是本类最容易漏掉的一步：界面是启动时"预创建"的（见 main.cpp 的 precreateAll），
// 构造发生在**存档/关卡/模组加载之前**。所以构造函数里读到的金币、通关数、模组数
// 全是初始值 —— 玩家会以为主页那一行数字是写死的。
void HomeScreen::onEnter(const QVariantMap& args)
{
	Q_UNUSED(args);
	refreshLiveInfo();
}

//把主页上所有"会变的信息"重新读一遍
void HomeScreen::refreshLiveInfo()
{
	if (m_who)
	{
		m_who->setText(QStringLiteral("%1　·　v%2")
			.arg(PlayerProfile::instance().info().getUserName(), QApplication::applicationVersion()));
	}

	if (m_stats)
	{
		const PlayerInfo& info = PlayerProfile::instance().info();
		const int levelCount = LevelDatabase::instance().count();
		m_stats->setText(QStringLiteral("段位 %1　·　金币 %2　·　经验 %3　·　已通关 %4 关　·　可用关卡 %5")
			.arg(info.getRank().isEmpty() ? QStringLiteral("未定级") : info.getRank())
			.arg(info.getCoins())
			.arg(info.getEP())
			.arg(info.getPassedLevels())
			.arg(levelCount > 0 ? QString::number(levelCount) : QStringLiteral("（程序化生成）")));
	}

	if (m_modsCard)
	{
		const int modCount = ModManager::instance().activeMods().size();
		const QString hint = modCount > 0
			? QStringLiteral("已启用 %1 个：改数值、加关卡、换配色").arg(modCount)
			: QStringLiteral("改数值、加关卡、换配色 —— 放进 mods 文件夹即可");
		m_modsCard->setText(QStringLiteral("模组\n%1").arg(hint));
		m_modsCard->setToolTip(hint);
	}
}

//加一张入口卡片：标题一行 + 说明一行，整体是一个按钮（点击区域大、键盘可达）
QPushButton* HomeScreen::addEntryCard(QGridLayout* grid, int row, int column,
									  const QString& title, const QString& hint,
									  const QString& cardId)
{
	QPushButton* card = new QPushButton(this);
	card->setObjectName("entryCard");
	card->setProperty("cardId", cardId);
	card->setText(QStringLiteral("%1\n%2").arg(title, hint));
	card->setToolTip(hint);
	card->setMinimumHeight(86);
	card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	card->setCursor(Qt::PointingHandCursor);

	grid->addWidget(card, row, column);
	return card;
}
