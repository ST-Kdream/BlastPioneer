#pragma once
#include "GameEngine.h"
#include "GameTypes.h"

#include <QWidget>
#include <QColor>
#include <QPointF>

class QTimer;
class QPainter;

namespace bp
{
	//插值比例的来源（GameLoop 实现它）。这里只需要指针，前向声明足够，
	//于是"画布不依赖帧循环的实现"这件事在 include 层面也成立
	class IInterpolationSource;
}

// ============================================================
// 游戏画面（视图层）
//
// 职责边界（阶段三定下、这里进一步收紧）：
//   本类只做三件事：① 把引擎快照画出来；② 像素↔格子换算；③ 按设置决定画法
//   绝不做任何规则计算（移动、碰撞、爆炸判定都在引擎里）
//   也绝不碰输入与引擎状态 —— 它只持有两个"只读接口"（见下面的注释）
//
// 为什么像素换算必须留在视图：
//   引擎只用逻辑格子坐标，所以"格子换算错了"这类 bug 不可能影响规则，
//   也正因为如此，引擎可以脱离窗口跑单测（tests/ 里就是这么测的）
//
// 渲染平滑（插值）：
//   逻辑固定 60Hz，显示可能是 144Hz。用 alpha() 在
//   "上一逻辑帧位置 → 当前逻辑帧位置"之间插值，移动才是连续的；
//   不插值的话会看到实体按格子跳动
// ============================================================
class GameCanvas : public QWidget
{
	Q_OBJECT

public:
	explicit GameCanvas(QWidget* parent = nullptr);
	~GameCanvas();

	//绑定数据来源（都不持有所有权，生命周期由界面管）
	//
	// 注意这里收窄成了两个"最小接口"而不是 GameEngine* / GameLoop*：
	//   ① 快照来源：视图要画的一切都从 snapshot() 里取（棋盘、实体、预警格、阶段）
	//   ② 插值来源：只有一个 alpha()，用来把移动画顺
	// 于是"视图顺手改游戏状态"在类型层面就不可能发生 —— 它拿不到那些方法。
	// 输入也不再经由画布转发：界面自己 m_engine->setInput(...)，画布只管画。
	void setSnapshotSource(bp::IGameSnapshotSource* source);
	void setInterpolationSource(bp::IInterpolationSource* source);

	//当前快照（没有来源时返回一个空的静态快照，调用方不必判空）
	const bp::GameSnapshot& snapshot() const;

	//是否显示网格（跟设置里那一项一致）
	void setShowGrid(bool show);
	bool showGrid() const { return m_showGrid; }

	//是否使用真实图片（player.png / enemy.png / bomb.png / 道具图标）
	//打开时仍然会自动判断"这张图到底有没有"，缺图就退回矢量画法，
	//所以不会出现"图片缺失导致满屏灰占位图"
	void setShowImages(bool show) { m_showImages = show; }
	bool showImages() const { return m_showImages; }

	//多人配色：按玩家编号取一个稳定颜色
	//颜色属于表现层，所以放在视图里（引擎只管编号，不管颜色）
	static QColor playerColor(int playerId);

	//是否显示实体血条（调试/演示可关）
	void setShowBars(bool show) { m_showBars = show; }

	//把当前渲染插值比例取出来（给测试/调试显示用）
	double interpolationAlpha() const;

protected:
	//把"格内偏移"换算成像素位置的几个小工具
	QPointF cellToPixel(const bp::GridPosF& pos) const;
	double  cellSize() const { return m_cellSize; }

	void paintEvent(QPaintEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
	void showEvent(QShowEvent* event) override;

private slots:
	//定时重绘（跟逻辑循环分开：这样显示帧率与逻辑步长无关）
	void onFrameTick();

private:
	void updateMetrics();

	//绘制各部分
	void paintTiles(QPainter& painter) const;
	void paintDrops(QPainter& painter) const;
	void paintBombs(QPainter& painter) const;
	void paintEnemies(QPainter& painter) const;
	void paintPlayer(QPainter& painter) const;
	//多人：把快照里的玩家列表逐个画出来（含名字、血条、本机高亮）
	void paintMultiPlayers(QPainter& painter) const;
	//当前是否具备"用图片画"的条件（总开关 + 关键图片存在）
	bool useImages() const;

	bp::IGameSnapshotSource* m_source = nullptr;
	bp::IInterpolationSource* m_loop = nullptr;

	QTimer* m_frameTimer = nullptr;

	//画面度量（每帧或尺寸变化时重算）
	double m_cellSize = 0.0;    //一格多少像素
	double m_offsetX = 0.0;     //棋盘左上角在控件里的像素偏移（居中留边）
	double m_offsetY = 0.0;

	//闪烁计时（爆炸警戒闪格用）：按渲染帧递增
	int m_blinkTick = 0;

	bool m_showGrid = true;
	bool m_showBars = true;
	bool m_showImages = true;
};
