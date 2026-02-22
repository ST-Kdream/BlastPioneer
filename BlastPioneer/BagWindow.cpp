#include "BagWindow.h"

//构造函数
BagWindow::BagWindow(PlayerInfo& playerInfo, QWidget* parent) :QWidget(parent), playerInfo(playerInfo)
{
	setupUI();
	refreshItemList();
}

//UI设置（包括信号槽链接）
void BagWindow::setupUI()
{
	setWindowTitle("玩家背包");

	// 设置默认大小，如果QSettings中没有保存的值则使用默认值
	int defaultWidth = 800;
	int defaultHeight = 600;
	int width = settings->value("windows/width", defaultWidth).toInt();
	int height = settings->value("windows/height", defaultHeight).toInt();
	resize(width, height);

	// 恢复窗口位置
	QPoint defaultPos(100, 100);  // 默认位置
	QPoint savedPos = settings->value("windows/position", defaultPos).toPoint();
	move(savedPos);

	//标题
	title = new QLabel("我的背包", this);
	title->setStyleSheet("font-size: 18px; font-weight: bold; color: #333;");

	//使用按钮、返回按钮(链接信号槽)
	useBtn = new QPushButton("使用", this);
	backBtn = new QPushButton("返回", this);
	connect(useBtn, &QPushButton::clicked, this, &BagWindow::useItem);
	connect(backBtn, &QPushButton::clicked, this, &BagWindow::goBack);

	//商品列表
	itemList = new QListWidget(this);

	//布局
	QVBoxLayout* mainLayout = new QVBoxLayout();
	mainLayout->addWidget(title, Qt::AlignLeft);
	mainLayout->addWidget(itemList);

	QHBoxLayout* btnLine = new QHBoxLayout();
	btnLine->addWidget(useBtn);
	btnLine->addWidget(backBtn);
	mainLayout->addLayout(btnLine);
	setLayout(mainLayout);
}

//更新道具列表
void BagWindow::refreshItemList()
{
	itemList->clear();
	QMap<QString, int> inventory = playerInfo.getInventory();

	for (auto it = inventory.begin(); it != inventory.end(); ++it)
	{
		QString displayText = QString("%1：%2").arg(it.key()).arg(it.value());
		itemList->addItem(displayText);
	}
	if (inventory.empty())
	{
		itemList->addItem("背包空空如也~~");
	}
}

//使用道具
void BagWindow::useItem()
{
	QListWidgetItem* currentItem = itemList->currentItem();
	if (!currentItem)
	{
		QMessageBox::information(this, "提示", "请选择一个道具");
		return;
	}

	QString itemText = currentItem->text();
	QString itemName = itemText.section("：", 0, 0);
	playerInfo.removeItem(itemName);
	QMessageBox::information(this, "提示", QString("道具：%1 使用成功").arg(itemName));
	refreshItemList();
}

//返回
void BagWindow::goBack()
{
	close();
}