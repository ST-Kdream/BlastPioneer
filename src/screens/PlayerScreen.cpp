#include "PlayerScreen.h"
#include "ScreenRouter.h"
#include "PlayerProfile.h"

#include <QLabel>
#include <QPushButton>
#include <QDialog>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QMessageBox>

PlayerScreen::PlayerScreen(QWidget* parent) : ScaffoldScreen(parent)
{
	setupUI();
	connectBtn();
}

//UI：五行"项目 + 数值"，底部三个动作按钮
//样式全部来自 objectName（#statKey / #statValue / 三种按钮角色），这里不写任何颜色
void PlayerScreen::setupUI()
{
	setPageTitle(QStringLiteral("玩家信息"), QStringLiteral("本地存档里的进度与资产"));

	const PlayerInfo& info = PlayerProfile::instance().info();

	userName = new QLabel(info.getUserName(), this);
	coins = new QLabel(QString::number(info.getCoins()), this);
	rank = new QLabel(info.getRank(), this);
	EP = new QLabel(QString::number(info.getEP()), this);
	passedLevels = new QLabel(QString::number(info.getPassedLevels()), this);

	//数值标签统一用一个 objectName：样式表里一条规则就能统一字号与颜色
	for (QLabel* value : { userName, coins, rank, EP, passedLevels })
	{
		value->setObjectName("statValue");
	}

	//改名按钮放在"名字"那一行右边
	newNameBtn = new QPushButton(QStringLiteral("更改名字"), this);
	newNameBtn->setObjectName("ghostBtn");
	newNameBtn->setCursor(Qt::PointingHandCursor);

	const QList<QPair<QString, QLabel*>> rows = {
		{ QStringLiteral("名字"), userName },
		{ QStringLiteral("金币"), coins },
		{ QStringLiteral("段位"), rank },
		{ QStringLiteral("经验"), EP },
		{ QStringLiteral("已通过关卡"), passedLevels },
	};

	QGridLayout* grid = new QGridLayout();
	grid->setHorizontalSpacing(18);
	grid->setVerticalSpacing(12);
	grid->setColumnStretch(1, 1);

	int row = 0;
	for (const auto& pair : rows)
	{
		QLabel* key = new QLabel(pair.first, this);
		key->setObjectName("statKey");

		grid->addWidget(key, row, 0);
		grid->addWidget(pair.second, row, 1);

		if (pair.second == userName)
		{
			grid->addWidget(newNameBtn, row, 2);
		}

		++row;
	}

	contentLayout()->addLayout(grid);
	contentLayout()->addStretch(1);

	//底部动作：背包 / 商店 / 返回（角色化按钮 = 主色 / 次要 / 强调）
	QPushButton* bagBtn = addFooterButton(QStringLiteral("背包"), QStringLiteral("primaryBtn"));
	QPushButton* shopBtn = addFooterButton(QStringLiteral("商店"), QStringLiteral("ghostBtn"));
	addBackButton();

	connect(bagBtn, &QPushButton::clicked, this, &PlayerScreen::goBag);
	connect(shopBtn, &QPushButton::clicked, this, &PlayerScreen::goShopping);
}

//链接信号槽函数
void PlayerScreen::connectBtn()
{
	connect(newNameBtn, &QPushButton::clicked, this, &PlayerScreen::changeName);
}

//进入界面时刷新数据
void PlayerScreen::onEnter(const QVariantMap& args)
{
	Q_UNUSED(args);
	updateUI();
}

//UI更新函数
void PlayerScreen::updateUI()
{
	const PlayerInfo& info = PlayerProfile::instance().info();
	userName->setText(info.getUserName());
	coins->setText(QString::number(info.getCoins()));
	rank->setText(info.getRank().isEmpty() ? QStringLiteral("未定级") : info.getRank());
	EP->setText(QString::number(info.getEP()));
	passedLevels->setText(QStringLiteral("%1 关").arg(info.getPassedLevels()));
}

//更改玩家姓名
void PlayerScreen::changeName()
{
	QDialog newNameInput(this);
	newNameInput.setWindowTitle(QStringLiteral("更改姓名"));
	newNameInput.setFixedSize(320, 150);

	QVBoxLayout* mainLayout = new QVBoxLayout(&newNameInput);
	QLineEdit* nameInput = new QLineEdit(&newNameInput);
	nameInput->setPlaceholderText(QStringLiteral("输入新姓名"));
	QDialogButtonBox* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &newNameInput);

	connect(btnBox, &QDialogButtonBox::accepted, &newNameInput, &QDialog::accept);
	connect(btnBox, &QDialogButtonBox::rejected, &newNameInput, &QDialog::reject);

	mainLayout->addWidget(nameInput);
	mainLayout->addWidget(btnBox);
	newNameInput.setLayout(mainLayout);

	if (newNameInput.exec() == QDialog::Accepted)
	{
		const QString newName = nameInput->text().trimmed();
		if (newName.isEmpty())
		{
			QMessageBox::warning(this, QStringLiteral("不合法的姓名"), QStringLiteral("姓名不能为空"));
		}
		else
		{
			PlayerProfile::instance().info().setUserName(newName);
			userName->setText(newName);
			//改名后立刻存档，避免异常退出丢名字
			PlayerProfile::instance().save();
		}
	}
}

void PlayerScreen::goBag()
{
	ScreenRouter::instance().push(ScreenId::Bag);
}

void PlayerScreen::goShopping()
{
	ScreenRouter::instance().push(ScreenId::Shop);
}
