/* ============================================================================
 * BlastPioneer 原生模组示例（C++）
 *
 * 它做的事都是"看一眼、记一笔、请求一次"，正好覆盖 L2/L3（接口 0.1.2）开放的全部能力：
 *   · init 里问 has_feature（能力查询）与读棋盘尺寸 / 世界元信息（只读查询）
 *   · 订阅爆炸、关卡开始、关卡结束、敌人受伤、使用道具（事件订阅）
 *   · 爆炸回调里用引擎给的独立随机流（rng）
 *   · 炸了 3 次之后请求暂停（写请求：引擎可以拒绝，我们只看返回码）
 *   · 注册一个模式 demo_arena：自己铺地图、决定敌人怎么走、自己判胜负（钩子）
 *
 * 关于"老引擎上没有的函数"：0.1.1 追加的函数指针不在老引擎（0.1.0）的表范围内，
 * 所以**先看 host->size 再调用** —— 这是本契约里唯一正确的探测方式。
 * has_feature 只回答"这个能力在不在"，而 size 回答"我能不能够到那个函数指针"。
 *
 * 怎么编译：双击同目录的 build.bat（用 cl 直接编，不需要 VS 工程）。
 * 怎么安装：把编出来的 hello_native.dll 放在本目录，然后在游戏里启用本模组
 *           （并且要先在设置里打开「允许原生模组」）。
 * ========================================================================== */

#include "bp_mod_api.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

/* 这个函数指针在我的头文件范围内吗？（老引擎的表比我的短） */
#define HAS_FN(host, fn) \
	((host) != nullptr && (host)->size >= (uint32_t)(offsetof(BpHostApi, fn) + sizeof((host)->fn)))

namespace
{

const BpHostApi* g_host = nullptr;
int g_explosions = 0;
int g_levelStarts = 0;

//把 QString 那套换成 C 字符串：SDK 只认 UTF-8 + 长度
BpStr str(const char* utf8)
{
	BpStr out;
	out.utf8 = utf8;
	out.len = -1;          //-1 = 按 \0 结尾读
	return out;
}

void say(const char* text)
{
	if (g_host && g_host->log) { g_host->log(BP_LOG_INFO, str(text)); }
}

//把引擎给的 BpStr 拷进本地缓冲区（它只在本次回调期间有效，要留就得自己复制）
void copyStr(const BpStr& text, char* out, std::size_t capacity)
{
	if (!out || capacity == 0) { return; }
	out[0] = '\0';
	if (!text.utf8) { return; }

	if (text.len < 0)
	{
		std::snprintf(out, capacity, "%s", text.utf8);
		return;
	}
	const std::size_t count = (static_cast<std::size_t>(text.len) < capacity - 1)
		? static_cast<std::size_t>(text.len) : capacity - 1;
	std::memcpy(out, text.utf8, count);
	out[count] = '\0';
}

// ---------- 事件回调（在游戏线程被调用；payload 只在本次回调内有效） ----------

void BP_CALL on_bomb_exploded(const BpGameEvent* event, void* /*user*/)
{
	++g_explosions;

	//看看这次炸在哪儿（老引擎可能给的是更小的结构体，所以先看 size）
	if (event->payload != nullptr)
	{
		const BpEventExplosion* boom = static_cast<const BpEventExplosion*>(event->payload);
		if (boom->size >= sizeof(BpEventExplosion))
		{
			char line[128];
			std::snprintf(line, sizeof(line), "第 %d 次爆炸：(%d, %d) 威力 %d",
						  g_explosions, boom->row, boom->col, boom->range);
			say(line);
		}
	}

	//用引擎给的随机流抽个数（**不能**自己调 rand：那样联机各端会不同步）
	if (g_host && g_host->rng_range)
	{
		const int roll = g_host->rng_range(1, 7);
		if (roll == 1) { say("运气不错（随机流抽到 1）"); }
	}

	//炸了 3 次就请求暂停：写请求都是"请求"，引擎保留裁决权，所以我们检查返回码
	if (g_explosions == 3 && g_host && g_host->request_pause)
	{
		const BpResult result = g_host->request_pause(1);
		say(result == BP_OK ? "已请求暂停（引擎同意了）" : "请求暂停被拒绝");
	}
}

void BP_CALL on_level_start(const BpGameEvent* event, void* /*user*/)
{
	++g_levelStarts;

	//只读查询：棋盘多大、场上有几个实体
	if (g_host && g_host->board_width && g_host->board_height)
	{
		char line[160];
		std::snprintf(line, sizeof(line), "第 %d 关开始：棋盘 %dx%d，场上实体 %d 个",
					  g_levelStarts,
					  g_host->board_width(), g_host->board_height(),
					  g_host->entity_count ? g_host->entity_count() : -1);
		say(line);
	}

	//0.1.1 的关卡载荷：能拿到"第几关"（老引擎给的是更小的结构体，所以先看 size）
	if (event->payload != nullptr)
	{
		const BpEventLevel* info = static_cast<const BpEventLevel*>(event->payload);
		if (info->size >= sizeof(BpEventLevel))
		{
			char line[128];
			std::snprintf(line, sizeof(line), "关卡载荷：第 %d 关，victory=%d", info->level, info->victory);
			say(line);
		}
	}

	//0.1.1 的炸弹快照：out = NULL 时只问"现在有几枚"
	if (HAS_FN(g_host, bombs_snapshot))
	{
		char line[96];
		std::snprintf(line, sizeof(line), "场上炸弹 %d 枚", g_host->bombs_snapshot(nullptr, 0));
		say(line);
	}
}

void BP_CALL on_item_used(const BpGameEvent* event, void* /*user*/)
{
	if (event->payload == nullptr) { return; }

	const BpEventDrop* drop = static_cast<const BpEventDrop*>(event->payload);
	if (drop->size < sizeof(BpEventDrop)) { return; }

	char name[96];
	copyStr(drop->itemName, name, sizeof(name));

	char line[160];
	std::snprintf(line, sizeof(line), "道具被使用：%s", name);
	say(line);
}

void BP_CALL on_enemy_damaged(const BpGameEvent* event, void* /*user*/)
{
	if (event->payload == nullptr) { return; }

	const BpEventDamage* damage = static_cast<const BpEventDamage*>(event->payload);
	if (damage->size < sizeof(BpEventDamage)) { return; }

	char line[128];
	std::snprintf(line, sizeof(line), "敌人受伤：掉 %d 点血，剩余 %d",
				  damage->amount, damage->livesLeft);
	say(line);
}

void BP_CALL on_level_result(const BpGameEvent* event, void* /*user*/)
{
	const char* what = (event->type == BP_EVENT_LEVEL_WON) ? "过关" : "失败";
	if (event->payload == nullptr)
	{
		say(what);
		return;
	}

	const BpEventLevel* info = static_cast<const BpEventLevel*>(event->payload);
	if (info->size < sizeof(BpEventLevel))
	{
		say(what);
		return;
	}

	char line[128];
	std::snprintf(line, sizeof(line), "关卡结束：%s（第 %d 关，victory=%d）",
				  what, info->level, info->victory);
	say(line);
}

// ============================================================================
// 0.1.2 的"模式"：一个很小的竞技场模式
//
// 它演示三件事：怎么接管地图、怎么接管敌人 AI、怎么接管胜负。
// 真正写模式时这三件都是**可选**的 —— 只声明想接管的那几件，其余交给引擎。
// ============================================================================
int g_arenaRuns = 0;
int g_thinkCalls = 0;

// 1) 地图：一片空场 + 一条按种子摆的砖墙，玩家在中间，四角各一个敌人
int32_t BP_CALL on_generate_map(const BpMapGenRequest* req, BpMapGenResult* out)
{
	if (!g_host || !req || !out) { return 0; }

	const int rows = req->rows;
	const int cols = req->cols;
	const uint32_t seed = req->seed;

	// 两条斜着的砖墙（位置由种子决定：同种子必须同图，联机/重演都靠这一条）
	for (int i = 2; i < rows - 2; ++i)
	{
		const int col = 3 + static_cast<int>((seed + static_cast<uint32_t>(i)) % 3u);
		g_host->set_tile(i, col, BP_TILE_BRICK);
	}

	// 四角各放一个 2 血的敌人
	const int spots[4][2] = { { 2, 2 }, { 2, cols - 3 }, { rows - 3, 2 }, { rows - 3, cols - 3 } };
	int spawned = 0;
	for (int i = 0; i < 4; ++i)
	{
		if (g_host->spawn_enemy(spots[i][0], spots[i][1], 2) == BP_OK) { ++spawned; }
	}
	++g_arenaRuns;

	char line[96];
	std::snprintf(line, sizeof(line), "模式：地图已铺好（%d 个敌人，第 %d 局）", spawned, g_arenaRuns);
	say(line);

	out->ok = 1;
	out->playerStartRow = rows / 2;
	out->playerStartCol = cols / 2;
	return 1;
}

// 2) 敌人 AI：朝玩家走（很简单，但足以证明"接管"是真的）
int32_t BP_CALL on_enemy_think(const BpEnemyThinkRequest* req, BpEnemyThinkResult* out)
{
	if (!g_host || !req || !out) { return 0; }

	BpPlayerState me;
	me.size = sizeof(BpPlayerState);
	if (g_host->player_state(0, &me) != 0) { return 0; }

	// 先横向靠近、再纵向靠近；撞墙时引擎会原地不动，下一步会自动换轴
	int dir = -1;
	if (me.col != req->col)      { dir = (me.col > req->col) ? 3 : 2; }
	else if (me.row != req->row) { dir = (me.row > req->row) ? 1 : 0; }

	out->acted = 1;
	out->direction = dir;
	// 贴到玩家身边就放一枚炸弹（引擎仍会检查"这一格能不能放"）
	out->placeBomb = (me.col == req->col && me.row == req->row) ? 1 : 0;

	// 每 120 次（约两秒）报一句：证明钩子真的每步都在被调（调试用，正式模组别这么写）
	if (++g_thinkCalls % 120 == 0)
	{
		char line[128];
		std::snprintf(line, sizeof(line), "AI：#%d 号敌人正在追你（已问 %d 次）",
					  req->enemyId, g_thinkCalls);
		say(line);
	}
	return 1;
}

// 3) 胜负：敌人清空 = 赢；玩家没命了 = 输（引擎的默认判定已被关掉）
int32_t BP_CALL on_check_win(BpWinResult* out)
{
	if (!g_host || !out) { return 0; }

	BpPlayerState me;
	me.size = sizeof(BpPlayerState);
	if (g_host->player_state(0, &me) == 0 && me.lives <= 0)
	{
		out->decided = 1;
		out->victory = 0;
		out->reason = str("你被追上了：这个模式就这样结束");
		say("胜负：玩家没命了 -> 失败");
		return 1;
	}

	if (g_host->enemies_snapshot(nullptr, 0) == 0)
	{
		out->decided = 1;
		out->victory = 1;
		out->reason = str("追兵清空了，你赢了这个模式");
		say("胜负：敌人清空 -> 胜利");
		return 1;
	}
	return 0;      //还没结论
}

void BP_CALL on_mode_start(BpStr modeId)
{
	char id[64];
	copyStr(modeId, id, sizeof(id));

	char text[160];
	std::snprintf(text, sizeof(text), "模式开始：%s（接管地图 / AI / 胜负）", id);
	say(text);
}

void BP_CALL on_mode_stop(BpStr modeId)
{
	char id[64];
	copyStr(modeId, id, sizeof(id));

	char text[96];
	std::snprintf(text, sizeof(text), "模式结束：%s", id);
	say(text);
}

// ---------- 生命周期 ----------

BpResult BP_CALL mod_init(const BpModInfo* info, const BpHostApi* host)
{
	g_host = host;

	//0) 头文件对不上就别硬上：buildId 是 ABI 版本与指针宽度的指纹（契约 §4.3）
	if (info->buildId != BP_BUILD_ID)
	{
		if (host->log) { host->log(BP_LOG_ERROR, str("SDK 头文件与引擎不一致，拒绝加载")); }
		return -1;
	}

	//1) 能力查询：新引擎才有我们想要的功能，先问再用
	if (!host->has_feature(str("event.subscribe")) || !host->has_feature(str("world.read")))
	{
		if (host->log) { host->log(BP_LOG_WARN, str("引擎不支持事件订阅/只读查询，跳过")); }
		return -2;
	}

	//2) 只读查询：开局前也能问棋盘尺寸（模组自己的地图工具会用得上）
	char line[192];
	std::snprintf(line, sizeof(line), "init 完成（模组目录：%.80s；棋盘 %dx%d）",
				  info->modDir.utf8 ? info->modDir.utf8 : "?",
				  host->board_width(), host->board_height());
	say(line);

	//3) 0.1.1 追加的能力：既问能力名，也用 size 确认函数指针真的够得到
	std::snprintf(line, sizeof(line), "能力：bombs=%d meta=%d players=%d",
				  host->has_feature(str("world.read.bombs")),
				  host->has_feature(str("world.read.meta")),
				  host->has_feature(str("world.read.players")));
	say(line);

	if (HAS_FN(host, level_index) && HAS_FN(host, phase) && HAS_FN(host, step_index)
		&& HAS_FN(host, player_count))
	{
		std::snprintf(line, sizeof(line), "世界信息：第 %d 关，阶段 %d，第 %d 步，玩家 %d 人",
					  host->level_index(), host->phase(), host->step_index(), host->player_count());
		say(line);
	}

	//4) 订阅事件
	host->subscribe(BP_EVENT_BOMB_EXPLODED, &on_bomb_exploded, nullptr);
	host->subscribe(BP_EVENT_LEVEL_START, &on_level_start, nullptr);
	host->subscribe(BP_EVENT_ITEM_USED, &on_item_used, nullptr);
	host->subscribe(BP_EVENT_ENEMY_DAMAGED, &on_enemy_damaged, nullptr);
	host->subscribe(BP_EVENT_LEVEL_WON, &on_level_result, nullptr);
	host->subscribe(BP_EVENT_LEVEL_LOST, &on_level_result, nullptr);
	say("已订阅：爆炸、关卡开始、使用道具、敌人受伤、过关、失败");

	//5) 注册一个模式（0.1.2）：它出现在「开始游戏 → 选择模式」里
	if (HAS_FN(host, register_mode) && host->has_feature(str("mode.register")))
	{
		BpModeDesc mode{};
		mode.size = sizeof(BpModeDesc);
		mode.id = str("demo_arena");
		mode.name = str("示例：竞技场");
		mode.hint = str("四个追兵、一片空场；由模组自己铺地图、决定敌人怎么走与怎么算赢");
		mode.description = str("这是 hello_native 附带的最小模式：演示地图生成、敌人 AI、胜负条件三个钩子。");
		mode.kind = BP_MODE_KIND_HOOKS;
		mode.flags = BP_MODE_FLAG_SINGLE | BP_MODE_FLAG_TAKE_OVER_MAP
				   | BP_MODE_FLAG_TAKE_OVER_AI | BP_MODE_FLAG_TAKE_OVER_WIN;
		mode.playerLives = 5;       //这个模式比关卡宽松一点
		mode.timeLimitSeconds = 0.0;

		const BpResult registered = host->register_mode(&mode);
		say(registered == BP_OK ? "已注册模式：demo_arena" : "模式注册失败（引擎不支持？）");
	}

	return 0;
}

void BP_CALL mod_shutdown(void)
{
	//注意：崩溃或强杀时不会调用这里，别把关键保存放在这（契约 §8）
	say("shutdown");
}

void BP_CALL mod_tick(double /*dt*/)
{
	//每逻辑步调用一次（dt 恒为 1/60）。这里故意什么都不做：
	//回调越少，模组对帧率的影响越小 —— 事件能解决的问题不要用轮询
}

const BpModApi g_api = {
	sizeof(BpModApi),
	&mod_init,
	&mod_shutdown,
	&mod_tick,
	&on_generate_map,   //0.1.2：接管地图
	&on_enemy_think,    //0.1.2：接管敌人 AI
	&on_check_win,      //0.1.2：接管胜负
	&on_mode_start,     //0.1.2：模式开始
	&on_mode_stop       //0.1.2：模式结束（末尾追加）
};

} // namespace

//整个 DLL 唯一的导出符号
extern "C" BP_EXPORT const BpModApi* BP_CALL bp_mod_entry(const char* hostApiVersion,
														  const BpHostApi* host)
{
	//版本不合就返回 NULL：这是契约里留给作者的"优雅拒绝"，引擎会记下来而不是崩
	if (!hostApiVersion || !host) { return nullptr; }
	if (std::strcmp(hostApiVersion, BP_MOD_API_VERSION) != 0) { return nullptr; }

	g_host = host;
	return &g_api;
}
