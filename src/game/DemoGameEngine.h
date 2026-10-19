#pragma once
#include "GameEngine.h"

namespace bp
{

// ============================================================
// 演示模式引擎：故意做到最小，用来证明一件事 ——
//   "加一个新游戏模式" = 继承 GameEngine + 实现三个函数
// 不需要碰窗口、绘制、输入分发、帧协调、快照、事件队列
// （那些都在基类与视图层里，直接复用）
//
// 玩法：一个方块在空棋盘上走（WASD），空格放炸弹，炸弹 1.5 秒后爆炸
// 它存在的意义是当"活文档"：以后想加新模式，照着这个文件抄骨架即可
// ============================================================
class DemoGameEngine : public GameEngine
{
public:
	explicit DemoGameEngine(const GameConfig& config, QObject* parent = nullptr);

	//本局已放下的炸弹数（演示用统计）
	int bombsPlaced() const { return m_bombsPlaced; }
	//累计引爆次数（验证事件/爆炸流程真的跑了）
	int explosions() const { return m_explosions; }

protected:
	void onReset() override;
	void onLevelInit() override;
	void advance(const FrameContext& ctx) override;

private:
	void handleInput(const FrameContext& ctx);
	void updateBombs(const FrameContext& ctx);

	int m_bombsPlaced = 0;
	int m_explosions = 0;
};

} // namespace bp
