#include "ModeSelectScreen.h"
#include "ScreenRouter.h"
#include "NativeModHost.h"
#include "FileManager.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ModeSelectScreen::ModeSelectScreen(QWidget* parent) : ScaffoldScreen(parent)
{
	setPageTitle(QStringLiteral("选择模式"),
				 QStringLiteral("单人闯关练手，或者和朋友联机对战"));

	// ---------- 内置模式 ----------
	QPushButton* single = addModeCard(QStringLiteral("单人闯关"),
		QStringLiteral("按关卡顺序挑战，通关解锁下一关"), QStringLiteral("singleModeCard"));
	connect(single, &QPushButton::clicked, this,
			[this]() { ScreenRouter::instance().push(ScreenId::LevelSelect); });

	QPushButton* multi = addModeCard(QStringLiteral("联机对战"),
		QStringLiteral("同一局域网内最多 4 人一起炸"), QStringLiteral("multiModeCard"));
	connect(multi, &QPushButton::clicked, this,
			[this]() { ScreenRouter::instance().push(ScreenId::Lobby); });

	// ---------- 更多模式 ----------
	//这里**不写"能力位/接口版本"这类开发细节**：玩家只关心"以后还会有别的模式"。
	//等 mod 模式真的做出来，这一区就按各 mod 声明的模式生成卡片（接口已经留好）。
	QLabel* extendedTitle = new QLabel(QStringLiteral("更多模式"), this);
	extendedTitle->setObjectName("sectionTitle");
	contentLayout()->addSpacing(8);
	contentLayout()->addWidget(extendedTitle);

	m_extendedLayout = new QVBoxLayout();
	m_extendedLayout->setContentsMargins(0, 0, 0, 0);
	m_extendedLayout->setSpacing(10);
	contentLayout()->addLayout(m_extendedLayout);

	addFooterStretch();
	addBackButton();
}

//加一张模式卡片
//用 QPushButton 而不是"自绘控件"：它自带点击/hover/禁用三态与键盘可达性，
//而"长得像卡片"完全由 QSS 决定（#modeCard），不需要写绘制代码
QPushButton* ModeSelectScreen::addModeCard(const QString& title, const QString& hint,
										   const QString& objectName)
{
	QPushButton* card = new QPushButton(this);
	card->setObjectName(QStringLiteral("modeCard"));
	card->setProperty("modeId", objectName);
	card->setText(QStringLiteral("%1\n%2").arg(title, hint));
	card->setToolTip(hint);
	card->setMinimumHeight(76);
	card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	m_cards.append(card);
	contentLayout()->addWidget(card);
	return card;
}

//加一张"更多模式"卡片（放进扩展区）
QPushButton* ModeSelectScreen::newCard(const QString& title, const QString& hint)
{
	QPushButton* card = new QPushButton(this);
	card->setObjectName(QStringLiteral("modeCard"));
	card->setProperty("modeMod", true);
	card->setText(QStringLiteral("%1\n%2").arg(title, hint));
	card->setToolTip(hint);
	card->setMinimumHeight(76);
	card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	m_cards.append(card);
	if (m_extendedLayout) { m_extendedLayout->addWidget(card); }
	return card;
}

void ModeSelectScreen::onEnter(const QVariantMap& args)
{
	Q_UNUSED(args);
	refreshExtendedModes();
}

//重建"更多模式"区：把模组注册的模式变成卡片
//
// 这里刻意**不写"接口版本/能力位"这类开发细节**：玩家看到的只是"这个模式叫什么、
// 一句话说明、能不能玩"。不能用的时候给一句人话原因（和模组页同一条规矩）。
void ModeSelectScreen::refreshExtendedModes()
{
	if (!m_extendedLayout) { return; }

	//清空旧内容
	while (QLayoutItem* item = m_extendedLayout->takeAt(0))
	{
		if (QWidget* widget = item->widget()) { widget->deleteLater(); }
		delete item;
	}

	const QList<NativeModHost::ModMode> modes = NativeModHost::instance().modes();

	if (modes.isEmpty())
	{
		QLabel* hint = new QLabel(QStringLiteral("更多模式正在准备中，敬请期待。"), this);
		hint->setObjectName("modeEmptyHint");
		hint->setWordWrap(true);
		m_extendedLayout->addWidget(hint);
		return;
	}

	for (const NativeModHost::ModMode& mode : modes)
	{
		const QString hint = mode.hint.isEmpty()
			? (mode.description.isEmpty() ? QStringLiteral("由模组提供的玩法") : mode.description)
			: mode.hint;

		QPushButton* card = newCard(QStringLiteral("%1（模组）").arg(mode.name), hint);
		card->setToolTip(mode.description.isEmpty() ? hint : mode.description);

		if (!mode.usable)
		{
			//模式声明了但当前玩不了：卡片留着（让玩家知道有这么个东西），
			//但明确说原因 —— "明明是灰的却不知道为什么"是最劝退的一种状态
			card->setEnabled(false);
			card->setText(card->text() + QStringLiteral("\n%1").arg(mode.reason));
			continue;
		}

		const QString key = mode.key();
		connect(card, &QPushButton::clicked, this, [this, key]()
		{
			FileManager::instance().log("ui", QStringLiteral("选择模组模式：%1").arg(key));
			ScreenRouter::instance().push(ScreenId::SingleGame,
										 QVariantMap{ { QStringLiteral("modMode"), key } });
		});
	}
}
