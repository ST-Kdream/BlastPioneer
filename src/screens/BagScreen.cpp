#include "BagScreen.h"
#include "ScreenRouter.h"
#include "PlayerProfile.h"
#include "ItemDatabase.h"
#include "SingleGameScreen.h"
#include "FigManager.h"
#include "FileManager.h"
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QIcon>

BagScreen::BagScreen(QWidget* parent) : ScreenBase(parent)
{
	//自定义 QWidget 子类必须开这个属性，QSS 里的 background-color 才会真的画出来。
	//不开的表现就是"设了背景色但看不见"—— 覆盖层会把游戏画面透出来，文字糊在一起
	setAttribute(Qt::WA_StyledBackground, true);
	setObjectName("bagPanel");

	setupUI();
}

//UI设置（包括信号槽链接）
//注意：这里只做布局与结构，颜色/字号一律交给样式表（#bagPanel / #bagTitle / #itemList / #useBtn / #backBtn），
//这样"改配色"才是改一处 qss，而不是翻 13 个界面找内联字符串
void BagScreen::setupUI()
{
	setWindowTitle("玩家背包");

	titleLabel = new QLabel("我的背包", this);
	titleLabel->setObjectName("bagTitle");
	titleLabel->setAlignment(Qt::AlignHCenter);

	useBtn = new QPushButton("使用", this);
	backBtn = new QPushButton("返回", this);
	useBtn->setObjectName("useBtn");
	backBtn->setObjectName("backBtn");

	connect(useBtn, &QPushButton::clicked, this, &BagScreen::useItem);
	connect(backBtn, &QPushButton::clicked, this, &BagScreen::goBack);

	itemList = new QListWidget(this);
	itemList->setObjectName("itemList");
	itemList->setIconSize(QSize(32, 32));

	//布局
	QVBoxLayout* mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(24, 24, 24, 24);
	mainLayout->setSpacing(14);

	mainLayout->addWidget(titleLabel);
	mainLayout->addWidget(itemList, 1);

	QHBoxLayout* btnLine = new QHBoxLayout();
	btnLine->setSpacing(16);
	for (auto btn : { useBtn, backBtn }) {
		btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		btnLine->addWidget(btn);
	}
	mainLayout->addLayout(btnLine);

	setLayout(mainLayout);
}

//进入界面时刷新列表（走窗口栈的那条路径）
void BagScreen::onEnter(const QVariantMap& args)
{
	Q_UNUSED(args);
	refresh();
}

//设置为游戏内覆盖层模式
void BagScreen::setGameScreen(SingleGameScreen* gameScreen)
{
	this->gameScreen = gameScreen;
	isGameRunning = (gameScreen != nullptr);
}

//刷新标题与道具列表
void BagScreen::refresh()
{
	//标题顺带说明"游戏已暂停"：玩家看到 B 打开背包后画面不动，需要知道这是有意的
	titleLabel->setText(isGameRunning ? QStringLiteral("我的背包（游戏已暂停）")
									  : QStringLiteral("我的背包"));

	refreshItemList();
}

//更新道具列表
void BagScreen::refreshItemList()
{
	itemList->clear();

	const QMap<QString, int> inventory = PlayerProfile::instance().info().getInventory();

	for (auto it = inventory.begin(); it != inventory.end(); ++it)
	{
		QString displayText = QString("%1：%2").arg(it.key()).arg(it.value());
		QListWidgetItem* item = new QListWidgetItem(displayText, itemList);
		item->setToolTip(QStringLiteral("%1（数量 %2）").arg(it.key()).arg(it.value()));

		//图标路径由道具数据库统一提供，缺图不再弹窗
		//图标走 FigManager 缓存（启动时已预热），不再每次读盘 + 缩放
		Fig fig;
		if (FigCatalog::itemFig(it.key(), fig))
		{
			item->setIcon(QIcon(FigManager::instance().scaled(fig, QSize(32, 32))));
		}
	}

	if (inventory.isEmpty())
	{
		//空背包是**正常状态**（新存档本来就没有道具，要去商店买）：
		//这里给出下一步提示，而不是让人以为界面坏了
		QListWidgetItem* hint = new QListWidgetItem(
			QStringLiteral("背包空空如也 —— 去「商店」用金币买道具，或进关卡炸砖块碰运气掉落"), itemList);
		hint->setFlags(Qt::NoItemFlags);   //占位提示，不可选中，避免"使用它"
	}
}

//使用道具
//关键改动：道具效果不再通过 Item::usageMap 里的"界面方法指针"执行，
//而是交给引擎的效果注册表（bp::GameEngine::applyEffect）。
//这样"道具叫什么/卖多少钱"是数据，"效果做什么"是引擎，界面只负责调用。
void BagScreen::useItem()
{
	QListWidgetItem* currentItem = itemList->currentItem();
	if (!currentItem)
	{
		QMessageBox::information(this, "提示", "请选择一个道具");
		return;
	}

	QString itemText = currentItem->text();
	QString itemName = itemText.section("：", 0, 0);

	if (!isGameRunning)
	{
		QMessageBox::information(this, "提示", "道具只能在游戏运行时才能使用（游戏时按B键打开）");
		return;
	}

	bp::SingleGameEngine* engine = gameScreen ? gameScreen->engine() : nullptr;
	if (!engine)
	{
		FileManager::instance().log("bag", "游戏内背包没有拿到引擎指针");
		QMessageBox::warning(this, "提示", "当前没有进行中的游戏");
		return;
	}

	//道具数据找不到时也算失败（避免"用了但没反应"这种迷惑表现）
	if (!ItemDatabase::instance().find(itemName))
	{
		FileManager::instance().log("bag", QString("未登记的道具: %1").arg(itemName));
		QMessageBox::warning(this, "提示", QString("未找到道具数据：%1").arg(itemName));
		return;
	}

	//效果ID由 ItemEffects 从道具名翻译；返回 false 表示这个道具暂时没有实现效果
	if (!engine->applyEffect(itemName))
	{
		QMessageBox::information(this, "提示", QString("道具 %1 当前没有可用效果").arg(itemName));
		return;
	}

	PlayerProfile::instance().info().removeItem(itemName);
	refreshItemList();
	PlayerProfile::instance().save();
	QMessageBox::information(this, "提示", QString("使用道具%1成功").arg(itemName));
}

//返回
void BagScreen::goBack()
{
	emit requestBack();
}
