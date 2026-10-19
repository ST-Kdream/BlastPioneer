#include "GameCanvas.h"
#include "GameLoop.h"
#include "FigManager.h"

#include <QPainter>
#include <QTimer>
#include <QResizeEvent>
#include <QShowEvent>
#include <QPolygonF>
#include <QtMath>

//渲染刷新间隔（毫秒）。逻辑固定 60Hz，这里只是"多久重绘一次"：
//取 16ms 时在 60Hz 屏上刚好一行一帧；高刷屏上会重绘更多次，
//每次都用最新的 alpha 插值，所以动作更顺滑
static const int kFrameIntervalMs = 16;

GameCanvas::GameCanvas(QWidget* parent) : QWidget(parent)
{
	setAttribute(Qt::WA_OpaquePaintEvent, true);
	setMinimumSize(320, 240);

	//本控件不处理键盘（按键统一由 AppHost 的事件过滤器分发），
	//所以不抢焦点，避免出现"点一下画面按键才生效"这种手感问题
	setFocusPolicy(Qt::NoFocus);

	m_frameTimer = new QTimer(this);
	connect(m_frameTimer, &QTimer::timeout, this, &GameCanvas::onFrameTick);
	m_frameTimer->start(kFrameIntervalMs);
}

GameCanvas::~GameCanvas()
{
	if (m_frameTimer) { m_frameTimer->stop(); }
}

void GameCanvas::setSnapshotSource(bp::IGameSnapshotSource* source)
{
	m_source = source;
	update();
}

void GameCanvas::setInterpolationSource(bp::IInterpolationSource* source)
{
	m_loop = source;
}

//当前快照：没有来源时返回一个空快照，于是 paintEvent 不必到处判空
//（空快照画出来就是一张空棋盘，比崩溃或黑屏好得多）
const bp::GameSnapshot& GameCanvas::snapshot() const
{
	static const bp::GameSnapshot s_empty;
	return m_source ? m_source->snapshot() : s_empty;
}

void GameCanvas::setShowGrid(bool show)
{
	m_showGrid = show;
	update();
}

double GameCanvas::interpolationAlpha() const
{
	return m_loop ? m_loop->alpha() : 0.0;
}

//多人配色：和玩家编号绑定，四色循环
QColor GameCanvas::playerColor(int playerId)
{
	switch (playerId % 4)
	{
	case 0:  return QColor(52, 152, 219);   //蓝
	case 1:  return QColor(231, 76, 60);    //红
	case 2:  return QColor(46, 204, 113);   //绿
	case 3:  return QColor(241, 196, 15);   //黄
	default: return QColor(155, 89, 182);   //紫
	}
}

//是否具备"用图片画"的条件
//总开关关掉、或者关键图片没加载成功（例如玩家把 Assets/picture 删了），
//就整体退回矢量画法 —— 宁可画得朴素，也不要满屏占位图
bool GameCanvas::useImages() const
{
	if (!m_showImages) { return false; }
	return FigManager::instance().ready(Fig::Player) && FigManager::instance().ready(Fig::Bomb);
}

// ---------- 度量与换算 ----------

//按控件尺寸算出"一格多少像素"与居中偏移
//注意：这里不用"行数列数各除一次取较小值"以外的任何假设，
//棋盘留边导致的非正方像素会由 cellSize 统一处理
void GameCanvas::updateMetrics()
{
	const double w = width();
	const double h = height();

	if (w < 2.0 || h < 2.0)
	{
		m_cellSize = 0.0;
		m_offsetX = 0.0;
		m_offsetY = 0.0;
		return;
	}

	m_cellSize = qMin(w / bp::kCols, h / bp::kRows);

	//棋盘整体尺寸（像素）与居中偏移
	const double boardW = m_cellSize * bp::kCols;
	const double boardH = m_cellSize * bp::kRows;
	m_offsetX = (w - boardW) / 2.0;
	m_offsetY = (h - boardH) / 2.0;
}

//把"格子 + 格内偏移"换算成控件内的像素坐标
QPointF GameCanvas::cellToPixel(const bp::GridPosF& pos) const
{
	return QPointF(m_offsetX + pos.col() * m_cellSize,
				   m_offsetY + pos.row() * m_cellSize);
}

void GameCanvas::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	updateMetrics();
	update();
}

void GameCanvas::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);
	updateMetrics();
}

void GameCanvas::onFrameTick()
{
	//闪烁计时按渲染帧递增（与逻辑帧无关，只影响"闪"的观感）
	++m_blinkTick;

	//只有真正可见时才重绘，避免隐藏界面里空转耗电
	if (isVisible()) { update(); }
}

// ---------- 绘制 ----------

void GameCanvas::paintEvent(QPaintEvent* event)
{
	Q_UNUSED(event);

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);

	//背景（棋盘之外的留边区域）
	painter.fillRect(rect(), QColor(28, 30, 34));

	//尺寸不可用时只画背景（避免用 0 像素格去画，导致所有实体挤在角落）
	if (m_cellSize <= 0.0)
	{
		updateMetrics();
		if (m_cellSize <= 0.0) { return; }
	}

	if (!m_source)
	{
		//没有数据来源时给个提示，方便排查"忘了 setSnapshotSource"
		painter.setPen(Qt::white);
		painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("GameCanvas: no snapshot source"));
		return;
	}

	paintTiles(painter);
	paintDrops(painter);
	paintBombs(painter);
	paintEnemies(painter);
	paintPlayer(painter);

	//暂停/结束遮罩（阶段也来自快照，不从引擎对象上问）
	const bp::Phase phase = snapshot().phase;
	if (phase == bp::Phase::Paused || phase == bp::Phase::Victory || phase == bp::Phase::Defeat)
	{
		QColor shade(0, 0, 0, 120);
		painter.fillRect(rect(), shade);

		QString text;
		if (phase == bp::Phase::Paused) { text = QStringLiteral("已暂停"); }
		else if (phase == bp::Phase::Victory) { text = QStringLiteral("关卡通过"); }
		else { text = QStringLiteral("失败"); }

		painter.setPen(Qt::white);
		QFont font = painter.font();
		font.setPointSizeF(qMax(12.0, m_cellSize * 0.45));
		font.setBold(true);
		painter.setFont(font);
		painter.drawText(rect(), Qt::AlignCenter, text);
	}
}

void GameCanvas::paintTiles(QPainter& painter) const
{
	const int rows = bp::kRows;
	const int cols = bp::kCols;
	const bp::GameSnapshot& snap = snapshot();

	for (int r = 0; r < rows; ++r)
	{
		for (int c = 0; c < cols; ++c)
		{
			//地形取自快照，不再去问棋盘对象（视图只读快照）
			const bp::TileType tile = snap.tileAt(bp::GridPos(r, c));

			QColor fill;
			switch (tile)
			{
			case bp::TileType::Wall:      fill = QColor(70, 72, 78);  break;   //外墙：深灰
			case bp::TileType::Brick:     fill = QColor(150, 120, 90); break;  //砖块：土黄
			case bp::TileType::Explosion: fill = QColor(255, 90, 40);  break;  //爆炸痕：橙红
			default:                      fill = QColor(46, 48, 54);   break;  //空地：近黑
			}

			const QRectF cellRect(m_offsetX + c * m_cellSize,
								  m_offsetY + r * m_cellSize,
								  m_cellSize, m_cellSize);
			painter.fillRect(cellRect, fill);

			if (m_showGrid)
			{
				painter.setPen(QColor(255, 255, 255, 24));
				painter.drawRect(cellRect);
			}
		}
	}
}

void GameCanvas::paintDrops(QPainter& painter) const
{
	const bool withImages = useImages();
	const double size = m_cellSize * 0.62;
	const double half = size / 2.0;

	//矢量兜底：黄钻石（不需要图片资源，保证"看得见"）
	const double diamondSize = m_cellSize * 0.42;
	const double diamondHalf = diamondSize / 2.0;

	for (const bp::DropItem& drop : snapshot().drops)
	{
		const QPointF center = cellToPixel(drop.pos);

		//掉落物图标：数据来自 itemInfo.json 的 iconPath（按名字缓存，不会每帧读盘）
		if (withImages && FigManager::instance().itemIconReady(drop.name))
		{
			const QPixmap icon = FigManager::instance().itemIcon(drop.name);
			painter.drawPixmap(QRectF(center.x() - half, center.y() - half, size, size),
							   icon, QRectF(icon.rect()));
			continue;
		}

		QPolygonF diamond;
		diamond << QPointF(center.x(), center.y() - diamondHalf)
				<< QPointF(center.x() + diamondHalf, center.y())
				<< QPointF(center.x(), center.y() + diamondHalf)
				<< QPointF(center.x() - diamondHalf, center.y());

		painter.setPen(QPen(QColor(255, 235, 120), qMax(1.0, m_cellSize * 0.04)));
		painter.setBrush(QColor(255, 235, 120, 200));
		painter.drawPolygon(diamond);
	}
}

void GameCanvas::paintBombs(QPainter& painter) const
{
	const bool withImages = useImages();
	const double radius = m_cellSize * (withImages ? 0.34 : 0.32);
	const double imgSize = m_cellSize * 0.62;
	const bp::GameSnapshot& snap = snapshot();

	//炸弹图片（bomb.png）：所有炸弹共用同一张，循环外取一次
	const QPixmap bombImg = withImages ? FigManager::instance().fig(Fig::Bomb) : QPixmap();

	const bool blinkOn = ((m_blinkTick / 8) % 2) == 0;

	//第一遍：即将爆炸的警戒闪格（画在底下，不遮挡后面的炸弹与实体）
	for (const bp::GameSnapshot::BombView& bomb : snap.bombViews)
	{
		//要闪哪些格由引擎算好放进快照（引擎只在最后 warningSeconds 内填这个列表），
		//视图不推算、也不去查棋盘 —— "闪的地方就是会炸的地方"因此永远成立
		if (bomb.warningCells.isEmpty()) { continue; }
		if (!blinkOn) { continue; }

		for (const bp::GridPos& cell : bomb.warningCells)
		{
			const QRectF cellRect(m_offsetX + cell.col * m_cellSize,
								  m_offsetY + cell.row * m_cellSize,
								  m_cellSize, m_cellSize);
			painter.fillRect(cellRect, QColor(255, 80, 60, 70));
			painter.setPen(QPen(QColor(255, 140, 120, 180), qMax(1.0, m_cellSize * 0.05)));
			painter.drawRect(cellRect.adjusted(1, 1, -1, -1));
		}
	}

	//第二遍：炸弹本体 + 剩余秒数
	for (const bp::GameSnapshot::BombView& bomb : snap.bombViews)
	{
		const QPointF center = cellToPixel(bomb.pos);

		//按归属区分颜色：玩家炸弹=青色（与玩家同色系），敌人炸弹=红色（与敌人同色系）
		//多人模式按放置者编号上色，一眼能看出"这颗是谁的"
		const QColor base = (bomb.owner == bp::BombOwner::Player)
			? (bomb.ownerId > bp::kPlayerOwnerId ? playerColor(bomb.ownerId) : QColor(80, 220, 255))
			: QColor(255, 90, 90);

		//炸弹本体：有图片就用图片，没有就用圆
		if (!bombImg.isNull())
		{
			painter.drawPixmap(QRectF(center.x() - imgSize / 2.0, center.y() - imgSize / 2.0, imgSize, imgSize),
							   bombImg, QRectF(bombImg.rect()));
		}
		else
		{
			//引信进度：progress 从 1（刚放下）走到 0（即将爆炸）
			painter.setBrush(base.darker(160));
			painter.setPen(QPen(base, qMax(1.5, m_cellSize * 0.06)));
			painter.drawEllipse(center, radius, radius);
		}

		//剩余圆弧：越接近爆炸越短；最后 0.5 秒随闪格一起闪，强化"要炸了"的感觉
		const double remaining = bomb.remainingSeconds;
		if (bomb.warningCells.isEmpty() || blinkOn)
		{
			const int span = static_cast<int>(360.0 * bomb.progress);
			painter.setBrush(Qt::NoBrush);
			painter.setPen(QPen(base, qMax(1.5, m_cellSize * 0.06)));
			painter.drawArc(QRectF(center.x() - radius, center.y() - radius, radius * 2, radius * 2),
							90 * 16, -span * 16);
		}

		//倒计时数字：开局就显示（这是以前缺的东西）
		QString text = remaining > 0.05
			? QString::number(remaining, 'f', 1)
			: QStringLiteral("!");
		QFont font = painter.font();
		font.setPointSizeF(qMax(7.0, m_cellSize * 0.22));
		font.setBold(true);
		painter.setFont(font);

		//数字画在炸弹下方一点点，避免和圆心糊在一起
		const QRectF textRect(center.x() - m_cellSize * 0.5,
							  center.y() + radius * 0.4,
							  m_cellSize, m_cellSize * 0.4);
		painter.setPen(QColor(255, 255, 255, 230));
		painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop, text);

		//调试倒计时（勾选"显示网格"时一起显示，便于确认引信与逻辑步对得上）
		if (m_showGrid)
		{
			font.setPointSizeF(qMax(6.0, m_cellSize * 0.16));
			font.setBold(false);
			painter.setFont(font);
			painter.setPen(QColor(200, 200, 200, 180));
			painter.drawText(QRectF(center.x() - m_cellSize * 0.5, center.y() - m_cellSize * 0.5,
									m_cellSize, m_cellSize * 0.3),
							 Qt::AlignHCenter | Qt::AlignBottom,
							 QStringLiteral("引信%1").arg(remaining, 0, 'f', 1));
		}
	}
}

void GameCanvas::paintEnemies(QPainter& painter) const
{
	const double radius = m_cellSize * 0.3;
	const double imgSize = m_cellSize * 0.62;
	const bp::GameSnapshot& snap = snapshot();
	const QList<bp::GridPosF>& prev = snap.previousEnemyPositions;
	const double alpha = interpolationAlpha();

	//敌人图片（enemy.png）；没有就用红圆
	const QPixmap enemyImg = useImages() ? FigManager::instance().fig(Fig::Enemy) : QPixmap();

	for (int i = 0; i < snap.enemies.size(); ++i)
	{
		const bp::Enemy& enemy = snap.enemies[i];

		//插值：上一逻辑帧位置 -> 当前逻辑帧位置
		bp::GridPosF draw = enemy.pos;
		if (i < prev.size())
		{
			const double row = prev[i].row() + (enemy.pos.row() - prev[i].row()) * alpha;
			const double col = prev[i].col() + (enemy.pos.col() - prev[i].col()) * alpha;
			draw = bp::posFromRowCol(row, col);
		}

		const QPointF center = cellToPixel(draw);

		//死亡动画：dying 期间缩小并淡出
		double scale = 1.0;
		int opacity = 255;
		if (enemy.dying)
		{
			scale = 0.6;
			opacity = 120;
		}

		if (!enemyImg.isNull())
		{
			const double size = imgSize * scale;
			painter.setOpacity(static_cast<double>(opacity) / 255.0);
			painter.drawPixmap(QRectF(center.x() - size / 2.0, center.y() - size / 2.0, size, size),
							   enemyImg, QRectF(enemyImg.rect()));
			painter.setOpacity(1.0);
		}
		else
		{
			painter.setPen(Qt::NoPen);
			painter.setBrush(QColor(255, 100, 100, opacity));
			painter.drawEllipse(center, radius * scale, radius * scale);
		}

		if (m_showBars && !enemy.dying && enemy.maxHp > 0)
		{
			const double barW = m_cellSize * 0.6;
			const double barH = qMax(2.0, m_cellSize * 0.08);
			const double barX = center.x() - barW / 2.0;
			const double barY = center.y() - m_cellSize * 0.42;

			painter.setBrush(QColor(30, 30, 30, 200));
			painter.drawRect(QRectF(barX, barY, barW, barH));

			const double filled = barW * static_cast<double>(qMax(0, enemy.hp)) / enemy.maxHp;
			painter.setBrush(QColor(240, 80, 80));
			painter.drawRect(QRectF(barX, barY, filled, barH));
		}
	}
}

void GameCanvas::paintPlayer(QPainter& painter) const
{
	const bp::GameSnapshot& snap = snapshot();

	//多人模式：快照里带了玩家列表，逐个画（名字 / 血条 / 本机高亮）
	//单人模式 players 为空，走下面那套"单数字段"，画面与阶段三完全一致
	if (!snap.players.isEmpty())
	{
		paintMultiPlayers(painter);
		return;
	}

	const double radius = m_cellSize * 0.3;

	//玩家位置也插值（但单个实体的"上一帧位置"由是否移动决定，
	//这里用快照位置直接画：玩家是操作对象，跟随输入即时响应，不需要插值延迟）
	const QPointF center = cellToPixel(snap.playerPos);

	//电子幽灵态：闪烁提示
	QColor color = snap.playerGhost ? QColor(160, 220, 255) : QColor(80, 200, 255);

	//玩家图片（player.png）
	const QPixmap playerImg = useImages() ? FigManager::instance().fig(Fig::Player) : QPixmap();
	if (!playerImg.isNull())
	{
		const double size = m_cellSize * 0.62;
		if (snap.playerGhost) { painter.setOpacity(0.6); }
		painter.drawPixmap(QRectF(center.x() - size / 2.0, center.y() - size / 2.0, size, size),
						   playerImg, QRectF(playerImg.rect()));
		painter.setOpacity(1.0);
	}
	else
	{
		painter.setPen(Qt::NoPen);
		painter.setBrush(color);
		painter.drawEllipse(center, radius, radius);
	}

	if (m_showBars && snap.playerMaxLives > 0)
	{
		const double barW = m_cellSize * 0.6;
		const double barH = qMax(2.0, m_cellSize * 0.1);
		const double barX = center.x() - barW / 2.0;
		const double barY = center.y() - m_cellSize * 0.44;

		painter.setBrush(QColor(30, 30, 30, 200));
		painter.drawRect(QRectF(barX, barY, barW, barH));

		const double ratio = qBound(0.0,
			static_cast<double>(snap.playerLives) / static_cast<double>(snap.playerMaxLives), 1.0);
		painter.setBrush(QColor(90, 230, 120));
		painter.drawRect(QRectF(barX, barY, barW * ratio, barH));
	}
}

//多人：把快照里的玩家列表逐个画出来
//这一段和单人那段"看着像"，但故意不合并：
//   单人画的是"我"（青色、无名字），多人画的是"我们"（按人配色 + 名字 + 本机描边），
//   合并只会变成一堆 if，而分开写两边都只有二十来行
void GameCanvas::paintMultiPlayers(QPainter& painter) const
{
	const bp::GameSnapshot& snap = snapshot();
	const double radius = m_cellSize * 0.3;
	const double imgSize = m_cellSize * 0.62;

	const QPixmap playerImg = useImages() ? FigManager::instance().fig(Fig::Player) : QPixmap();

	for (const bp::GameSnapshot::PlayerView& player : snap.players)
	{
		if (!player.alive) { continue; }

		const QPointF center = cellToPixel(player.pos);
		const QColor color = playerColor(player.id);

		if (!playerImg.isNull())
		{
			if (player.ghost) { painter.setOpacity(0.6); }
			painter.drawPixmap(QRectF(center.x() - imgSize / 2.0, center.y() - imgSize / 2.0, imgSize, imgSize),
							   playerImg, QRectF(playerImg.rect()));
			painter.setOpacity(1.0);
		}
		else
		{
			painter.setPen(Qt::NoPen);
			painter.setBrush(player.ghost ? color.lighter(150) : color);
			painter.drawEllipse(center, radius, radius);
		}

		//本机玩家：加一圈白描边。人多的时候"哪个是我"必须是零成本的
		if (player.local)
		{
			painter.setBrush(Qt::NoBrush);
			painter.setPen(QPen(QColor(255, 255, 255, 220), qMax(1.5, m_cellSize * 0.06)));
			painter.drawEllipse(center, radius * 1.2, radius * 1.2);
			painter.setPen(Qt::NoPen);
		}

		//血条
		if (m_showBars && player.maxLives > 0)
		{
			const double barW = m_cellSize * 0.6;
			const double barH = qMax(2.0, m_cellSize * 0.1);
			const double barX = center.x() - barW / 2.0;
			const double barY = center.y() - m_cellSize * 0.44;

			painter.setBrush(QColor(30, 30, 30, 200));
			painter.drawRect(QRectF(barX, barY, barW, barH));

			const double ratio = qBound(0.0,
				static_cast<double>(player.lives) / static_cast<double>(player.maxLives), 1.0);
			painter.setBrush(color);
			painter.drawRect(QRectF(barX, barY, barW * ratio, barH));
		}

		//名字
		QFont font = painter.font();
		font.setPointSizeF(qMax(7.0, m_cellSize * 0.2));
		font.setBold(player.local);
		painter.setFont(font);
		painter.setPen(QColor(255, 255, 255, 230));
		painter.drawText(QRectF(center.x() - m_cellSize, center.y() + m_cellSize * 0.28,
								m_cellSize * 2.0, m_cellSize * 0.4),
						 Qt::AlignHCenter | Qt::AlignTop, player.name);
		font.setBold(false);
		painter.setFont(font);
	}
}
