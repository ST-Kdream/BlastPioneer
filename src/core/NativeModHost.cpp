#include "NativeModHost.h"
#include "FileManager.h"
#include "LaunchGuard.h"
#include "ModManager.h"
#include "SettingsManager.h"
#include "bp_mod_api.h"

#include "GameEngine.h"
#include "GameTypes.h"
#include "MapGenerator.h"

#include <QLibrary>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QDateTime>
#include <cstddef>
#include <cstdlib>

// ============================================================
// 原生模组宿主实现
//
// 结构：
//   1) ModContext          —— 一个已加载模组的运行状态
//   2) 全局当前模组        —— 宿主函数表靠它知道"现在是谁在调我"
//   3) 宿主函数表实现      —— C ABI 那一侧（全部 static + BP_CALL）
//   4) 加载流程            —— 校验、entry、init
//   5) 每步分发            —— 事件 + on_tick + 记账
// ============================================================

//一个已加载模组的运行状态（头文件里只前置声明）
//**必须定义在这里**：下面的宿主函数表在匿名命名空间里就要用到它的成员
struct NativeModHost::ModContext
{
	struct Subscription
	{
		uint32_t type;
		void (BP_CALL* cb)(const BpGameEvent*, void*);
		void* user;
	};

	QString id;
	QString name;
	QString version;
	QString dllPath;
	QString apiVersion;

	QLibrary* library = nullptr;
	const BpModApi* api = nullptr;
	bool active = false;
	QString reason;

	QList<Subscription> subscriptions;
	QRandomGenerator rng;

	//记账（契约 §7.3：配额与耗时统计）
	int     commandsThisTick = 0;
	qint64  maxCallbackUs = 0;
	qint64  totalCallbackUs = 0;
	int     callbacks = 0;
	int     slowCallbacks = 0;

	ModContext() : rng(QRandomGenerator::securelySeeded()) {}
};

namespace
{

//一个模组的"当前调用"上下文：宿主函数表靠它区分调用者
NativeModHost::ModContext* g_current = nullptr;

//是否正在分发事件（契约 §8：回调里不能再触发事件 —— 订阅表正在被遍历，
//此刻往它里面追加就是在改一个正在被读的容器）
bool g_dispatchingEvents = false;

//本次回调期间有效的字符串存储（契约：跨边界的字符串只在本次调用内有效）
//用 QList<QByteArray> 而不是临时对象：返回给模组的 char* 必须活到本次回调结束
QList<QByteArray> g_stringPool;

//把 QString 放进字符串池，返回一个在本次回调内有效的 BpStr
BpStr poolString(const QString& text)
{
	g_stringPool.append(text.toUtf8());
	const QByteArray& stored = g_stringPool.last();
	BpStr out;
	out.utf8 = stored.constData();
	out.len = static_cast<int32_t>(stored.size());
	return out;
}

//把模组给的 BpStr 读成 QString（len < 0 表示按 \0 结尾读）
QString fromBpStr(const BpStr& text)
{
	if (!text.utf8) { return QString(); }
	if (text.len < 0) { return QString::fromUtf8(text.utf8); }
	return QString::fromUtf8(text.utf8, text.len);
}

//L2 实际开放的能力（has_feature 的答案；L3 的会在这里从 0 变成 1）
//
// "不静默失效"是这里最硬的一条：没实现的能力必须如实返回 0，
// 绝不"调了没反应" —— 那会让模组作者以为自己的代码生效了，问题要拖到很久以后才暴露。
struct FeatureEntry { const char* name; bool open; };
const FeatureEntry kFeatures[] = {
	{ "event.subscribe",    true  },
	{ "event.level_start",  true  },   //由宿主合成（引擎没有"关卡开始"事件）
	{ "event.level_result", true  },   //0.1.1：由宿主按阶段变化合成 LEVEL_WON / LEVEL_LOST
	{ "world.read",         true  },
	{ "world.read.bombs",   true  },   //0.1.1：炸弹的引信/威力/归属
	{ "world.read.players", true  },   //0.1.1：多人对局的玩家状态（player_state 按下标取）
	{ "world.read.meta",    true  },   //0.1.1：关卡号/阶段/逻辑步
	{ "pause.control",      true  },
	{ "item.effect",        true  },   //触发一件已有道具的效果
	{ "rng",                true  },
	// ---- 0.1.2：L3（需要引擎侧受控接口的那一批，本轮全部接上） ----
	{ "world.write.var",    true  },   //改数值（与 gameplay.json 同一张白名单）
	{ "world.write.entity", true  },   //生成/移除/伤害实体、掉落、放弹
	{ "time.scale",         true  },   //时间倍率（0 = 冻结，上限 4）
	{ "mode.register",      true  },   //注册模式（出现在「选择模式」里）
	{ "mode.hooks",         true  },   //地图生成 / 敌人 AI / 胜负三个钩子
	{ "map.generator",      true  },
	{ "ai.controller",      true  },
	{ "win.condition",      true  },
};

bp::GameEngine* engine()
{
	return NativeModHost::instance().engineForMods();
}

// ---------- 宿主函数表：基础 ----------

int32_t BP_CALL hp_has_feature(BpStr name)
{
	const QString feature = fromBpStr(name);
	for (const FeatureEntry& entry : kFeatures)
	{
		if (feature == QLatin1String(entry.name)) { return entry.open ? 1 : 0; }
	}
	//不认识的能力名：返回 0（而不是报错）—— 契约里就是这么约定的
	return 0;
}

uint32_t BP_CALL hp_abi_minor(void)
{
	return 1;   //0.1.0 的次版本号
}

void BP_CALL hp_log(int32_t level, BpStr message)
{
	NativeModHost::instance().logForCurrentMod(level, fromBpStr(message));
}

void* BP_CALL hp_alloc(size_t bytes) { return std::malloc(bytes); }
void  BP_CALL hp_free(void* p) { std::free(p); }

// ---------- 宿主函数表：只读查询 ----------

int32_t BP_CALL hp_board_width(void) { return bp::kCols; }
int32_t BP_CALL hp_board_height(void) { return bp::kRows; }

int32_t BP_CALL hp_tile_at(int32_t row, int32_t col)
{
	bp::GameEngine* game = engine();
	if (!game) { return BP_TILE_WALL; }

	const bp::GameSnapshot& snap = game->snapshot();
	if (row < 0 || row >= bp::kRows || col < 0 || col >= bp::kCols) { return BP_TILE_WALL; }

	switch (snap.tiles[row][col])
	{
	case bp::TileType::Wall:      return BP_TILE_WALL;
	case bp::TileType::Brick:     return BP_TILE_BRICK;
	case bp::TileType::Explosion: return BP_TILE_EXPLOSION;   //0.1.1：爆炸痕不再伪装成空地
	default:                      return BP_TILE_EMPTY;
	}
}

int32_t BP_CALL hp_bomb_count_at(int32_t row, int32_t col)
{
	bp::GameEngine* game = engine();
	if (!game) { return 0; }

	int count = 0;
	for (const bp::Bomb& bomb : game->snapshot().bombs)
	{
		if (bomb.pos.cell.row == row && bomb.pos.cell.col == col) { ++count; }
	}
	return count;
}

int32_t BP_CALL hp_enemies_snapshot(BpEnemyInfo* out, int32_t capacity)
{
	bp::GameEngine* game = engine();
	if (!game) { return 0; }

	const QList<bp::Enemy>& enemies = game->snapshot().enemies;
	if (!out || capacity <= 0) { return enemies.size(); }

	const int count = qMin<int>(capacity, enemies.size());
	for (int i = 0; i < count; ++i)
	{
		const bp::Enemy& enemy = enemies.at(i);
		out[i].size = sizeof(BpEnemyInfo);
		out[i].entityId = i;                     //引擎没有稳定 id：用下标（同一帧内可靠）
		out[i].row = enemy.pos.cell.row;
		out[i].col = enemy.pos.cell.col;
		out[i].hp = enemy.hp;
		out[i].maxHp = enemy.maxHp;
		out[i].alive = enemy.dying ? 0 : 1;
	}
	return enemies.size();
}

int32_t BP_CALL hp_drops_snapshot(BpDropInfo* out, int32_t capacity)
{
	bp::GameEngine* game = engine();
	if (!game) { return 0; }

	const QList<bp::DropItem>& drops = game->snapshot().drops;
	if (!out || capacity <= 0) { return drops.size(); }

	const int count = qMin<int>(capacity, drops.size());
	for (int i = 0; i < count; ++i)
	{
		out[i].size = sizeof(BpDropInfo);
		out[i].row = drops.at(i).pos.cell.row;
		out[i].col = drops.at(i).pos.cell.col;
		out[i].itemName = poolString(drops.at(i).name);
	}
	return drops.size();
}

//玩家状态：单人模式只有 0 号（就是"那个玩家"）；多人模式按 id 升序的下标取
//
// 为什么两种模式共用一个函数：契约 §7.5 要求"模组只跑在主机"，而主机在单人与多人
// 下都持有引擎 —— 让模组按同一套写法取玩家状态，比对它说"多人时请换一个函数"友好。
// 单人返回 1、多人返回在场人数，模组据此知道该循环几次。
int32_t BP_CALL hp_player_state(int32_t playerIndex, BpPlayerState* out)
{
	if (!out || playerIndex < 0) { return BP_ERR_INVALID_ARG; }

	bp::GameEngine* game = engine();
	if (!game) { return BP_ERR_NOT_READY; }

	const bp::GameSnapshot& snap = game->snapshot();
	out->size = sizeof(BpPlayerState);

	//多人：快照里有玩家列表，按下标取
	if (!snap.players.isEmpty())
	{
		if (playerIndex >= snap.players.size()) { return BP_ERR_INVALID_ARG; }

		const bp::GameSnapshot::PlayerView& player = snap.players.at(playerIndex);
		out->row = player.pos.cell.row;
		out->col = player.pos.cell.col;
		out->lives = player.lives;
		out->maxLives = player.maxLives;
		out->bombRange = player.bombRange;
		out->maxBombs = player.maxBombs;
		out->ghost = player.ghost ? 1 : 0;
		return BP_OK;
	}

	//单人：只有下标 0 有意义（越界如实报错，而不是悄悄给别人的数据）
	if (playerIndex != 0) { return BP_ERR_INVALID_ARG; }

	out->row = snap.playerPos.cell.row;
	out->col = snap.playerPos.cell.col;
	out->lives = snap.playerLives;
	out->maxLives = snap.playerMaxLives;
	out->bombRange = snap.playerBombRange;
	out->maxBombs = snap.playerMaxBombs;
	out->ghost = snap.playerGhost ? 1 : 0;
	return BP_OK;
}

int32_t BP_CALL hp_player_count(void)
{
	bp::GameEngine* game = engine();
	if (!game) { return 0; }

	const bp::GameSnapshot& snap = game->snapshot();
	return snap.players.isEmpty() ? 1 : snap.players.size();
}

int32_t BP_CALL hp_entity_count(void)
{
	bp::GameEngine* game = engine();
	if (!game) { return 0; }
	const bp::GameSnapshot& snap = game->snapshot();
	return snap.enemies.size() + snap.drops.size() + snap.bombs.size();
}

// ---------- 宿主函数表：世界元信息（0.1.1 追加） ----------

int32_t BP_CALL hp_level_index(void)
{
	bp::GameEngine* game = engine();
	return game ? game->snapshot().level : 0;
}

int32_t BP_CALL hp_phase(void)
{
	bp::GameEngine* game = engine();
	if (!game) { return BP_PHASE_READY; }

	switch (game->snapshot().phase)
	{
	case bp::Phase::Running: return BP_PHASE_RUNNING;
	case bp::Phase::Paused:  return BP_PHASE_PAUSED;
	case bp::Phase::Victory: return BP_PHASE_VICTORY;
	case bp::Phase::Defeat:  return BP_PHASE_DEFEAT;
	default:                 return BP_PHASE_READY;
	}
}

int32_t BP_CALL hp_step_index(void)
{
	bp::GameEngine* game = engine();
	return game ? game->snapshot().stepIndex : 0;
}

// ---------- 宿主函数表：炸弹快照（0.1.1 追加） ----------
//
// 0.1.0 只能问"这一格有几枚炸弹"（bomb_count_at），拿不到引信与威力 ——
// 想画个倒计时或者统计"谁埋了多少雷"都做不到。这里按统一的"两段式"补齐。
int32_t BP_CALL hp_bombs_snapshot(BpBombInfo* out, int32_t capacity)
{
	bp::GameEngine* game = engine();
	if (!game) { return 0; }

	const QList<bp::GameSnapshot::BombView>& bombs = game->snapshot().bombViews;
	if (!out || capacity <= 0) { return bombs.size(); }

	const int count = qMin<int>(capacity, bombs.size());
	for (int i = 0; i < count; ++i)
	{
		const bp::GameSnapshot::BombView& bomb = bombs.at(i);
		out[i].size = sizeof(BpBombInfo);
		out[i].row = bomb.pos.cell.row;
		out[i].col = bomb.pos.cell.col;
		//敌人炸弹的归属编号对模组没有意义（引擎内部用它区分放弹上限），统一报 -1
		out[i].ownerId = (bomb.owner == bp::BombOwner::Enemy) ? -1 : bomb.ownerId;
		out[i].range = bomb.range;
		out[i].remainingSeconds = bomb.remainingSeconds;
		out[i].fuseTotal = bomb.fuseTotal;
	}
	return bombs.size();
}

// ---------- 宿主函数表：订阅 ----------

BpResult BP_CALL hp_subscribe(uint32_t type, void (BP_CALL* cb)(const BpGameEvent*, void*), void* user)
{
	if (!g_current) { return BP_ERR_NOT_READY; }
	if (!cb) { return BP_ERR_INVALID_ARG; }
	//事件分发过程中订阅 = 重入（正遍历的订阅表会被改），如实拒绝而不是碰运气
	if (g_dispatchingEvents) { return BP_ERR_REENTRANT; }

	g_current->subscriptions.append({ type, cb, user });
	return BP_OK;
}

// ---------- 宿主函数表：写请求（0.1.2 起全部真实生效，但一律是"请求"） ----------
//
// 这一组是 0.1.0 就留在函数表里的槽位，0.1.2 把它们从"永远返回 UNSUPPORTED"变成真的。
// 每个函数都遵守同一条纪律：**引擎校验不过就返回错误码，世界一点都没变**。

namespace
{
	//把"校验结果"翻译成返回码：同一套语义，模组作者只需要记住"0 就是成功"
	BpResult fromBool(bool ok) { return ok ? BP_OK : BP_ERR_REJECTED; }
}

BpResult BP_CALL hp_set_var_f64(BpStr name, double value)
{
	bp::GameEngine* game = engine();
	if (!game) { return BP_ERR_NOT_READY; }

	//走与 mod 的 config/gameplay.json 同一张白名单：不认识的键如实拒绝
	return fromBool(game->requestSetConfigNumber(fromBpStr(name), value));
}

BpResult BP_CALL hp_set_var_i32(BpStr name, int32_t value)
{
	return hp_set_var_f64(name, static_cast<double>(value));
}

BpResult BP_CALL hp_spawn_enemy(int32_t row, int32_t col, int32_t hp)
{
	//地图生成期间 spawn_enemy 是"登记敌人"，不能直接塞进引擎
	//（引擎在 generate() 返回之后才建敌人列表，早塞进去会被清掉）
	if (NativeModHost::instance().isGeneratingMap())
	{
		return fromBool(NativeModHost::instance().registerGeneratedEnemy(row, col, hp));
	}

	bp::GameEngine* game = engine();
	if (!game) { return BP_ERR_NOT_READY; }
	return fromBool(game->requestSpawnEnemy(bp::GridPos(row, col), hp));
}

BpResult BP_CALL hp_remove_entity(int32_t entityId)
{
	bp::GameEngine* game = engine();
	if (!game) { return BP_ERR_NOT_READY; }
	return fromBool(game->requestRemoveEntity(entityId));
}

BpResult BP_CALL hp_damage_entity(int32_t entityId, int32_t amount)
{
	bp::GameEngine* game = engine();
	if (!game) { return BP_ERR_NOT_READY; }
	return fromBool(game->requestDamageEntity(entityId, amount));
}

BpResult BP_CALL hp_damage_player(int32_t playerIndex, int32_t amount)
{
	bp::GameEngine* game = engine();
	if (!game) { return BP_ERR_NOT_READY; }
	return fromBool(game->requestDamagePlayer(playerIndex, amount));
}

BpResult BP_CALL hp_spawn_drop(int32_t row, int32_t col, BpStr itemName)
{
	bp::GameEngine* game = engine();
	if (!game) { return BP_ERR_NOT_READY; }
	return fromBool(game->requestSpawnDrop(bp::GridPos(row, col), fromBpStr(itemName)));
}

//时间倍率：真正的落点在帧循环（GameLoop）上，界面把它接进宿主
BpResult BP_CALL hp_set_time_scale(double scale)
{
	NativeModHost& host = NativeModHost::instance();
	if (!host.hasTimeScaleSink()) { return BP_ERR_NOT_READY; }

	host.applyTimeScale(scale);
	return BP_OK;
}

//在"铺地图"期间写一个格子（0.1.2 末尾追加）。其它时候调用是时机不对，如实拒绝
int32_t BP_CALL hp_set_tile(int32_t row, int32_t col, int32_t tile)
{
	NativeModHost& host = NativeModHost::instance();
	if (!host.isGeneratingMap()) { return BP_ERR_NOT_READY; }
	return fromBool(host.setGeneratedTile(row, col, tile));
}

//铺地图期间：指定玩家出生点（写在墙上会被拒绝）
int32_t BP_CALL hp_set_player_start(int32_t row, int32_t col)
{
	NativeModHost& host = NativeModHost::instance();
	if (!host.isGeneratingMap()) { return BP_ERR_NOT_READY; }
	return host.setGeneratedPlayerStart(row, col) ? BP_OK : BP_ERR_REJECTED;
}

//注册一个模式（0.1.2：0.1.0 起就留着的槽位）。只能在 init 里调
BpResult BP_CALL hp_register_mode(const BpModeDesc* desc)
{
	if (!g_current) { return BP_ERR_NOT_READY; }
	if (!desc) { return BP_ERR_INVALID_ARG; }
	//size 必须至少覆盖到我们认识的那一段（老模组给更小的结构体也能读）
	if (desc->size < offsetof(BpModeDesc, flags)) { return BP_ERR_INVALID_ARG; }

	return NativeModHost::instance().registerModeForCurrentMod(desc);
}

BpResult BP_CALL hp_request_pause(int32_t paused)
{
	bp::GameEngine* game = engine();
	if (!game) { return BP_ERR_NOT_READY; }

	//暂停是"允许的写操作"：它改变的是引擎的相位，而相位本来就由界面控制
	game->setPhase(paused ? bp::Phase::Paused : bp::Phase::Running);
	return BP_OK;
}

//触发一件已有道具的效果（引擎自己的效果注册表说了算，模组只是发动它）
BpResult BP_CALL hp_apply_item_effect(BpStr nameOrEffect)
{
	bp::GameEngine* game = engine();
	if (!game) { return BP_ERR_NOT_READY; }

	const QString effectId = game->resolveEffectId(fromBpStr(nameOrEffect));
	if (effectId.isEmpty()) { return BP_ERR_INVALID_ARG; }

	return game->applyEffect(effectId) ? BP_OK : BP_ERR_REJECTED;
}

// ---------- 宿主函数表：随机数 ----------

uint32_t BP_CALL hp_rng_next(void)
{
	if (!g_current) { return 0; }
	return g_current->rng.generate();
}

int32_t BP_CALL hp_rng_range(int32_t minInclusive, int32_t maxExclusive)
{
	if (!g_current || maxExclusive <= minInclusive) { return minInclusive; }
	return minInclusive + static_cast<int32_t>(
		g_current->rng.bounded(static_cast<quint32>(maxExclusive - minInclusive)));
}

//宿主函数表：静态单例（指针必须稳定，模组会长期持有）
BpHostApi& hostTable()
{
	static BpHostApi table = []()
	{
		BpHostApi api{};
		api.size = sizeof(BpHostApi);

		api.has_feature = &hp_has_feature;
		api.abi_minor = &hp_abi_minor;

		api.log = &hp_log;
		api.alloc = &hp_alloc;
		api.free_ = &hp_free;

		api.board_width = &hp_board_width;
		api.board_height = &hp_board_height;
		api.tile_at = &hp_tile_at;
		api.bomb_count_at = &hp_bomb_count_at;
		api.enemies_snapshot = &hp_enemies_snapshot;
		api.drops_snapshot = &hp_drops_snapshot;
		api.player_state = &hp_player_state;
		api.entity_count = &hp_entity_count;

		api.subscribe = &hp_subscribe;

		api.set_var_i32 = &hp_set_var_i32;
		api.set_var_f64 = &hp_set_var_f64;
		api.spawn_enemy = &hp_spawn_enemy;
		api.remove_entity = &hp_remove_entity;
		api.damage_entity = &hp_damage_entity;
		api.damage_player = &hp_damage_player;
		api.spawn_drop = &hp_spawn_drop;
		api.set_time_scale = &hp_set_time_scale;
		api.request_pause = &hp_request_pause;
		api.apply_item_effect = &hp_apply_item_effect;

		api.rng_next = &hp_rng_next;
		api.rng_range = &hp_rng_range;

		//0.1.2：register_mode 顶替了原来的 reserved_mode 槽位（偏移不变）
		api.reserved_mapgen = nullptr;
		api.reserved_ai = nullptr;
		api.reserved_win = nullptr;

		//0.1.1 追加（只能接在预留槽位之后：这就是"只在末尾追加"的代价）
		api.level_index = &hp_level_index;
		api.phase = &hp_phase;
		api.step_index = &hp_step_index;
		api.player_count = &hp_player_count;
		api.bombs_snapshot = &hp_bombs_snapshot;

		//0.1.2：把 0.1.0 预留的槽位填成真函数（偏移一个字节都没变），再追加 set_tile
		api.register_mode = &hp_register_mode;
		api.set_tile = &hp_set_tile;
		api.set_player_start = &hp_set_player_start;
		return api;
	}();
	return table;
}

//事件类型映射：引擎的事件 -> ABI 的事件号
//
// 0.1.1 修掉了两个"合并"：
//   · EnemyDamaged 以前被塞进 ENEMY_DIED —— 收到"敌人受伤"却以为它死了，这是真错；
//   · ItemUsed 以前被塞进 DROP_PICKED —— 使用道具与捡到道具是两件事。
// 这两个都是 0.1.0 的事件号还没对外用过（没有已发布模组），所以补上独立编号，
// 老编号一律不动（加法式演进的底线）。
uint32_t mapEventType(bp::GameEventType type)
{
	switch (type)
	{
	case bp::GameEventType::BombPlaced:    return BP_EVENT_BOMB_PLACED;
	case bp::GameEventType::BombExploded:  return BP_EVENT_BOMB_EXPLODED;
	case bp::GameEventType::EnemyDamaged:  return BP_EVENT_ENEMY_DAMAGED;
	case bp::GameEventType::EnemyDied:     return BP_EVENT_ENEMY_DIED;
	case bp::GameEventType::PlayerDamaged: return BP_EVENT_PLAYER_DAMAGED;
	case bp::GameEventType::DropSpawned:   return BP_EVENT_DROP_SPAWNED;
	case bp::GameEventType::DropCollected: return BP_EVENT_DROP_PICKED;
	case bp::GameEventType::ItemUsed:      return BP_EVENT_ITEM_USED;
	default:                               return 0;
	}
}

} // namespace

// ---------- ModContext 的定义在文件开头（宿主函数表需要完整类型） ----------

// ---------- 单例与查询 ----------

NativeModHost& NativeModHost::instance()
{
	static NativeModHost s_instance;
	return s_instance;
}

int NativeModHost::activeCount() const
{
	int count = 0;
	for (const LoadedMod& mod : m_mods)
	{
		if (mod.active) { ++count; }
	}
	return count;
}

//逐模组耗时统计（契约 §7.3）
//为什么统计的是"回调次数 + 平均/最大耗时"而不是"总耗时"：
//总耗时随对局时长线性增长，没有可比性；平均与最大值才能回答"这个模组慢不慢"
QList<NativeModHost::ModStats> NativeModHost::stats() const
{
	QList<ModStats> list;
	for (const ModContext* context : m_contexts)
	{
		ModStats item;
		item.id = context->id;
		item.callbacks = context->callbacks;
		item.slowCallbacks = context->slowCallbacks;
		item.maxCallbackMs = context->maxCallbackUs / 1000.0;
		item.avgCallbackMs = (context->callbacks > 0)
			? (context->totalCallbackUs / 1000.0) / context->callbacks
			: 0.0;
		list.append(item);
	}
	return list;
}

QString NativeModHost::currentModId() const
{
	return g_current ? g_current->id : QString();
}

void NativeModHost::logForCurrentMod(int level, const QString& text)
{
	const QString id = g_current ? g_current->id : QStringLiteral("unknown");
	FileManager::instance().log(QStringLiteral("mod:%1").arg(id),
		QStringLiteral("[%1] %2").arg(level).arg(text));
}

void NativeModHost::setActiveEngine(bp::GameEngine* engine)
{
	m_engine = engine;
}

QStringList NativeModHost::activeIds() const
{
	QStringList ids;
	for (const LoadedMod& mod : m_mods)
	{
		if (mod.active) { ids.append(mod.id); }
	}
	ids.sort();
	return ids;
}

//模组集合指纹：联机握手用（契约 §7.5 —— 集合不一致就拒绝加入，避免各端规则不同）
QString NativeModHost::modSetHash() const
{
	QStringList parts;
	for (const LoadedMod& mod : m_mods)
	{
		if (!mod.active) { continue; }
		parts.append(QStringLiteral("%1@%2#%3").arg(mod.id, mod.version, mod.apiVersion));
	}
	parts.sort();

	const QByteArray joined = parts.join(QLatin1Char('|')).toUtf8();
	return QString::fromLatin1(QCryptographicHash::hash(joined, QCryptographicHash::Sha1).toHex().left(12));
}

// ---------- 加载 ----------

void NativeModHost::loadAll(QStringList* problems)
{
	if (m_loaded) { return; }
	m_loaded = true;

	//安全模式：本次运行不加载任何原生模组（数据类模组不受影响）
	if (m_safeMode)
	{
		FileManager::instance().log("mod", QStringLiteral("安全模式：跳过所有原生模组"));
		return;
	}

	if (!SettingsManager::instance().allowNativeMods())
	{
		FileManager::instance().log("mod", QStringLiteral("未开启「允许原生模组」，跳过所有原生模组"));
		return;
	}

	const QByteArray hostVersion = QByteArray(BP_MOD_API_VERSION);
	BpHostApi& host = hostTable();

	//崩溃定位：先把"本次打算加载谁"记下来（LoadLibrary 里崩掉时日志都来不及写，
	//所以这份记录必须**在碰任何 DLL 之前**落盘 —— 契约 §3.3）
	QStringList planned;
	for (const ModManager::ModEntry& entry : ModManager::instance().mods())
	{
		if (entry.manifest.native && entry.active) { planned.append(entry.manifest.id); }
	}
	LaunchGuard::instance().setPlannedNativeMods(planned);

	for (const ModManager::ModEntry& entry : ModManager::instance().mods())
	{
		if (!entry.manifest.native) { continue; }

		LoadedMod info;
		info.id = entry.manifest.id;
		info.name = entry.manifest.name;
		info.version = entry.manifest.version;
		info.apiVersion = QString::fromLatin1(BP_MOD_API_VERSION);

		if (!entry.active)
		{
			//数据层没生效（被禁用/被兼容性拒绝）—— 原生部分也不加载，避免"半个模组"
			info.reason = entry.blockedReason.isEmpty()
				? QStringLiteral("已禁用") : entry.blockedReason;
			m_mods.append(info);
			continue;
		}

		const QString fileName = entry.manifest.nativeLib.isEmpty()
			? entry.manifest.id : entry.manifest.nativeLib;
		const QString path = entry.directory + QLatin1Char('/') + fileName
			+ (fileName.endsWith(QLatin1String(".dll"), Qt::CaseInsensitive) ? QString() : QStringLiteral(".dll"));
		info.dllPath = path;

		if (!QFileInfo::exists(path))
		{
			info.reason = QStringLiteral("找不到原生模组文件：%1").arg(path);
			m_mods.append(info);
			if (problems) { problems->append(QStringLiteral("%1: %2").arg(info.id, info.reason)); }
			continue;
		}

		QLibrary* library = new QLibrary(path);
		//永不卸载：卸载一个还持有静态对象/回调的 DLL 极易崩（契约 §3.2）
		library->setLoadHints(QLibrary::PreventUnloadHint);

		//**加载之前**打点：如果这一句里直接崩掉，文件里指向的就是这个模组
		LaunchGuard::instance().markNativeModLoading(info.id);

		if (!library->load())
		{
			info.reason = QStringLiteral("加载失败：%1").arg(library->errorString());
			delete library;
			m_mods.append(info);
			FileManager::instance().log("mod", QStringLiteral("%1: %2").arg(info.id, info.reason));
			continue;
		}

		using EntryFn = const BpModApi* (BP_CALL*)(const char*, const BpHostApi*);
		EntryFn entryFn = reinterpret_cast<EntryFn>(library->resolve("bp_mod_entry"));
		if (!entryFn)
		{
			info.reason = QStringLiteral("这个 DLL 没有导出 bp_mod_entry（是不是忘了 BP_EXPORT？）");
			m_mods.append(info);
			FileManager::instance().log("mod", QStringLiteral("%1: %2").arg(info.id, info.reason));
			continue;
		}

		const BpModApi* api = entryFn(hostVersion.constData(), &host);
		if (!api)
		{
			//模组主动拒绝：这是契约里留给作者的"优雅退出"
			info.reason = QStringLiteral("模组主动拒绝加载（通常表示接口版本不兼容）");
			m_mods.append(info);
			FileManager::instance().log("mod", QStringLiteral("%1: %2").arg(info.id, info.reason));
			continue;
		}

		//表至少要包含 init/shutdown/on_tick 三项（size 是模组自己填的）
		if (api->size < offsetof(BpModApi, on_generate_map) || !api->init)
		{
			info.reason = QStringLiteral("函数表不完整（size=%1）").arg(api->size);
			m_mods.append(info);
			FileManager::instance().log("mod", QStringLiteral("%1: %2").arg(info.id, info.reason));
			continue;
		}

		ModContext* context = new ModContext();
		context->id = info.id;
		context->name = info.name;
		context->version = info.version;
		context->dllPath = path;
		context->apiVersion = info.apiVersion;
		context->library = library;
		context->api = api;

		//交给模组的声明：它可以用 buildId 判断"我这份头文件和引擎是不是同一版"
		BpModInfo modInfo{};
		modInfo.size = sizeof(BpModInfo);
		modInfo.abiVersionMajor = 0;
		modInfo.abiVersionMinor = 1;
		modInfo.pointerSize = sizeof(void*);
		modInfo.endianLittle = 1;
		modInfo.buildId = BP_BUILD_ID;
		modInfo.id = poolString(info.id);
		modInfo.name = poolString(info.name);
		modInfo.version = poolString(info.version);
		modInfo.modDir = poolString(entry.directory);

		//init 期间允许订阅，所以要先把 g_current 设好
		ModContext* previous = g_current;
		g_current = context;
		g_stringPool.clear();

		const BpResult initResult = api->init(&modInfo, &host);

		g_current = previous;
		g_stringPool.clear();

		if (initResult != 0)
		{
			info.reason = QStringLiteral("init 返回 %1（拒绝加载）").arg(initResult);
			delete context;
			m_mods.append(info);
			FileManager::instance().log("mod", QStringLiteral("%1: %2").arg(info.id, info.reason));
			continue;
		}

		context->active = true;
		info.active = true;
		m_contexts.append(context);
		m_mods.append(info);

		//init 成功 -> 记进 loadedOk：崩在下一个模组时，这个列表就是"哪些已经安全通过"
		LaunchGuard::instance().markNativeModLoaded(info.id);

		FileManager::instance().log("mod", QStringLiteral("原生模组已加载：%1 v%2（ABI %3，文件 %4）")
			.arg(info.name, info.version, info.apiVersion, QFileInfo(path).fileName()));
	}

	//汇总：加载了几个、跳过几个。原生死机风险高，这行日志是第一现场（契约 §3.2）
	FileManager::instance().log("mod", QStringLiteral("原生模组汇总：生效 %1 个，共 %2 个")
		.arg(activeCount()).arg(m_mods.size()));

	for (const LoadedMod& mod : m_mods)
	{
		if (!mod.active && !mod.reason.isEmpty())
		{
			FileManager::instance().log("mod", QStringLiteral("原生模组未加载：%1（%2）").arg(mod.id, mod.reason));
		}
	}
}

void NativeModHost::unloadAll()
{
	//只清记录：DLL 本身永不卸载（契约 §3.2），所以"关掉原生模组要重启游戏"
	endMode();          //先把正在跑的模式收尾（会调模组的 on_mode_stop）
	qDeleteAll(m_contexts);
	m_contexts.clear();
	m_mods.clear();
	m_modes.clear();    //模式是模组注册的：模组没了，模式也没了
	m_loaded = false;
	m_engine = nullptr;
}

// ---------- 每步分发 ----------

void NativeModHost::onEngineStepped(double stepSeconds)
{
	if (!m_engine || m_contexts.isEmpty()) { return; }

	const bp::GameSnapshot& snapshot = m_engine->snapshot();

	//1) 合成事件：关卡开始 / 过关 / 失败
	//   引擎没有这三个事件（它只发"发生了什么"，不发"这一局怎么了"），但模组最需要它们，
	//   而它们完全可以从快照推出来 —— 于是**不需要改引擎**就能有（交接文档 §4.6 第 3 条）
	if (snapshot.level != m_lastLevel)
	{
		m_lastLevel = snapshot.level;
		m_lastPhase = -1;                      //新一局：阶段检测重新开始
		dispatchSyntheticLevel(BP_EVENT_LEVEL_START, snapshot.level, 0);
	}

	const int phase = static_cast<int>(snapshot.phase);
	if (phase != m_lastPhase)
	{
		m_lastPhase = phase;
		if (snapshot.phase == bp::Phase::Victory)
		{
			dispatchSyntheticLevel(BP_EVENT_LEVEL_WON, snapshot.level, 1);
		}
		else if (snapshot.phase == bp::Phase::Defeat)
		{
			dispatchSyntheticLevel(BP_EVENT_LEVEL_LOST, snapshot.level, 0);
		}
	}

	//2) 引擎这一步产生的事件
	for (const bp::GameEvent& event : snapshot.events)
	{
		const uint32_t type = mapEventType(event.type);
		if (type != 0) { dispatchEngineEvent(type, event); }
	}

	//3) 每个模组的 on_tick
	for (ModContext* context : m_contexts)
	{
		if (!context->active || !context->api || !context->api->on_tick) { continue; }

		QElapsedTimer timer;
		timer.start();

		enterContext(context);
		context->api->on_tick(stepSeconds);
		leaveContext(context, timer);
	}
}

//进入一个模组的回调：重置本次调用的状态
void NativeModHost::enterContext(ModContext* context)
{
	g_current = context;
	g_stringPool.clear();
	context->commandsThisTick = 0;
}

//离开：记账 + 超时警告（契约 §7.3：只警告、不自动禁用，判断权交给玩家）
void NativeModHost::leaveContext(ModContext* context, QElapsedTimer& timer)
{
	const qint64 micros = timer.nsecsElapsed() / 1000;
	++context->callbacks;
	context->totalCallbackUs += micros;
	context->maxCallbackUs = qMax(context->maxCallbackUs, micros);

	//2ms 软预算：超了就记一条（日志里也会给出平均/最大值，模组页显示同样的信息）
	if (micros > 2000)
	{
		++context->slowCallbacks;
		if (context->slowCallbacks <= 3 || context->slowCallbacks % 300 == 0)
		{
			FileManager::instance().log("mod", QStringLiteral("原生模组 %1 回调耗时 %2 ms（第 %3 次超 2ms）")
				.arg(context->id).arg(micros / 1000.0, 0, 'f', 2).arg(context->slowCallbacks));
		}
	}

	g_current = nullptr;
	g_stringPool.clear();
}

//把引擎事件发给订阅者
//
// 这一段的规矩：**载荷只在本次回调期间有效**，而且能把"引擎漏掉的信息"从快照里补出来
// 就补（例如炸弹的威力、敌人的剩余血量）—— 因为让模组自己去猜只会写出更难查的代码。
void NativeModHost::dispatchEngineEvent(uint32_t type, const bp::GameEvent& event)
{
	const bp::GameSnapshot& snapshot = m_engine->snapshot();

	BpGameEvent wrapped{};
	wrapped.size = sizeof(BpGameEvent);
	wrapped.type = type;

	//载荷按类型填；所有载荷首字段都是 size（老模组只读它认识的那部分）
	BpEventBomb bomb{};
	BpEventExplosion explosion{};
	BpEventEntity entity{};
	BpEventDamage damage{};
	BpEventDrop drop{};
	const void* payload = nullptr;

	switch (type)
	{
	case BP_EVENT_BOMB_PLACED:
	{
		bomb.size = sizeof(BpEventBomb);
		bomb.row = event.cell.row;
		bomb.col = event.cell.col;
		bomb.playerIndex = (event.ownerId == 0) ? -1 : event.ownerId;

		//威力不在事件里（引擎发 BombPlaced 时 amount 是 0），从快照里补：
		//"这一格刚放下的那枚炸弹"就是我们要找的
		bomb.range = 0;
		for (const bp::GameSnapshot::BombView& view : snapshot.bombViews)
		{
			if (view.pos.cell == event.cell) { bomb.range = view.range; break; }
		}
		payload = &bomb;
		break;
	}

	case BP_EVENT_BOMB_EXPLODED:
		explosion.size = sizeof(BpEventExplosion);
		explosion.row = event.cell.row;
		explosion.col = event.cell.col;
		//引擎只报告爆炸范围，不报告"炸掉几块砖"（砖块被清掉之后就算不出来了）。
		//所以 destroyedBricks 本版本恒为 0（字段保留只为不破坏老模组），真实信息在 range 里
		explosion.destroyedBricks = 0;
		explosion.range = event.amount;
		payload = &explosion;
		break;

	case BP_EVENT_ENEMY_DAMAGED:
	case BP_EVENT_ENEMY_DIED:
	{
		damage.size = sizeof(BpEventDamage);
		damage.playerIndex = -1;                 //-1 = 敌人
		damage.amount = event.amount;
		//剩余血量从快照里找同一个格子上的敌人
		damage.livesLeft = 0;
		for (const bp::Enemy& enemy : snapshot.enemies)
		{
			if (enemy.pos.cell == event.cell) { damage.livesLeft = enemy.hp; break; }
		}
		if (type == BP_EVENT_ENEMY_DAMAGED)
		{
			payload = &damage;
		}
		else
		{
			entity.size = sizeof(BpEventEntity);
			entity.entityId = -1;                //引擎不报稳定 id（契约 §6 写明了这条限制）
			entity.row = event.cell.row;
			entity.col = event.cell.col;
			payload = &entity;
		}
		break;
	}

	case BP_EVENT_PLAYER_DAMAGED:
		damage.size = sizeof(BpEventDamage);
		damage.playerIndex = (event.ownerId == 0) ? 0 : event.ownerId;
		damage.amount = event.amount;
		//剩余生命：单人直接用快照那套单数字段；多人必须按 id 去玩家列表里找，
		//否则会一直报"还有 3 条命"—— 那种"看着像真的"的假数据比报错更害人
		damage.livesLeft = snapshot.playerLives;
		for (const bp::GameSnapshot::PlayerView& player : snapshot.players)
		{
			if (player.id == event.ownerId) { damage.livesLeft = player.lives; break; }
		}
		payload = &damage;
		break;

	case BP_EVENT_DROP_SPAWNED:
	case BP_EVENT_DROP_PICKED:
	case BP_EVENT_ITEM_USED:
		drop.size = sizeof(BpEventDrop);
		drop.entityId = -1;
		drop.row = event.cell.row;
		drop.col = event.cell.col;
		drop.itemName = poolString(event.text);   //0.1.1：掉落/道具事件带名字
		payload = &drop;
		break;

	default:
		entity.size = sizeof(BpEventEntity);
		entity.entityId = -1;
		entity.row = event.cell.row;
		entity.col = event.cell.col;
		payload = &entity;
		break;
	}

	wrapped.payload = payload;

	dispatchToSubscribers(wrapped);

	//玩家生命归零时补一个 PLAYER_DIED：引擎没有这个事件（它只报"受伤"），
	//而"这局结束了"正是模组最想知道的时刻之一。
	//判据用上面刚算出来的 livesLeft（多人时那是**被打的那个玩家**的剩余生命）
	if (type == BP_EVENT_PLAYER_DAMAGED && damage.livesLeft <= 0)
	{
		BpEventDamage died{};
		died.size = sizeof(BpEventDamage);
		died.playerIndex = damage.playerIndex;
		died.amount = damage.amount;
		died.livesLeft = 0;

		BpGameEvent second{};
		second.size = sizeof(BpGameEvent);
		second.type = BP_EVENT_PLAYER_DIED;
		second.payload = &died;
		dispatchToSubscribers(second);
	}
}

//宿主自己合成的事件：关卡开始 / 过关 / 失败
void NativeModHost::dispatchSyntheticLevel(uint32_t type, int32_t level, int32_t victory)
{
	BpEventLevel info{};
	info.size = sizeof(BpEventLevel);
	info.entityId = -1;
	info.row = -1;
	info.col = -1;
	info.level = level;
	info.victory = victory;

	BpGameEvent wrapped{};
	wrapped.size = sizeof(BpGameEvent);
	wrapped.type = type;
	wrapped.payload = &info;

	dispatchToSubscribers(wrapped);
}

void NativeModHost::dispatchToSubscribers(const BpGameEvent& event)
{
	//重入防护：订阅表在下面的循环里被读，如果模组在回调里再 subscribe，
	//就是在遍历一个正在被改的列表（契约 §8：回调里不能再触发事件）
	g_dispatchingEvents = true;

	for (ModContext* context : m_contexts)
	{
		if (!context->active) { continue; }

		//按值取一份订阅表：回调里即使有人改它，这里的遍历也不会失效
		const QList<ModContext::Subscription> subscriptions = context->subscriptions;
		for (const ModContext::Subscription& subscription : subscriptions)
		{
			const bool wants = (subscription.type == BP_EVENT_ANY || subscription.type == event.type);
			if (!wants) { continue; }

			QElapsedTimer timer;
			timer.start();

			enterContext(context);
			subscription.cb(&event, subscription.user);
			leaveContext(context, timer);
		}
	}

	g_dispatchingEvents = false;
}

// ============================================================
// 模组模式（0.1.2）
//
// 一个模式 = 一套玩法。它出现在「开始游戏 → 选择模式」里，点进去就直接开局
// （**不经过关卡选择**：关卡流程由模组自己设计，游戏不在中间插一层界面）。
//
// 模式与引擎的关系（这一轮的做法，见契约 §11 的 L3）：
//   引擎保留"确定性内核"：玩家移动、炸弹、爆炸、掉落、胜负判定框架；
//   模组接管三件可选的规则：地图怎么铺、敌人怎么动、这一局什么时候结束，
//   并且可以在任意时刻用写请求生成/移除/伤害实体、改数值、改时间倍率。
//   于是"完全不同的玩法"能写出来，而 GameCanvas 的渲染契约（只读快照）一点没破。
//
// 另一种模式类型（BP_MODE_KIND_OWN_ENGINE：模组自己跑每个逻辑步）在这一轮**没有实现**，
// 注册时如实拒绝并说明原因 —— 契约里最硬的一条是"绝不调了没反应"。
// ============================================================

NativeModHost::ModContext* NativeModHost::findContext(const QString& modId) const
{
	for (ModContext* context : m_contexts)
	{
		if (context->id == modId) { return context; }
	}
	return nullptr;
}

//把一个 BpModeDesc 变成 ModMode（校验不通过时 usable=false 并填 reason）
NativeModHost::ModMode NativeModHost::parseModeDesc(ModContext* context, const void* raw)
{
	ModMode mode;
	const BpModeDesc* desc = static_cast<const BpModeDesc*>(raw);
	if (!desc) { return mode; }

	mode.modId = context ? context->id : QString();
	mode.id = fromBpStr(desc->id).trimmed();
	mode.name = fromBpStr(desc->name).trimmed();
	mode.hint = fromBpStr(desc->hint).trimmed();
	mode.description = fromBpStr(desc->description).trimmed();
	mode.kind = desc->kind;
	mode.flags = desc->flags;
	mode.playerLives = desc->playerLives;
	mode.playerMaxBombs = desc->playerMaxBombs;
	mode.playerBombRange = desc->playerBombRange;
	mode.timeLimitSeconds = desc->timeLimitSeconds;

	//名字没写就用 id，别在卡片上显示一个空标题
	if (mode.name.isEmpty()) { mode.name = mode.id; }
	//没有声明任何"允许谁玩"时，默认单人可玩（最常见的那种）
	if ((mode.flags & (BP_MODE_FLAG_SINGLE | BP_MODE_FLAG_MULTIPLAYER)) == 0)
	{
		mode.flags |= BP_MODE_FLAG_SINGLE;
	}

	if (mode.id.isEmpty())
	{
		mode.usable = false;
		mode.reason = QStringLiteral("模式没有 id");
		return mode;
	}
	if (mode.id.contains(QLatin1Char('/')))
	{
		mode.usable = false;
		mode.reason = QStringLiteral("模式 id 不能包含 '/'（它用来拼全局标识）");
		return mode;
	}
	if (mode.kind == BP_MODE_KIND_OWN_ENGINE)
	{
		//如实拒绝：声明了却不实现是最糟糕的一种"看起来能用"
		mode.usable = false;
		mode.reason = QStringLiteral("本版本还不支持「模组自己跑每个逻辑步」的模式（kind=OWN_ENGINE）");
		return mode;
	}
	if (mode.kind != BP_MODE_KIND_HOOKS)
	{
		mode.usable = false;
		mode.reason = QStringLiteral("未知的模式类型（kind=%1）").arg(mode.kind);
		return mode;
	}

	mode.usable = true;
	return mode;
}

//注册模式：只能在模组的 init 里调（g_current 指向正在 init 的模组）
int32_t NativeModHost::registerModeForCurrentMod(const BpModeDesc* desc)
{
	if (!g_current) { return BP_ERR_NOT_READY; }

	const ModMode mode = parseModeDesc(g_current, desc);

	//同一个模组里 id 不能重复（重复注册是作者的 bug，如实拒绝）
	for (const ModMode& existing : m_modes)
	{
		if (existing.modId == mode.modId && existing.id == mode.id)
		{
			FileManager::instance().log("mod", QStringLiteral("%1: 模式「%2」重复注册，已忽略")
				.arg(mode.modId, mode.id));
			return BP_ERR_REJECTED;
		}
	}
	//每个模组最多 16 个模式：界面放得下，也不至于让一个模组把选择页塞满
	if (m_modes.size() >= 64)
	{
		return BP_ERR_QUOTA;
	}

	m_modes.append(mode);

	if (mode.usable)
	{
		FileManager::instance().log("mod", QStringLiteral("%1: 注册模式「%2」%3")
			.arg(mode.modId, mode.name,
				 (mode.flags & BP_MODE_FLAG_MULTIPLAYER) ? QStringLiteral("（可联机）") : QString()));
	}
	else
	{
		FileManager::instance().log("mod", QStringLiteral("%1: 模式「%2」不可用：%3")
			.arg(mode.modId, mode.id, mode.reason));
	}

	return mode.usable ? BP_OK : BP_ERR_UNSUPPORTED;
}

const NativeModHost::ModMode* NativeModHost::findMode(const QString& modId, const QString& modeId) const
{
	for (const ModMode& mode : m_modes)
	{
		if (mode.modId == modId && mode.id == modeId) { return &mode; }
	}
	return nullptr;
}

const NativeModHost::ModMode* NativeModHost::activeMode() const
{
	if (m_activeModId.isEmpty() || m_activeModeId.isEmpty()) { return nullptr; }
	return findMode(m_activeModId, m_activeModeId);
}

//开始一个模组模式：把钩子挂到引擎上
bool NativeModHost::beginMode(const QString& modId, const QString& modeId,
							  bp::GameEngine* engine, QString* reason)
{
	if (reason) { reason->clear(); }

	const ModMode* mode = findMode(modId, modeId);
	if (!mode)
	{
		if (reason) { *reason = QStringLiteral("找不到这个模式（模组可能已被卸载）"); }
		return false;
	}
	if (!mode->usable)
	{
		if (reason) { *reason = mode->reason; }
		return false;
	}
	ModContext* context = findContext(modId);
	if (!context || !context->active || !context->api)
	{
		if (reason) { *reason = QStringLiteral("提供这个模式的模组没有加载成功"); }
		return false;
	}
	if (!engine)
	{
		if (reason) { *reason = QStringLiteral("没有可用的引擎"); }
		return false;
	}

	endMode();   //防御式：上一个模式没收拾干净时先收拾

	m_activeModId = modId;
	m_activeModeId = modeId;
	m_modeEngine = engine;
	m_modeFinishReason.clear();
	m_engine = engine;

	//1) 模式声明的数值覆盖（0 = 用引擎默认值）
	if (mode->playerLives > 0)    { engine->requestSetConfigNumber(QStringLiteral("playerMaxLives"), mode->playerLives); }
	if (mode->playerMaxBombs > 0) { engine->requestSetConfigNumber(QStringLiteral("playerMaxBombs"), mode->playerMaxBombs); }
	if (mode->playerBombRange > 0){ engine->requestSetConfigNumber(QStringLiteral("playerBombRange"), mode->playerBombRange); }
	if (mode->timeLimitSeconds > 0.0)
	{
		engine->requestSetConfigNumber(QStringLiteral("levelTimeoutSeconds"), mode->timeLimitSeconds);
	}

	//2) 敌人 AI 接管
	if (mode->flags & BP_MODE_FLAG_TAKE_OVER_AI)
	{
		const QString id = modId;
		engine->setEnemyThinkHook([this, id](const bp::Enemy& enemy, const bp::FrameContext& ctx,
											int& direction, bool& placeBomb)
		{
			//模式已经结束（或换了模式）就别再往下调了：模组可能已经卸载
			if (m_activeModId != id) { return 0; }
			return onModEnemyThink(enemy, ctx, direction, placeBomb);
		});
	}

	//3) 胜负判定接管（同时关掉引擎的默认判定：否则一个 0 敌人的模式会立刻自动通关）
	if (mode->flags & BP_MODE_FLAG_TAKE_OVER_WIN)
	{
		const QString id = modId;
		engine->setDefaultWinDisabled(true);
		engine->setWinConditionHook([this, id](QString* finishReason)
		{
			if (m_activeModId != id) { return -1; }
			return onModCheckWin(finishReason);
		});
	}

	//4) 通知模组"模式开始了"
	if (context->api->on_mode_start)
	{
		QElapsedTimer timer;
		timer.start();
		enterContext(context);
		context->api->on_mode_start(poolString(modeId));
		leaveContext(context, timer);
	}

	FileManager::instance().log("mod", QStringLiteral("模组模式开始：%1（%2）").arg(mode->name, mode->key()));
	return true;
}

void NativeModHost::endMode()
{
	if (m_activeModeId.isEmpty()) { return; }

	ModContext* context = findContext(m_activeModId);
	if (context && context->active && context->api && context->api->on_mode_stop)
	{
		QElapsedTimer timer;
		timer.start();
		enterContext(context);
		context->api->on_mode_stop(poolString(m_activeModeId));
		leaveContext(context, timer);
	}

	//钩子要摘掉：引擎可能还要活一会儿（结算画面），而模组那边已经"模式结束"了
	if (m_modeEngine)
	{
		m_modeEngine->setEnemyThinkHook(nullptr);
		m_modeEngine->setWinConditionHook(nullptr);
		m_modeEngine->setDefaultWinDisabled(false);
	}

	FileManager::instance().log("mod", QStringLiteral("模组模式结束：%1/%2")
		.arg(m_activeModId, m_activeModeId));

	m_activeModId.clear();
	m_activeModeId.clear();
	m_modeEngine = nullptr;
	m_genBoard = nullptr;
	m_genSpawns.clear();
	m_genPlayerStartSet = false;
}

// ---------- 地图生成 ----------

namespace
{
	//把"模式的地图生成"接到引擎的 IMapGenerator 接口上。
	//适配器本身不认识 ABI：它只转调宿主，宿主再转成 C 回调 —— 引擎那边完全干净
	class ModMapGenerator : public bp::IMapGenerator
	{
	public:
		explicit ModMapGenerator(const QString& label) : m_label(label) {}

		bp::GeneratedMap generate(bp::GameBoard& board, const bp::LevelSpec& spec) override
		{
			bp::GeneratedMap result;
			NativeModHost::instance().generateModeMap(board, spec, &result);
			return result;
		}

		QString name() const override { return m_label; }

	private:
		QString m_label;
	};
}

std::unique_ptr<bp::IMapGenerator> NativeModHost::takeModeMapGenerator()
{
	const ModMode* mode = activeMode();
	if (!mode) { return nullptr; }
	if ((mode->flags & BP_MODE_FLAG_TAKE_OVER_MAP) == 0) { return nullptr; }

	return std::make_unique<ModMapGenerator>(QStringLiteral("mod:%1").arg(mode->key()));
}

//真正的地图生成：准备好棋盘 → 调模组的 on_generate_map → 收集它写下的格子与敌人
void NativeModHost::generateModeMap(bp::GameBoard& board, const bp::LevelSpec& spec,
									bp::GeneratedMap* out)
{
	if (!out) { return; }

	const ModMode* mode = activeMode();
	ModContext* context = mode ? findContext(mode->modId) : nullptr;
	if (!mode || !context || !context->active || !context->api || !context->api->on_generate_map)
	{
		out->ok = false;      //模组没给地图 -> 调用方回落到默认随机生成
		return;
	}

	//先把棋盘清成"只有边框墙"，模组在干净的地基上铺
	board.fill(bp::TileType::Empty);
	board.buildBorderWalls();

	m_genBoard = &board;
	m_genSpawns.clear();
	m_genPlayerStartSet = false;
	m_genPlayerStart = spec.playerStart;

	BpMapGenRequest request{};
	request.size = sizeof(BpMapGenRequest);
	request.level = spec.level;
	request.rows = bp::kRows;
	request.cols = bp::kCols;
	request.seed = spec.seed;
	request.brickCount = spec.brickCount;
	request.enemyCount = spec.enemyCount;

	BpMapGenResult result{};
	result.size = sizeof(BpMapGenResult);
	result.ok = 0;
	result.playerStartRow = spec.playerStart.row;
	result.playerStartCol = spec.playerStart.col;

	QElapsedTimer timer;
	timer.start();
	enterContext(context);
	const int32_t answered = context->api->on_generate_map(&request, &result);
	leaveContext(context, timer);

	//把模组登记过的敌人交给引擎（它在 generate() 返回之后才建敌人列表）
	for (const QPair<bp::GridPos, int>& spawn : m_genSpawns)
	{
		bp::EnemySpawn entry;
		entry.pos = spawn.first;
		entry.hp = spawn.second;
		out->enemies.append(entry);
	}
	if (m_genPlayerStartSet && result.ok == 0)
	{
		result.playerStartRow = m_genPlayerStart.row;
		result.playerStartCol = m_genPlayerStart.col;
	}

	out->ok = (answered != 0 && result.ok != 0);
	out->usedLayout = out->ok;
	if (out->ok)
	{
		bp::GridPos start(result.playerStartRow, result.playerStartCol);
		if (!bp::GameBoard::inBounds(start) || board.tile(start) == bp::TileType::Wall)
		{
			//出生点写在墙里 = 模组的地图有问题：退回 (1,1) 并记一条（不要让玩家卡在墙里）
			FileManager::instance().log("mod", QStringLiteral("%1: 地图的玩家出生点不合法，已退回 (1,1)")
				.arg(mode->key()));
			start = bp::GridPos(1, 1);
		}
		out->playerStart = start;
		out->hasPlayerStart = true;
	}

	m_genBoard = nullptr;
	m_genSpawns.clear();
	m_genPlayerStartSet = false;
}

//铺地图期间：写一个格子
bool NativeModHost::setGeneratedTile(int32_t row, int32_t col, int32_t tile)
{
	if (!m_genBoard) { return false; }

	const bp::GridPos cell(row, col);
	if (!bp::GameBoard::inBounds(cell)) { return false; }
	//边框那一圈永远是墙：模组不能把玩家"放出棋盘"
	if (row == 0 || col == 0 || row == bp::kRows - 1 || col == bp::kCols - 1) { return false; }

	switch (tile)
	{
	case BP_TILE_EMPTY: m_genBoard->setTile(cell, bp::TileType::Empty); return true;
	case BP_TILE_WALL:  m_genBoard->setTile(cell, bp::TileType::Wall);  return true;
	case BP_TILE_BRICK: m_genBoard->setTile(cell, bp::TileType::Brick); return true;
	default: return false;      //爆炸痕这类东西不是"地图"
	}
}

//铺地图期间：指定玩家出生点
bool NativeModHost::setGeneratedPlayerStart(int32_t row, int32_t col)
{
	if (!m_genBoard) { return false; }

	const bp::GridPos cell(row, col);
	if (!bp::GameBoard::inBounds(cell)) { return false; }
	//写在墙里没有意义（玩家会被卡住）：如实拒绝，让调用方知道地图有问题
	if (m_genBoard->tile(cell) == bp::TileType::Wall) { return false; }

	m_genPlayerStart = cell;
	m_genPlayerStartSet = true;
	return true;
}

//铺地图期间：登记一个敌人
bool NativeModHost::registerGeneratedEnemy(int32_t row, int32_t col, int32_t hp)
{
	if (!m_genBoard) { return false; }

	const bp::GridPos cell(row, col);
	if (!bp::GameBoard::inBounds(cell)) { return false; }
	if (m_genBoard->tile(cell) != bp::TileType::Empty) { return false; }

	m_genSpawns.append(qMakePair(cell, qMax(1, hp)));
	return true;
}

//引擎侧的敌人思考钩子：把 req 填好 → 调模组 → 解析 result
int NativeModHost::onModEnemyThink(const bp::Enemy& enemy, const bp::FrameContext& ctx,
								   int& direction, bool& placeBomb)
{
	ModContext* context = findContext(m_activeModId);
	if (!context || !context->active || !context->api || !context->api->on_enemy_think)
	{
		return 0;
	}

	BpEnemyThinkRequest request{};
	request.size = sizeof(BpEnemyThinkRequest);
	request.enemyId = enemy.id;
	request.row = enemy.pos.cell.row;
	request.col = enemy.pos.cell.col;
	request.x = enemy.pos.col();
	request.y = enemy.pos.row();
	request.hp = enemy.hp;
	request.maxHp = enemy.maxHp;
	request.stepIndex = ctx.stepIndex;
	request.frameTime = ctx.frameTime;

	BpEnemyThinkResult result{};
	result.size = sizeof(BpEnemyThinkResult);
	result.acted = 0;
	result.direction = -1;
	result.placeBomb = 0;

	QElapsedTimer timer;
	timer.start();
	enterContext(context);
	const int32_t answered = context->api->on_enemy_think(&request, &result);
	leaveContext(context, timer);

	if (answered == 0 || result.acted == 0) { return 0; }

	direction = result.direction;
	placeBomb = (result.placeBomb != 0);
	return 1;
}

//引擎侧的胜负钩子
int NativeModHost::onModCheckWin(QString* finishReason)
{
	ModContext* context = findContext(m_activeModId);
	if (!context || !context->active || !context->api || !context->api->on_check_win)
	{
		return -1;
	}

	BpWinResult result{};
	result.size = sizeof(BpWinResult);
	result.decided = 0;
	result.victory = 0;

	QElapsedTimer timer;
	timer.start();
	enterContext(context);
	const int32_t answered = context->api->on_check_win(&result);
	const QString reason = fromBpStr(result.reason);
	leaveContext(context, timer);

	if (answered == 0 || result.decided == 0) { return -1; }

	if (!reason.isEmpty())
	{
		m_modeFinishReason = reason;
		if (finishReason) { *finishReason = reason; }
	}
	return (result.victory != 0) ? 1 : 0;
}

//时间倍率：转给帧循环（界面在开局时把 GameLoop::setTimeScale 接进来）
void NativeModHost::applyTimeScale(double scale)
{
	if (!m_timeScaleSink) { return; }

	//夹在 [0, 4]：更快就等于"一帧补一大堆逻辑步"，确定性会被破坏
	m_timeScaleSink(qBound(0.0, scale, 4.0));
}
