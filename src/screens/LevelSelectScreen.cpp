#include "LevelSelectScreen.h"
#include "ScreenRouter.h"
#include "PlayerProfile.h"
#include "LevelDatabase.h"
#include "ScaffoldScreen.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>

LevelSelectScreen::LevelSelectScreen(QWidget* parent) : ScaffoldScreen(parent)
{
	setupUI();
}

//UI设置（关卡按钮在 rebuildLevelButtons 里生成）
//样式全部走 objectName：关卡按钮是 #levelBtn（圆形大号数字），底部是通用 #accentBtn
void LevelSelectScreen::setupUI()
{
	setPageTitle(QStringLiteral("关卡选择"),
				 QStringLiteral("通关一关解锁下一关；关卡数量以实际数据为准"));

	levelGrid = new QGridLayout();
	levelGrid->setSpacing(20);
	for (int c = 0; c < kColumns; ++c)
	{
		levelGrid->setColumnStretch(c, 1);
	}

	contentLayout()->addLayout(levelGrid);
	contentLayout()->addStretch(1);

	addFooterStretch();
	addBackButton();

	//先建一次，保证构造完成后界面就是完整的（onEnter 会再按最新数据重建）
	rebuildLevelButtons();
}

//按 LevelDatabase 的内容重建关卡按钮
//为什么每次都重建：关卡是数据，改完 JSON 只要重新进这个界面就能看到，不需要重启程序 ——
//"数据驱动"要能被眼睛看见才算数
void LevelSelectScreen::rebuildLevelButtons()
{
	if (!levelGrid) { return; }

	for (QPushButton* button : levelBtns)
	{
		levelGrid->removeWidget(button);
		button->deleteLater();
	}
	levelBtns.clear();
	levelIds.clear();

	const QList<bp::LevelDefinition> levels = LevelDatabase::instance().all();

	//关卡数来自数据；一个都没读到就用兜底的 6 关（程序化随机地图）
	const int count = levels.isEmpty() ? kFallbackLevelCount : levels.size();

	//副标题把进度写清楚：玩家一眼能看出"我还有几关没解锁"
	const int passed = PlayerProfile::instance().info().getPassedLevels();
	setPageTitle(QStringLiteral("关卡选择"),
				 levels.isEmpty()
					 ? QStringLiteral("没有读到关卡文件，用程序化随机地图兜底（共 %1 关）").arg(count)
					 : QStringLiteral("已通关 %1 关 · 共 %2 关 · 通关一关解锁下一关")
						   .arg(qMin(passed, count)).arg(count));

	for (int i = 0; i < count; ++i)
	{
		const int levelId = levels.isEmpty() ? (i + 1) : levels.at(i).id;
		const QString levelName = levels.isEmpty() ? QString() : levels.at(i).name;

		QPushButton* button = new QPushButton(QString::number(levelId), this);
		button->setObjectName(QStringLiteral("levelBtn"));
		button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
		button->setMinimumSize(80, 80);
		button->setMaximumSize(120, 120);
		button->setCursor(Qt::PointingHandCursor);

		//关卡名放提示里：按钮上还是大号数字
		const QString hint = levelName.isEmpty() ? QStringLiteral("第 %1 关").arg(levelId)
												 : QStringLiteral("%1：%2").arg(levelId).arg(levelName);
		button->setToolTip(hint);
		button->setProperty("levelHint", hint);   //applyUnlockState 会在这上面加"未解锁"说明

		connect(button, &QPushButton::clicked, this, [this, levelId]() { goGameWindow(levelId); });

		levelGrid->addWidget(button, i / kColumns, i % kColumns);
		levelBtns.append(button);
		levelIds.append(levelId);
	}

	applyUnlockState();
}

//解锁规则：**按列表里的位置**，不按关卡号
//
// 为什么不能按关卡号（原来就是 `passedLevels >= levelId - 1`）：
// 模组可以加一个"99 关"，那它就要求先通过第 98 关 —— 永远打不开，玩家只会觉得坏了。
// 按位置解释才符合直觉：第 N 个关卡需要先通过前面的 N-1 个。
void LevelSelectScreen::applyUnlockState()
{
	const int passedLevels = PlayerProfile::instance().info().getPassedLevels();

	for (int i = 0; i < levelBtns.size(); ++i)
	{
		const bool unlocked = (passedLevels >= i);
		levelBtns[i]->setEnabled(unlocked);

		//没解锁的关卡要说清楚"为什么"，否则玩家只会觉得"关卡没了"
		//（真实反馈就是这么来的：按钮其实在，只是看不见也点不动）
		const QString base = levelBtns[i]->property("levelHint").toString();
		levelBtns[i]->setToolTip(unlocked
			? base
			: QStringLiteral("%1\n未解锁：先通过上一关").arg(base));
	}
}

//进入界面时按进度解锁关卡，并按最新的关卡数据重建按钮
void LevelSelectScreen::onEnter(const QVariantMap& args)
{
	Q_UNUSED(args);
	rebuildLevelButtons();
}

//进入游戏：替换栈顶，返回时仍回到本界面
void LevelSelectScreen::goGameWindow(int level)
{
	QVariantMap args;
	args.insert("level", level);
	ScreenRouter::instance().replace(ScreenId::SingleGame, args);
}
