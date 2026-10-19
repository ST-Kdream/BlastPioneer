#include "DemoGameEngine.h"

namespace bp
{

//一个新模式引擎的全部"存在感"就是下面这约 50 行
DemoGameEngine::DemoGameEngine(const GameConfig& config, QObject* parent)
	: GameEngine(config, parent)
{
}

//回到初始状态：把棋盘清空，只留一圈边框墙
void DemoGameEngine::onReset()
{
	boardRef().fill(TileType::Empty);
	boardRef().buildBorderWalls();
	m_bombsPlaced = 0;
	m_explosions = 0;
}

//开局：把玩家放在棋盘中间
void DemoGameEngine::onLevelInit()
{
	playerPosRef() = GridPosF::centered(GridPos(kRows / 2, kCols / 2));
	boardRef().markEntity(playerPosRef().cell, EntityKind::Player);
}

//每个逻辑步：先处理输入，再推进炸弹
void DemoGameEngine::advance(const FrameContext& ctx)
{
	if (!isRunning()) { return; }

	handleInput(ctx);
	updateBombs(ctx);
}

//把方向键转成"格/秒"位移（逐轴判定碰撞由基类的 movePlayerBy 负责）
void DemoGameEngine::handleInput(const FrameContext& ctx)
{
	const InputState& in = input();
	const double step = config().playerSpeed * ctx.frameTime;

	double dx = 0.0;
	double dy = 0.0;
	if (in.left) { dx -= step; }
	if (in.right) { dx += step; }
	if (in.up) { dy -= step; }
	if (in.down) { dy += step; }

	movePlayerBy(dx, dy);

	//放炸弹：tryPlaceBomb 会处理"同格已有炸弹"与数量上限
	if (in.placeBomb)
	{
		if (tryPlaceBomb(playerPos().cell, BombOwner::Player))
		{
			++m_bombsPlaced;
		}
	}
}

//炸弹倒计时：引信到点就引爆，并把周围几格标成爆炸痕
void DemoGameEngine::updateBombs(const FrameContext& ctx)
{
	QList<Bomb>& list = bombsRef();

	for (int i = list.size() - 1; i >= 0; --i)
	{
		list[i].fuse -= ctx.frameTime;

		if (list[i].fuse > 0.0) { continue; }

		const Bomb exploded = list[i];
		list.removeAt(i);

		const QList<GridPos> cells = detonateBomb(exploded);
		for (const GridPos& cell : cells)
		{
			markExplosion(cell);
		}

		++m_explosions;
	}
}

} // namespace bp
