#pragma once
#include "ScaffoldScreen.h"

#include <QList>

class QPushButton;

// ============================================================
// 模式选择界面
//
// 为什么单独做一页：主页只留一个「开始游戏」，点进来才是模式列表。
// 这样以后加模式（比如 mod 带来的新模式）只需要往这一页加一张卡片，
// 主页不用动；玩家也不必在主页面对一排"单人/多人/…"猜哪个是新东西。
//
// 目前有两种内置模式：单人闯关、联机对战。
// 「更多模式」区放**模组注册的模式**：模组在 init 里调 host->register_mode，
// 它就会在这里变成一张卡片，点进去直接开局（关卡流程由模组自己设计）。
// ============================================================
class ModeSelectScreen : public ScaffoldScreen
{
	Q_OBJECT

public:
	explicit ModeSelectScreen(QWidget* parent = nullptr);

	QString screenId() const override { return "ModeSelect"; }
	QString displayTitle() const override { return "选择模式"; }
	bool persistent() const override { return true; }

	void onEnter(const QVariantMap& args) override;

private:
	//加一张模式卡片（左侧标题 + 副说明，样式来自 #modeCard）
	QPushButton* addModeCard(const QString& title, const QString& hint, const QString& objectName);
	//加一张"更多模式"卡片（放进扩展区；模组模式专用）
	QPushButton* newCard(const QString& title, const QString& hint);
	//重建"更多模式"区（模组注册的模式）
	void refreshExtendedModes();

	QList<QPushButton*> m_cards;      //内置模式卡片
	QVBoxLayout* m_extendedLayout = nullptr;
};
