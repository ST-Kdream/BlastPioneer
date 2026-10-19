#pragma once
#include "ScaffoldScreen.h"

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

// ============================================================
// 模组界面（从「设置」里搬出来独立成页）
//
// 为什么独立：模组不是"设置项"，它是一个功能（看列表、开关、看详情与问题、重扫目录）。
// 塞在设置页最下面时，玩家得先想到"去设置里找"；而主页给一个显眼的入口更符合直觉。
//
// 这里同时承担"让玩家看见程序状态"的职责：
//   - 每个模组显示自己需要哪一版接口、用到哪些能力、覆盖了几处；
//   - 被拒绝/被禁用的显示成灰色，悬停或选中能看到原因；
//   - 汇总行显示本构建接受哪些接口版本。
// ============================================================
class ModsScreen : public ScaffoldScreen
{
	Q_OBJECT

public:
	explicit ModsScreen(QWidget* parent = nullptr);

	QString screenId() const override { return "Mods"; }
	QString displayTitle() const override { return "模组"; }
	bool persistent() const override { return true; }

	void onEnter(const QVariantMap& args) override;

private:
	//重建列表（勾选状态 + 状态说明 + 汇总）
	void refreshMods();
	//根据当前选中项刷新详情
	void refreshModDetail();
	//把界面外观（令牌/样式表）也重算一遍：模组可能改了配色
	void reapplyTheme();

	QLabel* m_summary = nullptr;
	QListWidget* m_list = nullptr;
	QLabel* m_detail = nullptr;
	//原生模组的开关要重启才生效，这条提示只在玩家真的动过原生模组之后出现
	QLabel* m_hint = nullptr;
	//重建列表时会触发 itemChanged，用它挡住递归
	bool m_syncing = false;

private slots:
	void onItemChanged(QListWidgetItem* item);
	void onSelectionChanged();
	void onReloadClicked();
	void onOpenFolderClicked();
};
