/* ============================================================================
 * BlastPioneer 原生模组 C ABI —— 模组作者唯一需要 include 的头文件
 *
 * 契约文档：docs/原生模组ABI契约.md（结构与规则以那里为准）
 *
 * 设计目标（决定了下面所有"看起来不必要"的写法）：
 *   1. 多语言：纯 C + 定宽整数 + 只传结构体指针 + 字符串带长度
 *      -> C / C++ / Rust / Zig / C# / Go / Python(ctypes) 都能直接消费
 *   2. 加法式演进：每个结构体首字段是 size，新字段只能追加在末尾
 *      -> 引擎加功能时，已经编译好的老模组不用重编
 *   3. 不侵入引擎：模组拿不到引擎对象的指针，只能调这张函数表；
 *      所有写操作都是"请求"，引擎保留最终裁决权
 *
 * 三条硬规则（违反会崩，不是"可能有问题"）：
 *   · 不跨边界分配内存：跨边界的内存一律 host->alloc，并由 host->free_ 释放
 *   · 不跨边界抛异常：模组内部随便抛，但必须在自己的函数里 catch 干净再返回错误码
 *   · 不保存引擎给你的指针：快照只在本次回调期间有效，要留数据请自己复制
 * ========================================================================== */

#ifndef BP_MOD_API_H
#define BP_MOD_API_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * 版本：两个号，各管一件事（契约 §2）
 *
 *   BP_MOD_API_VERSION   接口版本（字符串）。
 *                        与 mod.json 的 apiVersion 是同一个号，管"声明契约"：
 *                        mod.json 字段、features 能力名、数据覆盖格式。
 *                        0.1.0 -> 0.1.1  加法式：末尾追加函数/结构体字段/事件号
 *                        0.1.1 -> 0.1.2  加法式：把 0.1.0 预留的槽位填成真正的函数
 *                                        （模式注册与三个钩子；布局与偏移一个字节都没变）
 *                        0.1.2 -> 0.2.0  破坏性：改签名、删函数、改既有字段含义
 *
 *   BP_MOD_ABI_VERSION   C ABI 的二进制版本（整数）。
 *                        只有破坏性 ABI 改动才 +1；末尾追加**不**动它。
 * ------------------------------------------------------------------------- */
#define BP_MOD_API_VERSION "0.1.2"

/* 破坏性 ABI 改动才 +1（末尾追加函数/字段不算，见 BP_BUILD_ID 的说明） */
#define BP_MOD_ABI_VERSION 1u

/* 调用约定：Win32 下显式 __cdecl，x64 下为空。
 * BP_MOD_NO_EXPORT：只做编译期契约检查（例如引擎侧的测试）时定义它，
 * 于是这份头文件可以随便 include，而不会凭空声明一个"要导出的符号" */
#if defined(_WIN32) || defined(_WIN64)
#  define BP_CALL __cdecl
#  if defined(BP_MOD_NO_EXPORT)
#    define BP_EXPORT
#  else
#    define BP_EXPORT __declspec(dllexport)
#  endif
#else
#  define BP_CALL
#  define BP_EXPORT __attribute__((visibility("default")))
#endif

/* 返回码：0 = 成功，负数 = 失败 */
typedef int32_t BpResult;

#define BP_OK                 0
#define BP_ERR_INVALID_ARG   (-1)   /* 参数非法：空指针、越界、size 不对 */
#define BP_ERR_UNSUPPORTED   (-2)   /* 引擎不支持这个能力（先问 has_feature） */
#define BP_ERR_NOT_READY     (-3)   /* 时机不对（例如在大厅里请求生成实体） */
#define BP_ERR_REJECTED      (-4)   /* 引擎校验没过（越界、超上限、游戏已结束） */
#define BP_ERR_QUOTA         (-5)   /* 配额用尽（本 tick 命令数/实体数） */
#define BP_ERR_REENTRANT     (-6)   /* 重入：在回调里调了禁止的函数 */
#define BP_ERR_DISABLED      (-7)   /* 该能力被玩家设置关闭 */
#define BP_ERR_INTERNAL      (-8)   /* 引擎内部错误 */

/* 日志等级 */
#define BP_LOG_DEBUG 0
#define BP_LOG_INFO  1
#define BP_LOG_WARN  2
#define BP_LOG_ERROR 3

/* ---------------------------------------------------------------------------
 * 基础类型
 * ------------------------------------------------------------------------- */

/* 字符串：UTF-8 + 显式长度。len < 0 表示"按 \0 结尾读"。
 * 为什么不用裸 const char*：不是所有语言都用 \0 结尾，而且带长度能表达内嵌 \0。 */
typedef struct BpStr
{
    const char* utf8;
    int32_t     len;
} BpStr;

/* 棋盘格的类型（与引擎的 TileType 对应，数值一旦发布就不能改；只能追加） */
enum
{
    BP_TILE_EMPTY     = 0,
    BP_TILE_WALL      = 1,
    BP_TILE_BRICK     = 2,
    BP_TILE_EXPLOSION = 3    /* 0.1.1 追加：爆炸痕还没消退的格子 */
};

/* 游戏阶段（0.1.1 追加）：与引擎的 bp::Phase 一一对应 */
enum
{
    BP_PHASE_READY   = 0,
    BP_PHASE_RUNNING = 1,
    BP_PHASE_PAUSED  = 2,
    BP_PHASE_VICTORY = 3,
    BP_PHASE_DEFEAT  = 4
};

/* 事件类型（只追加；引擎不会发的类型永远不会回调你） */
enum
{
    BP_EVENT_ANY           = 0,   /* 订阅时用：表示"全部事件" */
    BP_EVENT_LEVEL_START   = 1,
    BP_EVENT_BOMB_PLACED   = 2,
    BP_EVENT_BOMB_EXPLODED = 3,
    BP_EVENT_ENEMY_DIED    = 4,
    BP_EVENT_PLAYER_DAMAGED= 5,
    BP_EVENT_PLAYER_DIED   = 6,
    BP_EVENT_DROP_SPAWNED  = 7,
    BP_EVENT_DROP_PICKED   = 8,
    BP_EVENT_LEVEL_WON     = 9,
    BP_EVENT_LEVEL_LOST    = 10,
    /* ---- 0.1.1 追加 ---- */
    BP_EVENT_ENEMY_DAMAGED = 11,  /* 敌人受伤（不再和"死亡"混在一起） */
    BP_EVENT_ITEM_USED     = 12   /* 道具被使用（不再和"拾取"混在一起） */
};

/* 事件载荷：type 决定用哪个结构体读；
 * 所有载荷首字段都是 size —— 引擎按你的 size 决定给多少字段（老模组读到老字段）。
 * 载荷都声明成"前四个字段 = {size, entityId, row, col}"，所以 0.1.0 时期
 * 按 BpEventEntity 读的老模组，在 0.1.1 的新载荷上仍然读得到合理值（契约 §4.3）。 */
typedef struct BpEventBomb
{
    uint32_t size;
    int32_t  row;
    int32_t  col;
    int32_t  playerIndex;   /* -1 = 敌人放的 */
    int32_t  range;
} BpEventBomb;

typedef struct BpEventExplosion
{
    uint32_t size;
    int32_t  row;
    int32_t  col;
    /* 0.1.0 的设想字段，但引擎**不报告**"炸掉几块砖"（砖块清掉之后就算不出来了），
     * 所以本版本恒为 0；保留它是为了不破坏按老布局读的模组 */
    int32_t  destroyedBricks;
    /* 0.1.1 追加：爆炸范围（向四个方向各延伸几格）—— 这才是引擎真正给得出的信息 */
    int32_t  range;
} BpEventExplosion;

typedef struct BpEventEntity
{
    uint32_t size;
    int32_t  entityId;
    int32_t  row;
    int32_t  col;
} BpEventEntity;

/* 掉落类事件（DROP_SPAWNED / DROP_PICKED / ITEM_USED）的载荷：
 * 它就是 BpEventEntity 后面追加了一个道具名（所以老读法依然成立） */
typedef struct BpEventDrop
{
    uint32_t size;
    int32_t  entityId;      /* -1（掉落物没有稳定 id） */
    int32_t  row;
    int32_t  col;
    BpStr    itemName;      /* 0.1.1 新增：道具名 */
} BpEventDrop;

/* 伤害事件（PLAYER_DAMAGED / ENEMY_DAMAGED）的载荷 */
typedef struct BpEventDamage
{
    uint32_t size;
    int32_t  playerIndex;   /* >= 0 = 玩家编号；-1 = 敌人 */
    int32_t  amount;
    int32_t  livesLeft;     /* 玩家：剩余生命；敌人：剩余血量 */
} BpEventDamage;

/* 关卡类事件（LEVEL_START / LEVEL_WON / LEVEL_LOST）的载荷：
 * 同样是 BpEventEntity 后面追加字段 */
typedef struct BpEventLevel
{
    uint32_t size;
    int32_t  entityId;      /* -1 */
    int32_t  row;           /* -1（关卡事件不对应格子） */
    int32_t  col;           /* -1 */
    int32_t  level;         /* 0.1.1 新增：第几关 */
    int32_t  victory;       /* 0.1.1 新增：仅 WON/LOST 有意义（1/0） */
} BpEventLevel;

typedef struct BpGameEvent
{
    uint32_t    size;       /* = sizeof(BpGameEvent) */
    uint32_t    type;       /* BP_EVENT_* */
    const void* payload;    /* 按 type 转成上面某个结构体；只在本次回调期间有效 */
} BpGameEvent;

/* 只读快照：敌人 / 掉落物 / 玩家 / 炸弹 */
typedef struct BpEnemyInfo
{
    uint32_t size;
    int32_t  entityId;      /* 同一帧内的下标（引擎不保证跨帧稳定，见契约 §6） */
    int32_t  row;
    int32_t  col;
    int32_t  hp;
    int32_t  maxHp;
    int32_t  alive;
} BpEnemyInfo;

typedef struct BpDropInfo
{
    uint32_t size;
    int32_t  row;
    int32_t  col;
    BpStr    itemName;
} BpDropInfo;

typedef struct BpPlayerState
{
    uint32_t size;
    int32_t  row;
    int32_t  col;
    int32_t  lives;
    int32_t  maxLives;
    int32_t  bombRange;
    int32_t  maxBombs;
    int32_t  ghost;         /* 1 = 电子幽灵态（可穿墙） */
} BpPlayerState;

/* 0.1.1 新增：一枚炸弹的状态（引信 / 威力 / 归属） */
typedef struct BpBombInfo
{
    uint32_t size;
    int32_t  row;
    int32_t  col;
    int32_t  ownerId;          /* 放置者编号；敌人放的炸弹为 -1 */
    int32_t  range;            /* 爆炸威力 */
    double   remainingSeconds; /* 剩余引信（秒） */
    double   fuseTotal;        /* 总引信（秒） */
} BpBombInfo;

/* ---------------------------------------------------------------------------
 * 0.1.2 新增：模式（在「开始游戏 → 选择模式」里出现的一张卡片）
 *
 * 一个模式是"一套玩法"，由模组注册。模式**不是关卡**：
 * 关卡流程由模组自己设计（它内部想怎么分阶段都行），游戏不在中间插一层界面。
 * ------------------------------------------------------------------------- */

/* 模式类型 */
enum
{
    BP_MODE_KIND_HOOKS      = 0,  /* 引擎跑确定性内核（移动/炸弹/爆炸/掉落），模组接管规则 */
    BP_MODE_KIND_OWN_ENGINE = 1   /* 模组全接管每个逻辑步 —— **本版本未实现，注册会被拒绝** */
};

/* 模式能力位（告诉引擎"我要接管哪些部分"） */
enum
{
    BP_MODE_FLAG_SINGLE       = 1 << 0,  /* 允许单人玩（默认就有） */
    BP_MODE_FLAG_MULTIPLAYER  = 1 << 1,  /* 允许联机：主机跑规则、客户端镜像 */
    BP_MODE_FLAG_OWN_NETWORK  = 1 << 2,  /* 联机由**模组自己**实现：游戏只在本机跑这个模式 */
    BP_MODE_FLAG_TAKE_OVER_MAP = 1 << 3, /* 我生成地图（on_generate_map） */
    BP_MODE_FLAG_TAKE_OVER_AI  = 1 << 4, /* 我决定敌人怎么动（on_enemy_think） */
    BP_MODE_FLAG_TAKE_OVER_WIN = 1 << 5, /* 我判定胜负（on_check_win） */
    /* 没声明的部分按引擎默认走 —— 于是"只改一件事"的模式也能写得很小 */
};

typedef struct BpModeDesc
{
    uint32_t size;
    BpStr    id;               /* 在**本模组内**唯一（全局 id = modId/modeId） */
    BpStr    name;             /* 卡片标题 */
    BpStr    hint;             /* 卡片副标题（一句话，玩家看的） */
    BpStr    description;      /* 详情（可空） */
    int32_t  kind;             /* BP_MODE_KIND_* */
    int32_t  flags;            /* BP_MODE_FLAG_* 的按位或 */
    /* 0 = 用引擎默认值。下面的字段让"新手友好/硬核"这类变体不用自己实现 */
    int32_t  playerLives;
    int32_t  playerMaxBombs;
    int32_t  playerBombRange;
    double   timeLimitSeconds; /* 0 = 不限时 */
} BpModeDesc;

/* ---- 钩子 1：地图生成 ----
 * 引擎问"这一局的地图怎么铺"。模组用 host->set_tile 铺格子、用 host->spawn_enemy 放敌人，
 * 然后填好玩家出生点、返回 1。
 * 返回 0 = 我没有意见（引擎用默认随机地图）。 */
typedef struct BpMapGenRequest
{
    uint32_t size;
    int32_t  level;        /* 关卡号（模式可以只用 1，也可以自己推进） */
    int32_t  rows;         /* 棋盘尺寸（引擎的编译期常量，给模组省一次查询） */
    int32_t  cols;
    uint32_t seed;         /* 本局种子：同一模式下同种子必须产出同一张图 */
    int32_t  brickCount;   /* 引擎建议的砖块数（模组可无视） */
    int32_t  enemyCount;   /* 引擎建议的敌人数（模组可无视） */
} BpMapGenRequest;

typedef struct BpMapGenResult
{
    uint32_t size;
    int32_t  ok;             /* 1 = 我铺好了；0 = 交回引擎默认 */
    int32_t  playerStartRow; /* 玩家出生点（应在可走格上；越界会被引擎拒绝） */
    int32_t  playerStartCol;
} BpMapGenResult;

/* ---- 钩子 2：敌人思考 ----
 * 每个逻辑步、每个活着的敌人问一次。acted=1 表示"这一步按我说的办"：
 * 引擎负责把敌人朝 direction 走一步（撞墙就不走）、必要时放一枚敌人炸弹。
 * 位置与碰撞仍由引擎负责 —— 所以模组的 AI 不可能把敌人送进墙里。 */
typedef struct BpEnemyThinkRequest
{
    uint32_t size;
    int32_t  enemyId;      /* 引擎给的稳定 id（同一局内不变） */
    int32_t  row;
    int32_t  col;
    double   x;            /* 连续坐标：(x = 列, y = 行) */
    double   y;
    int32_t  hp;
    int32_t  maxHp;
    int32_t  stepIndex;    /* 本局第几个逻辑步 */
    double   frameTime;    /* 本步时长（恒为 1/60） */
} BpEnemyThinkRequest;

typedef struct BpEnemyThinkResult
{
    uint32_t size;
    int32_t  acted;        /* 0 = 交回引擎默认；1 = 按下面执行 */
    int32_t  direction;    /* 0 上 1 下 2 左 3 右；-1 = 原地不动 */
    int32_t  placeBomb;    /* 1 = 在当前格放一枚敌人炸弹（仍受引擎的规则约束） */
} BpEnemyThinkResult;

/* ---- 钩子 3：胜负 ----
 * 每个逻辑步问一次。decided=1 表示这局结束了；reason 会显示给玩家。
 * 想看世界状态就调 host 的只读查询 —— 不在这里传快照，是为了不让"引擎内部结构"
 * 变成 ABI 的一部分。 */
typedef struct BpWinResult
{
    uint32_t size;
    int32_t  decided;   /* 0 = 交回引擎默认；1 = 用下面的结论 */
    int32_t  victory;   /* 1 胜 / 0 负 */
    BpStr    reason;    /* 可空：显示给玩家的一句话（只在本次回调期间有效） */
} BpWinResult;

/* ---------------------------------------------------------------------------
 * 模组声明 + ABI 握手
 * ------------------------------------------------------------------------- */

/* 运行期的兼容性握手：只有**破坏性** ABI 改动才需要动 BP_MOD_ABI_VERSION。
 *
 * 为什么不再把 sizeof(BpHostApi)/sizeof(BpModInfo) 混进来（0.1.0 的做法，0.1.1 修正）：
 *   本契约最核心的承诺是"**末尾追加**函数/字段不破坏已经编译好的老模组"，
 *   而 sizeof 一追加就变 —— 那等于把每一次加法式改动都变成破坏性改动，
 *   老模组会被自己的 buildId 检查挡在门外。所以两件事分开管：
 *     · 运行期：BP_BUILD_ID = ABI 版本 + 指针宽度（管"版本/平台对不对得上"）；
 *     · 编译期：文件末尾的 static_assert 钉住 0.1.0 已经承诺过的字段偏移
 *       （管"改了布局却忘了提版本"，这是 §4.3 真正想要的那道保险）。
 */
#define BP_BUILD_ID ( \
      (uint64_t)BP_MOD_ABI_VERSION * 1000003ull \
    + (uint64_t)sizeof(void*) )

typedef struct BpModInfo
{
    uint32_t size;              /* = sizeof(BpModInfo) */
    uint32_t abiVersionMajor;   /* 0 */
    uint32_t abiVersionMinor;   /* 1 */
    uint32_t pointerSize;       /* sizeof(void*)：32/64 位不匹配要当场发现 */
    uint32_t endianLittle;      /* 1 = 小端 */
    uint64_t buildId;           /* = BP_BUILD_ID */
    BpStr    id;                /* 建议与 mod.json 的 id 一致 */
    BpStr    name;
    BpStr    version;
    BpStr    modDir;            /* 模组自己的目录（绝对路径）：读自己的数据文件用这个 */
} BpModInfo;

/* ---------------------------------------------------------------------------
 * 宿主函数表：引擎给模组的能力
 *
 * 老引擎上后追加的函数指针**不在你的 size 范围内** —— 用 host->size 判断：
 *     int have_bombs = host->size >= (uint32_t)(offsetof(BpHostApi, bombs_snapshot)
 *                                              + sizeof(host->bombs_snapshot));
 * 不要只看 has_feature：能力名是 0.1.1 才有的，而 size 是运行期真话。
 * ------------------------------------------------------------------------- */

/* 下面两张表是 `struct X` 而不是 typedef：为了让**纯 C**（以及 bindgen / cgo）
 * 也能用裸名引用它们，这里先补一行 typedef —— 少了它，C 代码里 `BpHostApi` 不是类型名 */
typedef struct BpHostApi BpHostApi;
typedef struct BpModApi  BpModApi;

struct BpHostApi
{
    uint32_t size;              /* = sizeof(BpHostApi) */

    /* ---- 能力查询（加法式演进的关键） ---- */
    int32_t  (BP_CALL *has_feature)(BpStr name);
    uint32_t (BP_CALL *abi_minor)(void);        /* 引擎的 ABI 次版本号 */

    /* ---- 基础服务 ---- */
    void  (BP_CALL *log)(int32_t level, BpStr message);
    void* (BP_CALL *alloc)(size_t bytes);
    void  (BP_CALL *free_)(void* p);

    /* ---- 只读查询 ----
     * 快照类函数的约定（两段式，避免跨边界分配）：
     *   out == NULL 时只返回"当前有多少个"；
     *   否则把最多 capacity 个写进 out，返回**真实数量**（可能大于 capacity，表示被截断）。
     * 所有返回值只在**本次回调期间**有效；要留就自己复制。 */
    int32_t (BP_CALL *board_width)(void);
    int32_t (BP_CALL *board_height)(void);
    int32_t (BP_CALL *tile_at)(int32_t row, int32_t col);
    int32_t (BP_CALL *bomb_count_at)(int32_t row, int32_t col);
    int32_t (BP_CALL *enemies_snapshot)(BpEnemyInfo* out, int32_t capacity);
    int32_t (BP_CALL *drops_snapshot)(BpDropInfo* out, int32_t capacity);
    int32_t (BP_CALL *player_state)(int32_t playerIndex, BpPlayerState* out);
    int32_t (BP_CALL *entity_count)(void);

    /* ---- 事件订阅 ----
     * type = BP_EVENT_ANY 表示全部；回调在**游戏线程**被调用；多次订阅同一个 type 会全部收到。 */
    BpResult (BP_CALL *subscribe)(uint32_t type,
                                  void (BP_CALL *cb)(const BpGameEvent*, void*), void* user);

    /* ---- 写操作：全部是"请求"，引擎保留最终裁决权 ----
     * L2 真正开放的只有下面两个（request_pause / apply_item_effect）。
     * 其余写请求本版本一律返回 BP_ERR_UNSUPPORTED，且 has_feature 返回 0 ——
     * 契约里最硬的一条是"**绝不调了没反应**"，所以不支持的必须如实报不支持。 */
    BpResult (BP_CALL *set_var_i32)(BpStr name, int32_t value);
    BpResult (BP_CALL *set_var_f64)(BpStr name, double value);
    BpResult (BP_CALL *spawn_enemy)(int32_t row, int32_t col, int32_t hp);
    BpResult (BP_CALL *remove_entity)(int32_t entityId);
    BpResult (BP_CALL *damage_entity)(int32_t entityId, int32_t amount);
    /* amount 为伤害点数；单人模式 playerIndex 传 0，多人按 0..player_count-1 */
    BpResult (BP_CALL *damage_player)(int32_t playerIndex, int32_t amount);
    BpResult (BP_CALL *spawn_drop)(int32_t row, int32_t col, BpStr itemName);
    /* 时间倍率：0 = 冻结，1 = 正常，上限 4（超出会被夹住）；只影响推进速度，不改规则 */
    BpResult (BP_CALL *set_time_scale)(double scale);
    BpResult (BP_CALL *request_pause)(int32_t paused);      /* L2 已实现 */
    /* 触发一件**已有**道具的效果（名字或效果 id 都行）：引擎自己的效果注册表说了算 */
    BpResult (BP_CALL *apply_item_effect)(BpStr nameOrEffectId);

    /* ---- 随机数：模组**必须**用它（引擎给每个模组一条独立随机流，保证联机一致） ---- */
    uint32_t (BP_CALL *rng_next)(void);
    int32_t  (BP_CALL *rng_range)(int32_t minInclusive, int32_t maxExclusive);

    /* ---- 模式注册（0.1.2：这里原来是 reserved_mode，0.1.0 起就留着的槽位） ----
     * **只能在 init 里调**。注册的模式会出现在「开始游戏 → 选择模式」里。
     * 校验不过（重名/类型不支持/声明了不会实现的能力）-> 返回错误码并在日志里说明。 */
    BpResult (BP_CALL *register_mode)(const BpModeDesc* desc);

    /* ---- 仍是预留：本版本恒为 NULL，**不要调用** ----
     * 这三个槽位原本打算放"per-mode 的细粒度 setter"；0.1.2 用 BpModeDesc 的字段
     * 加上 set_tile / set_player_start 已经够用，所以没有填。
     * 判断"某个函数在运行期存不存在"请看 host->size，不要假设这些槽位可用。 */
    void*    (BP_CALL *reserved_mapgen)(void);
    void*    (BP_CALL *reserved_ai)(void);
    void*    (BP_CALL *reserved_win)(void);

    /* ---- 0.1.1 追加：世界只读扩展 ---- */
    int32_t  (BP_CALL *level_index)(void);      /* 当前第几关（0 = 还没开局） */
    int32_t  (BP_CALL *phase)(void);            /* BP_PHASE_* */
    int32_t  (BP_CALL *step_index)(void);       /* 本局第几个逻辑步 */
    int32_t  (BP_CALL *player_count)(void);     /* 单人 = 1；多人 = 在场的玩家数；0 = 还没开局 */
    /* 两段式，与其它 *_snapshot 一致。ownerId = -1 表示敌人放的炸弹 */
    int32_t  (BP_CALL *bombs_snapshot)(BpBombInfo* out, int32_t capacity);

    /* ---- 0.1.2 追加：铺地图用的写格子 ----
     * **只在 on_generate_map 期间有效**（其它时候返回 BP_ERR_NOT_READY）。
     * 只能写 空地/墙/砖块；引擎会校验范围（边框那一圈永远是墙）。 */
    int32_t  (BP_CALL *set_tile)(int32_t row, int32_t col, int32_t tile);
    /* 铺地图期间指定玩家出生点；不调就用引擎默认的 (1,1)。
     * 写在墙里会被引擎拒绝并回退 —— 玩家不该因为模组的地图而卡进墙里 */
    int32_t  (BP_CALL *set_player_start)(int32_t row, int32_t col);
};

/* ---------------------------------------------------------------------------
 * 模组函数表：模组给引擎的回调
 * ------------------------------------------------------------------------- */

struct BpModApi
{
    uint32_t size;              /* = sizeof(BpModApi) */

    /* 生命周期：init 返回非 0 -> 拒绝加载并把这个值记进日志 */
    BpResult (BP_CALL *init)(const BpModInfo* info, const BpHostApi* host);
    void     (BP_CALL *shutdown)(void);

    /* 固定步长逻辑回调：dt 恒为 1/60（跟引擎的固定步长一致，不跟渲染帧率） */
    void     (BP_CALL *on_tick)(double dt);

    /* ---------- 0.1.2：模式钩子（这里是 0.1.0 起就留着的三个槽位 + 一个） ----------
     * 只有"本模组注册的模式正在被玩"时才会被调用；没注册模式就永远是 NULL 也行。
     * 每个钩子都返回"我有没有意见"：0 = 交回引擎默认。 */
    int32_t  (BP_CALL *on_generate_map)(const BpMapGenRequest* req, BpMapGenResult* out);
    int32_t  (BP_CALL *on_enemy_think)(const BpEnemyThinkRequest* req, BpEnemyThinkResult* out);
    int32_t  (BP_CALL *on_check_win)(BpWinResult* out);
    void     (BP_CALL *on_mode_start)(BpStr modeId);
    /* 0.1.2 末尾追加：模式结束（胜利/失败/退出都算；崩溃时不会调用） */
    void     (BP_CALL *on_mode_stop)(BpStr modeId);
};

/* ---------------------------------------------------------------------------
 * 唯一导出符号
 *
 * 引擎加载 DLL 后找这个名字，并传入：
 *   hostApiVersion : 引擎的版本字符串（"0.1.2"），\0 结尾
 *   host           : 引擎的函数表
 * 返回：你的函数表，或者 NULL 表示"我不兼容，别加载我"。
 *
 * 注意：这是**优雅拒绝**的机会 —— 版本不合、缺少必需能力时返回 NULL，
 * 引擎会记一条"模组主动拒绝"而不是崩溃。
 * ------------------------------------------------------------------------- */
BP_EXPORT const BpModApi* BP_CALL bp_mod_entry(const char* hostApiVersion, const BpHostApi* host);

/* ---------------------------------------------------------------------------
 * 冻结前缀的编译期保险（契约 §4.3）
 *
 * 结构体大小写错、字段顺序改了，在 C 里**不是编译错误而是运行时崩溃**。
 * 所以把 0.1.0 对外承诺过的偏移量写成 static_assert：谁改了布局却不提
 * BP_MOD_ABI_VERSION，编译就不会通过。
 * 只在 64 位 Windows（本项目的发布平台）上校验具体数值 —— 其它平台的对齐
 * 规则可能不同，那种情况下这些"具体数字"本身就不该被钉死。
 * ------------------------------------------------------------------------- */
#if defined(__cplusplus) && defined(_WIN64)
static_assert(offsetof(BpStr, len) == 8, "BpStr 布局变了：必须提 BP_MOD_ABI_VERSION");
static_assert(offsetof(BpModInfo, buildId) == 24, "BpModInfo 布局变了：必须提 BP_MOD_ABI_VERSION");
static_assert(offsetof(BpModInfo, id) == 32, "BpModInfo 布局变了：必须提 BP_MOD_ABI_VERSION");
static_assert(offsetof(BpModInfo, modDir) == 80, "BpModInfo 布局变了：必须提 BP_MOD_ABI_VERSION");
static_assert(sizeof(BpModInfo) == 96, "BpModInfo 大小变了：必须提 BP_MOD_ABI_VERSION");
static_assert(offsetof(BpGameEvent, payload) == 8, "BpGameEvent 布局变了：必须提 BP_MOD_ABI_VERSION");
static_assert(sizeof(BpGameEvent) == 16, "BpGameEvent 大小变了：必须提 BP_MOD_ABI_VERSION");
/* 宿主函数表：0.1.0 承诺过的偏移量。0.1.1 只在 reserved_win 之后追加，
 * 0.1.2 把 reserved_mode 原地填成 register_mode（类型变了、布局没变），所以这些数字不变 */
static_assert(offsetof(BpHostApi, has_feature) == 8, "函数表表头变了：必须提 BP_MOD_ABI_VERSION");
static_assert(offsetof(BpHostApi, subscribe) == 112, "函数表顺序变了：必须提 BP_MOD_ABI_VERSION");
static_assert(offsetof(BpHostApi, rng_range) == 208, "函数表顺序变了：必须提 BP_MOD_ABI_VERSION");
static_assert(offsetof(BpHostApi, reserved_win) == 240, "函数表顺序变了：必须提 BP_MOD_ABI_VERSION");
/* 模组函数表：init / shutdown / on_tick 必须在最前面 */
static_assert(offsetof(BpModApi, init) == 8, "模组函数表表头变了：必须提 BP_MOD_ABI_VERSION");
static_assert(offsetof(BpModApi, on_tick) == 24, "模组函数表顺序变了：必须提 BP_MOD_ABI_VERSION");

/* 0.1.2 新增的结构体：从这一天起它们也冻结了 */
static_assert(offsetof(BpModeDesc, flags) == 76, "BpModeDesc 布局变了：必须提 BP_MOD_ABI_VERSION");
static_assert(sizeof(BpModeDesc) == 104, "BpModeDesc 大小变了：必须提 BP_MOD_ABI_VERSION");
static_assert(sizeof(BpMapGenRequest) == 28, "BpMapGenRequest 大小变了：必须提 BP_MOD_ABI_VERSION");
static_assert(sizeof(BpMapGenResult) == 16, "BpMapGenResult 大小变了：必须提 BP_MOD_ABI_VERSION");
static_assert(sizeof(BpEnemyThinkRequest) == 56, "BpEnemyThinkRequest 大小变了：必须提 BP_MOD_ABI_VERSION");
static_assert(sizeof(BpEnemyThinkResult) == 16, "BpEnemyThinkResult 大小变了：必须提 BP_MOD_ABI_VERSION");
static_assert(sizeof(BpWinResult) == 32, "BpWinResult 大小变了：必须提 BP_MOD_ABI_VERSION");
#endif

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* BP_MOD_API_H */
