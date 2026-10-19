#include "GameHud.h"

#include <QHBoxLayout>
#include <QLabel>

// ============================================================
// 游戏内顶部信息条
//
// 两个必须记住的点：
//   1. 自定义 QWidget 子类要设 WA_StyledBackground，QSS 的 background-color 才会画出来。
//      以前没设 → 面板背景其实没画，浅色窗口底色上写着近白色的字，于是"看不清"。
//   2. 颜色/字号一律用 objectName 交给样式表（#gameHud / #hudValue / #hudHint），
//      不再在这里写内联样式 —— 这样换主题才是改一处 qss。
//
// 为什么 HUD 在两套主题里都是深色：它贴在游戏画布上，而画布的颜色是玩法的一部分
// （深色底才看得清炸弹与爆炸预警），所以这一块不跟着主题变浅。
// ============================================================
GameHud::GameHud(QWidget* parent) : QWidget(parent)
{
	setObjectName("gameHud");
	setAttribute(Qt::WA_StyledBackground, true);
	setFixedHeight(36);

	m_levelLabel = new QLabel(QStringLiteral("关卡 -"), this);
	m_livesLabel = new QLabel(QStringLiteral("生命 -"), this);
	m_bombLabel = new QLabel(QStringLiteral("炸弹 -"), this);
	m_hintLabel = new QLabel(QStringLiteral("WASD 移动  Space 放弹  B 背包  ESC 返回"), this);

	//三个数值共用一个 objectName：样式表里一条规则就够（QSS 的 id 选择器会匹配所有同名控件）
	m_levelLabel->setObjectName("hudValue");
	m_livesLabel->setObjectName("hudValue");
	m_bombLabel->setObjectName("hudValue");
	m_hintLabel->setObjectName("hudHint");

	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(14, 0, 14, 0);
	layout->setSpacing(22);
	layout->addWidget(m_levelLabel);
	layout->addWidget(m_livesLabel);
	layout->addWidget(m_bombLabel);
	layout->addStretch(1);
	layout->addWidget(m_hintLabel);

	setLayout(layout);
}

void GameHud::refresh()
{
	if (!m_source) { return; }

	const bp::GameSnapshot& snap = m_source->snapshot();

	//多人对局：HUD 显示"我的"状态 + 存活人数
	//（快照里 localPlayerIndex 指向本机玩家；单人是 -1，走下面那套）
	if (snap.localPlayerIndex >= 0 && snap.localPlayerIndex < snap.players.size())
	{
		const bp::GameSnapshot::PlayerView& me = snap.players.at(snap.localPlayerIndex);

		m_levelLabel->setText(QStringLiteral("对战"));
		m_livesLabel->setText(QStringLiteral("生命 %1/%2").arg(me.lives).arg(me.maxLives));
		m_bombLabel->setText(QStringLiteral("炸弹 威力%1 上限%2").arg(me.bombRange).arg(me.maxBombs));

		int alive = 0;
		for (const bp::GameSnapshot::PlayerView& player : snap.players)
		{
			if (player.alive) { ++alive; }
		}

		QString hint = QStringLiteral("存活 %1/%2").arg(alive).arg(snap.players.size());
		if (me.ghost) { hint += QStringLiteral("   幽灵态"); }
		m_hintLabel->setText(hint);
		return;
	}

	m_levelLabel->setText(QStringLiteral("关卡 %1").arg(snap.level));
	m_livesLabel->setText(QStringLiteral("生命 %1/%2").arg(snap.playerLives).arg(snap.playerMaxLives));
	m_bombLabel->setText(QStringLiteral("炸弹 威力%1 上限%2")
		.arg(snap.playerBombRange).arg(snap.playerMaxBombs));

	//剩余敌人（只统计还活着的；dying 的算已死）
	int alive = 0;
	for (const bp::Enemy& enemy : snap.enemies)
	{
		if (!enemy.dying) { ++alive; }
	}

	QString hint = QStringLiteral("剩余敌人 %1").arg(alive);
	if (snap.playerGhost) { hint += QStringLiteral("   幽灵态"); }
	m_hintLabel->setText(hint);
}
