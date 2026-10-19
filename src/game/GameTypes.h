#pragma once
#include <QtGlobal>
#include <QString>
#include <cmath>

// ============================================================
// 引擎层基础类型（game/ 目录只允许依赖 QtCore，绝不出现 QtWidgets）
// 坐标约定：全程使用"逻辑格子坐标"，引擎内部不出现像素
// ============================================================

//地图尺寸（编译期常量，棋盘直接用它开数组）
namespace bp
{
	constexpr int kRows = 11;
	constexpr int kCols = 13;

	//单人模式下"玩家"这个归属者的编号。
	//多人模式用的是主机分配的真实玩家 id（1、2、3……），所以两者天然不冲突
	constexpr int kPlayerOwnerId = 1;
}

namespace bp
{

//格子坐标（具名字段：彻底避免旧代码里"用 QPoint 兼职行列"导致的行列写反）
struct GridPos
{
	int row = 0;
	int col = 0;

	GridPos() = default;
	GridPos(int r, int c) : row(r), col(c) {}

	bool operator==(const GridPos& other) const { return row == other.row && col == other.col; }
	bool operator!=(const GridPos& other) const { return !(*this == other); }
};

//带"格内偏移"的位置：cell 是所在格子，offset 是格内 0~1 的比例偏移
//用于"自由移动"（非格子对齐）的手感，同时保持引擎不接触像素
struct GridPosF
{
	GridPos cell;
	double offsetX = 0.0;   // 0~1，相对格子宽
	double offsetY = 0.0;   // 0~1，相对格子高

	GridPosF() = default;

	//以格子中心为基准建立
	static GridPosF centered(const GridPos& p)
	{
		GridPosF out;
		out.cell = p;
		out.offsetX = 0.5;
		out.offsetY = 0.5;
		return out;
	}

	//行列的连续值：row = cell.row + offsetY，col = cell.col + offsetX
	double row() const { return cell.row + offsetY; }
	double col() const { return cell.col + offsetX; }
};

//把连续的行列值转换成"带偏移的位置"（负值也能正确落到格子内）
inline GridPosF posFromRowCol(double row, double col)
{
	GridPosF out;
	out.cell.row = static_cast<int>(std::floor(row));
	out.cell.col = static_cast<int>(std::floor(col));
	out.offsetY = row - out.cell.row;
	out.offsetX = col - out.cell.col;
	return out;
}

//地图元素类型
enum class TileType
{
	Empty,
	Wall,       //不可破坏的外墙（边框）
	Brick,      //可破坏的砖块
	Player,     //玩家所在格标记
	Enemy,      //敌人所在格标记
	Bomb,       //炸弹所在格标记
	Explosion   //爆炸痕（带存续计时，到点恢复为空地）
};

//实体类别（用于"写入/清除格子标记"时的白名单判断）
enum class EntityKind
{
	Player,
	Enemy,
	Bomb
};

//炸弹归属
enum class BombOwner
{
	Player,
	Enemy
};

//炸弹
struct Bomb
{
	GridPosF pos;
	double   fuse = 1.5;        //剩余引信秒数
	double   fuseTotal = 1.5;   //总引信时长（归一化进度用：fuse / fuseTotal）
	int      range = 2;
	BombOwner owner = BombOwner::Player;
	//放置者编号：多人模式用来区分"这是谁的炸弹"（伤害判定、放弹上限、配色）
	//单人模式固定为 1，与 GameEvent::ownerId 的约定一致
	int      ownerId = 0;
};

//敌人
struct Enemy
{
	GridPosF pos;
	int    hp = 3;
	int    maxHp = 3;
	double dirX = 0.0;          //方向（单位向量风格：只取 -1/0/1 之一）
	double dirY = 0.0;
	double bombCooldown = 0.0;  //放弹冷却
	bool   dying = false;       //延迟销毁：标记后由视图播动画，再真正移除
	double deathTimer = 0.0;
	//0.1.2（模组 L3）追加：同一局内稳定的实体 id。
	//为什么需要它：模组的写请求要能指名道姓地"伤害/移除某个敌人"，
	//而"第几个"在一帧之内可靠、跨帧就不可靠了（容器会重排）
	int    id = 0;
};

//掉落物
struct DropItem
{
	GridPosF pos;
	QString  name;
	double   lifetime = 0.0;    //已存在时长（可用于闪烁/自动消失）
	bool     dying = false;
};

//输入状态（由视图从按键转换而来）
struct InputState
{
	bool up = false;
	bool down = false;
	bool left = false;
	bool right = false;
	bool placeBomb = false;
};

//游戏阶段
enum class Phase
{
	Ready,
	Running,
	Paused,
	Victory,
	Defeat
};

//帧上下文
struct FrameContext
{
	double frameTime = 1.0 / 60.0;   //本逻辑步的时长（固定步长）
	int    stepIndex = 0;            //第几个逻辑步（自本局开始计数）
};

//引擎每帧向外抛的事件（表现层用它触发粒子/音效/飘字）
enum class GameEventType
{
	BombPlaced,
	BombExploded,
	EnemyDamaged,
	EnemyDied,
	PlayerDamaged,
	DropSpawned,
	DropCollected,
	ItemUsed
};

struct GameEvent
{
	GameEventType type = GameEventType::BombPlaced;
	GridPos  cell;                  //事件发生格
	int      ownerId = 0;           //炸弹/伤害来源（单人固定为 1）
	QString  text;                  //掉落物名 / 道具名等
	int      amount = 0;            //伤害量 / 数量
};

} // namespace bp
