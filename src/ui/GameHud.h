#pragma once
#include "GameEngine.h"

#include <QWidget>
#include <QLabel>

// ============================================================
// 游戏内 HUD（顶部信息条）
//
// 只读展示引擎快照里的数字：关卡 / 生命 / 炸弹威力与上限 / 剩余敌人
// 特意不做任何逻辑：它改不了游戏状态，只显示
// 数据来源收窄成 IGameSnapshotSource（而不是 GameEngine*）：
// HUD 连"改状态的方法"都拿不到，于是"界面只读"不靠自觉，靠类型
// ============================================================
class GameHud : public QWidget
{
	Q_OBJECT

public:
	explicit GameHud(QWidget* parent = nullptr);

	//绑定数据来源后调用 refresh() 刷新显示
	void setSnapshotSource(bp::IGameSnapshotSource* source) { m_source = source; }

public slots:
	//从快照刷新文字
	void refresh();

private:
	bp::IGameSnapshotSource* m_source = nullptr;

	QLabel* m_levelLabel = nullptr;
	QLabel* m_livesLabel = nullptr;
	QLabel* m_bombLabel = nullptr;
	QLabel* m_hintLabel = nullptr;
};
