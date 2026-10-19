"""
bp_mod_api.py —— 用 Python 写 BlastPioneer 原生模组时的 ctypes 声明（零依赖，只用标准库）

作者只需要写 `mod.py`，在 `bp_init()` 里把宿主函数表指针收下来，然后：

    import bp_mod_api as api

    HOST = None

    def bp_init(host_ptr, mod_dir):
        global HOST
        HOST = api.Host(host_ptr, mod_dir)      # 校验版本/指针宽度，并把函数表接过来
        HOST.log("你好，引擎")
        HOST.subscribe(api.BP_EVENT_BOMB_EXPLODED, on_boom)
        return 0                                 # 非 0 = 我不兼容，别加载我

约定的函数名（shim 按名字调用；没写的都可以不写）：

    bp_init(host_ptr, mod_dir) -> int          必须：返回 0 表示加载成功
    bp_shutdown()                              可选
    bp_tick(dt)                                可选：每个逻辑步一次（dt 恒为 1/60）
    bp_on_event(type, row, col, amount, text)  可选：订阅后的事件
    bp_generate_map(level, rows, cols, seed) -> int   可选：模式接管地图
    bp_enemy_think(enemy_id, row, col, x, y, hp, max_hp, step, dt) -> int  可选：模式接管敌人 AI
    bp_check_win() -> int                     可选：模式接管胜负（1 胜 / 2 负 / 0 没意见）
    bp_mode_start(mode_id) / bp_mode_stop(mode_id)  可选

这份文件必须与 sdk/bp_mod_api.h **逐字段一致**：结构体与函数表的顺序一个都不能错，
否则会读到错位的指针（那不是报错，是崩）。改动任何一处都要同步三边：
头文件、本文件、`abi_check.py`（它会把偏移量与头文件里的 static_assert 对一遍）。
"""

import ctypes as C
import os

# ---------------------------------------------------------------- 版本与常量

BP_MOD_API_VERSION = b"0.1.2"      # 与头文件的 BP_MOD_API_VERSION 一致
BP_MOD_ABI_VERSION = 1

BP_OK = 0
BP_ERR_INVALID_ARG = -1
BP_ERR_UNSUPPORTED = -2
BP_ERR_NOT_READY = -3
BP_ERR_REJECTED = -4
BP_ERR_QUOTA = -5
BP_ERR_REENTRANT = -6
BP_ERR_DISABLED = -7
BP_ERR_INTERNAL = -8

BP_LOG_DEBUG, BP_LOG_INFO, BP_LOG_WARN, BP_LOG_ERROR = 0, 1, 2, 3

BP_TILE_EMPTY, BP_TILE_WALL, BP_TILE_BRICK, BP_TILE_EXPLOSION = 0, 1, 2, 3

BP_PHASE_READY, BP_PHASE_RUNNING, BP_PHASE_PAUSED, BP_PHASE_VICTORY, BP_PHASE_DEFEAT = 0, 1, 2, 3, 4

BP_EVENT_ANY = 0
BP_EVENT_LEVEL_START = 1
BP_EVENT_BOMB_PLACED = 2
BP_EVENT_BOMB_EXPLODED = 3
BP_EVENT_ENEMY_DIED = 4
BP_EVENT_PLAYER_DAMAGED = 5
BP_EVENT_PLAYER_DIED = 6
BP_EVENT_DROP_SPAWNED = 7
BP_EVENT_DROP_PICKED = 8
BP_EVENT_LEVEL_WON = 9
BP_EVENT_LEVEL_LOST = 10
BP_EVENT_ENEMY_DAMAGED = 11
BP_EVENT_ITEM_USED = 12

BP_MODE_KIND_HOOKS = 0
BP_MODE_KIND_OWN_ENGINE = 1

BP_MODE_FLAG_SINGLE = 1 << 0
BP_MODE_FLAG_MULTIPLAYER = 1 << 1
BP_MODE_FLAG_OWN_NETWORK = 1 << 2
BP_MODE_FLAG_TAKE_OVER_MAP = 1 << 3
BP_MODE_FLAG_TAKE_OVER_AI = 1 << 4
BP_MODE_FLAG_TAKE_OVER_WIN = 1 << 5

# ---------------------------------------------------------------- 结构体


class BpStr(C.Structure):
    """UTF-8 + 显式长度。len < 0 表示按 \\0 结尾读。"""
    _fields_ = [("utf8", C.c_char_p), ("len", C.c_int32)]


class BpModInfo(C.Structure):
    _fields_ = [
        ("size", C.c_uint32),
        ("abiVersionMajor", C.c_uint32),
        ("abiVersionMinor", C.c_uint32),
        ("pointerSize", C.c_uint32),
        ("endianLittle", C.c_uint32),
        ("buildId", C.c_uint64),
        ("id", BpStr),
        ("name", BpStr),
        ("version", BpStr),
        ("modDir", BpStr),
    ]


class BpGameEvent(C.Structure):
    _fields_ = [
        ("size", C.c_uint32),
        ("type", C.c_uint32),
        ("payload", C.c_void_p),
    ]


class BpEventBomb(C.Structure):
    _fields_ = [("size", C.c_uint32), ("row", C.c_int32), ("col", C.c_int32),
                ("playerIndex", C.c_int32), ("range", C.c_int32)]


class BpEventExplosion(C.Structure):
    _fields_ = [("size", C.c_uint32), ("row", C.c_int32), ("col", C.c_int32),
                ("destroyedBricks", C.c_int32), ("range", C.c_int32)]


class BpEventEntity(C.Structure):
    _fields_ = [("size", C.c_uint32), ("entityId", C.c_int32), ("row", C.c_int32), ("col", C.c_int32)]


class BpEventDrop(C.Structure):
    _fields_ = [("size", C.c_uint32), ("entityId", C.c_int32), ("row", C.c_int32),
                ("col", C.c_int32), ("itemName", BpStr)]


class BpEventDamage(C.Structure):
    _fields_ = [("size", C.c_uint32), ("playerIndex", C.c_int32), ("amount", C.c_int32),
                ("livesLeft", C.c_int32)]


class BpEventLevel(C.Structure):
    _fields_ = [("size", C.c_uint32), ("entityId", C.c_int32), ("row", C.c_int32),
                ("col", C.c_int32), ("level", C.c_int32), ("victory", C.c_int32)]


class BpEnemyInfo(C.Structure):
    _fields_ = [("size", C.c_uint32), ("entityId", C.c_int32), ("row", C.c_int32),
                ("col", C.c_int32), ("hp", C.c_int32), ("maxHp", C.c_int32), ("alive", C.c_int32)]


class BpDropInfo(C.Structure):
    _fields_ = [("size", C.c_uint32), ("row", C.c_int32), ("col", C.c_int32), ("itemName", BpStr)]


class BpPlayerState(C.Structure):
    _fields_ = [("size", C.c_uint32), ("row", C.c_int32), ("col", C.c_int32),
                ("lives", C.c_int32), ("maxLives", C.c_int32), ("bombRange", C.c_int32),
                ("maxBombs", C.c_int32), ("ghost", C.c_int32)]


class BpBombInfo(C.Structure):
    _fields_ = [("size", C.c_uint32), ("row", C.c_int32), ("col", C.c_int32),
                ("ownerId", C.c_int32), ("range", C.c_int32),
                ("remainingSeconds", C.c_double), ("fuseTotal", C.c_double)]


class BpModeDesc(C.Structure):
    _fields_ = [
        ("size", C.c_uint32),
        ("id", BpStr),
        ("name", BpStr),
        ("hint", BpStr),
        ("description", BpStr),
        ("kind", C.c_int32),
        ("flags", C.c_int32),
        ("playerLives", C.c_int32),
        ("playerMaxBombs", C.c_int32),
        ("playerBombRange", C.c_int32),
        ("timeLimitSeconds", C.c_double),
    ]


class BpMapGenRequest(C.Structure):
    _fields_ = [("size", C.c_uint32), ("level", C.c_int32), ("rows", C.c_int32),
                ("cols", C.c_int32), ("seed", C.c_uint32), ("brickCount", C.c_int32),
                ("enemyCount", C.c_int32)]


class BpMapGenResult(C.Structure):
    _fields_ = [("size", C.c_uint32), ("ok", C.c_int32),
                ("playerStartRow", C.c_int32), ("playerStartCol", C.c_int32)]


class BpEnemyThinkRequest(C.Structure):
    _fields_ = [("size", C.c_uint32), ("enemyId", C.c_int32), ("row", C.c_int32),
                ("col", C.c_int32), ("x", C.c_double), ("y", C.c_double),
                ("hp", C.c_int32), ("maxHp", C.c_int32), ("stepIndex", C.c_int32),
                ("frameTime", C.c_double)]


class BpEnemyThinkResult(C.Structure):
    _fields_ = [("size", C.c_uint32), ("acted", C.c_int32), ("direction", C.c_int32),
                ("placeBomb", C.c_int32)]


class BpWinResult(C.Structure):
    _fields_ = [("size", C.c_uint32), ("decided", C.c_int32), ("victory", C.c_int32),
                ("reason", BpStr)]


EventCb = C.CFUNCTYPE(None, C.POINTER(BpGameEvent), C.c_void_p)


class BpHostApi(C.Structure):
    """宿主函数表。字段顺序**必须**与 bp_mod_api.h 完全一致。"""
    pass


class BpModApi(C.Structure):
    """模组函数表。字段顺序**必须**与 bp_mod_api.h 完全一致。"""
    pass


BpHostApi._fields_ = [
    ("size", C.c_uint32),
    ("has_feature", C.CFUNCTYPE(C.c_int32, BpStr)),
    ("abi_minor", C.CFUNCTYPE(C.c_uint32)),
    ("log", C.CFUNCTYPE(None, C.c_int32, BpStr)),
    ("alloc", C.CFUNCTYPE(C.c_void_p, C.c_size_t)),
    ("free_", C.CFUNCTYPE(None, C.c_void_p)),
    ("board_width", C.CFUNCTYPE(C.c_int32)),
    ("board_height", C.CFUNCTYPE(C.c_int32)),
    ("tile_at", C.CFUNCTYPE(C.c_int32, C.c_int32, C.c_int32)),
    ("bomb_count_at", C.CFUNCTYPE(C.c_int32, C.c_int32, C.c_int32)),
    ("enemies_snapshot", C.CFUNCTYPE(C.c_int32, C.POINTER(BpEnemyInfo), C.c_int32)),
    ("drops_snapshot", C.CFUNCTYPE(C.c_int32, C.POINTER(BpDropInfo), C.c_int32)),
    ("player_state", C.CFUNCTYPE(C.c_int32, C.c_int32, C.POINTER(BpPlayerState))),
    ("entity_count", C.CFUNCTYPE(C.c_int32)),
    ("subscribe", C.CFUNCTYPE(C.c_int32, C.c_uint32, EventCb, C.c_void_p)),
    ("set_var_i32", C.CFUNCTYPE(C.c_int32, BpStr, C.c_int32)),
    ("set_var_f64", C.CFUNCTYPE(C.c_int32, BpStr, C.c_double)),
    ("spawn_enemy", C.CFUNCTYPE(C.c_int32, C.c_int32, C.c_int32, C.c_int32)),
    ("remove_entity", C.CFUNCTYPE(C.c_int32, C.c_int32)),
    ("damage_entity", C.CFUNCTYPE(C.c_int32, C.c_int32, C.c_int32)),
    ("damage_player", C.CFUNCTYPE(C.c_int32, C.c_int32, C.c_int32)),
    ("spawn_drop", C.CFUNCTYPE(C.c_int32, C.c_int32, C.c_int32, BpStr)),
    ("set_time_scale", C.CFUNCTYPE(C.c_int32, C.c_double)),
    ("request_pause", C.CFUNCTYPE(C.c_int32, C.c_int32)),
    ("apply_item_effect", C.CFUNCTYPE(C.c_int32, BpStr)),
    ("rng_next", C.CFUNCTYPE(C.c_uint32)),
    ("rng_range", C.CFUNCTYPE(C.c_int32, C.c_int32, C.c_int32)),
    ("register_mode", C.CFUNCTYPE(C.c_int32, C.POINTER(BpModeDesc))),
    ("reserved_mapgen", C.c_void_p),
    ("reserved_ai", C.c_void_p),
    ("reserved_win", C.c_void_p),
    ("level_index", C.CFUNCTYPE(C.c_int32)),
    ("phase", C.CFUNCTYPE(C.c_int32)),
    ("step_index", C.CFUNCTYPE(C.c_int32)),
    ("player_count", C.CFUNCTYPE(C.c_int32)),
    ("bombs_snapshot", C.CFUNCTYPE(C.c_int32, C.POINTER(BpBombInfo), C.c_int32)),
    ("set_tile", C.CFUNCTYPE(C.c_int32, C.c_int32, C.c_int32, C.c_int32)),
    ("set_player_start", C.CFUNCTYPE(C.c_int32, C.c_int32, C.c_int32)),
]

InitFn = C.CFUNCTYPE(C.c_int32, C.POINTER(BpModInfo), C.POINTER(BpHostApi))
ShutdownFn = C.CFUNCTYPE(None)
TickFn = C.CFUNCTYPE(None, C.c_double)
GenMapFn = C.CFUNCTYPE(C.c_int32, C.POINTER(BpMapGenRequest), C.POINTER(BpMapGenResult))
ThinkFn = C.CFUNCTYPE(C.c_int32, C.POINTER(BpEnemyThinkRequest), C.POINTER(BpEnemyThinkResult))
WinFn = C.CFUNCTYPE(C.c_int32, C.POINTER(BpWinResult))
ModeFn = C.CFUNCTYPE(None, BpStr)

BpModApi._fields_ = [
    ("size", C.c_uint32),
    ("init", InitFn),
    ("shutdown", ShutdownFn),
    ("on_tick", TickFn),
    ("on_generate_map", GenMapFn),
    ("on_enemy_think", ThinkFn),
    ("on_check_win", WinFn),
    ("on_mode_start", ModeFn),
    ("on_mode_stop", ModeFn),
]


# ---------------------------------------------------------------- 小工具


def build_id():
    """与头文件的 BP_BUILD_ID 一致：ABI 版本 + 指针宽度。"""
    return BP_MOD_ABI_VERSION * 1000003 + C.sizeof(C.c_void_p)


def think(direction=-1, place_bomb=False):
    """把 `bp_enemy_think` 的结论打包成 shim 认识的整数。

    direction：0 上 / 1 下 / 2 左 / 3 右 / -1 原地不动；None 表示"没意见"。
    编码（shim 侧解析）：0 = 没意见；1 = 原地不动；2..5 = 方向 0..3；6..9 = 方向 0..3 且放弹。
    为什么要打包：C 侧只解析一个整数，避免跨语言传元组/字典那种容易出错的约定。
    """
    if direction is None:
        return 0
    if direction < 0:
        return 1
    return 2 + (direction % 4) + (4 if place_bomb else 0)


def encode(text):
    """str -> BpStr（UTF-8，带长度）"""
    data = text.encode("utf-8")
    return BpStr(data, len(data))


def decode(value):
    """BpStr -> str（len < 0 表示按 \\0 结尾读）"""
    if not value.utf8:
        return ""
    raw = value.utf8
    return raw.decode("utf-8", "replace") if value.len < 0 else raw[: value.len].decode("utf-8", "replace")


def copy_bpstr(value, capacity=256):
    """把引擎给的 BpStr 拷成 Python str（只在本次回调期间有效，想留就留副本）"""
    return decode(value)[: max(0, capacity - 1)]


def decode_event(event):
    """BpGameEvent -> (type, row, col, amount, text)

    载荷按事件号解析成普通值；不认识的事件返回 row/col = -1。
    shim 内部做的是同一件事，这里给"自己在 Python 里收事件"的作者用。
    """
    event_type = int(event.type)
    row, col, amount, text = -1, -1, 0, ""
    payload = event.payload
    if not payload:
        return event_type, row, col, amount, text

    def as_struct(struct_type):
        return C.cast(payload, C.POINTER(struct_type)).contents

    if event_type == BP_EVENT_BOMB_PLACED:
        bomb = as_struct(BpEventBomb)
        row, col, amount = bomb.row, bomb.col, bomb.range
    elif event_type == BP_EVENT_BOMB_EXPLODED:
        boom = as_struct(BpEventExplosion)
        row, col, amount = boom.row, boom.col, boom.range
    elif event_type in (BP_EVENT_ENEMY_DAMAGED, BP_EVENT_PLAYER_DAMAGED, BP_EVENT_PLAYER_DIED):
        damage = as_struct(BpEventDamage)
        amount, text = damage.amount, str(damage.livesLeft)
    elif event_type in (BP_EVENT_DROP_SPAWNED, BP_EVENT_DROP_PICKED, BP_EVENT_ITEM_USED):
        drop = as_struct(BpEventDrop)
        row, col, text = drop.row, drop.col, decode(drop.itemName)
    elif event_type in (BP_EVENT_LEVEL_START, BP_EVENT_LEVEL_WON, BP_EVENT_LEVEL_LOST):
        level = as_struct(BpEventLevel)
        amount, text = level.level, str(level.victory)
    else:
        entity = as_struct(BpEventEntity)
        row, col = entity.row, entity.col

    return event_type, row, col, amount, text


# ---------------------------------------------------------------- 作者用的封装


class Host:
    """宿主函数表的 Python 封装：把"指针 + 长度"这类细节藏起来。

    所有写操作都是**请求**：引擎会校验，校验不过返回负数（见 BP_ERR_*）。
    不要假设它一定成功 —— 契约里最硬的一条是"不支持就如实返回，绝不调了没反应"。
    """

    def __init__(self, host_ptr, mod_dir=""):
        self.ptr = C.cast(host_ptr, C.POINTER(BpHostApi))
        self.api = self.ptr.contents
        self.mod_dir = mod_dir
        self._callbacks = []          # 交给 C 的回调必须钉住，否则会被 GC

        if self.api.size < C.sizeof(BpHostApi):
            raise RuntimeError(
                "引擎的宿主函数表比这份 bp_mod_api.py 短：要么引擎太老，要么这份声明不是 0.1.2")

    # ---- 能力查询 ----
    def has(self, name):
        return self.api.has_feature(encode(name)) == 1

    # ---- 基础 ----
    def log(self, text, level=BP_LOG_INFO):
        self.api.log(level, encode(text))

    # ---- 只读查询 ----
    def board_size(self):
        return self.api.board_width(), self.api.board_height()

    def tile_at(self, row, col):
        return self.api.tile_at(row, col)

    def player_state(self, index=0):
        state = BpPlayerState()
        state.size = C.sizeof(BpPlayerState)
        if self.api.player_state(index, C.byref(state)) != 0:
            return None
        return state

    def player_count(self):
        return self.api.player_count()

    def enemies(self):
        total = self.api.enemies_snapshot(None, 0)
        if total <= 0:
            return []
        buffer = (BpEnemyInfo * total)()
        for item in buffer:
            item.size = C.sizeof(BpEnemyInfo)
        self.api.enemies_snapshot(buffer, total)
        return list(buffer)

    def bombs(self):
        total = self.api.bombs_snapshot(None, 0)
        if total <= 0:
            return []
        buffer = (BpBombInfo * total)()
        for item in buffer:
            item.size = C.sizeof(BpBombInfo)
        self.api.bombs_snapshot(buffer, total)
        return list(buffer)

    def drops(self):
        total = self.api.drops_snapshot(None, 0)
        if total <= 0:
            return []
        buffer = (BpDropInfo * total)()
        for item in buffer:
            item.size = C.sizeof(BpDropInfo)
        self.api.drops_snapshot(buffer, total)
        return list(buffer)

    def level(self):
        return self.api.level_index()

    def phase(self):
        return self.api.phase()

    def step(self):
        return self.api.step_index()

    # ---- 订阅 ----
    def subscribe(self, event_type, handler):
        """handler(type, row, col, amount, text)：订阅某个事件（或 BP_EVENT_ANY）

        说明：**最简单的做法是在 mod.py 里写 bp_on_event(...)** —— shim 会自动订阅全部事件
        并调用它，你什么都不用做。这个方法给"想自己细分订阅"的作者用；
        同一类事件不要两边都收（否则你会收到两次）。
        """
        def trampoline(event_ptr, user):
            del user          # C 侧的 user 指针我们不用（用闭包就够了）
            event = event_ptr.contents
            handler(*decode_event(event))

        callback = EventCb(trampoline)
        self._callbacks.append(callback)
        return self.api.subscribe(event_type, callback, None)

    # ---- 随机数（**必须**用它：自己抽 rand 会让联机各端不同步） ----
    def rng(self):
        return self.api.rng_next()

    def rng_range(self, low, high):
        return self.api.rng_range(low, high)

    # ---- 写请求 ----
    def set_var(self, key, value):
        if isinstance(value, int):
            return self.api.set_var_i32(encode(key), value)
        return self.api.set_var_f64(encode(key), float(value))

    def spawn_enemy(self, row, col, hp=1):
        return self.api.spawn_enemy(row, col, hp)

    def remove_entity(self, entity_id):
        return self.api.remove_entity(entity_id)

    def damage_entity(self, entity_id, amount):
        return self.api.damage_entity(entity_id, amount)

    def damage_player(self, player_index, amount):
        return self.api.damage_player(player_index, amount)

    def spawn_drop(self, row, col, item_name):
        return self.api.spawn_drop(row, col, encode(item_name))

    def pause(self, paused=True):
        return self.api.request_pause(1 if paused else 0)

    def time_scale(self, scale):
        return self.api.set_time_scale(float(scale))

    def apply_item_effect(self, name_or_effect):
        return self.api.apply_item_effect(encode(name_or_effect))

    # ---- 模式（只能在 bp_init 里调） ----
    def register_mode(self, mode_id, name, hint="", description="",
                      flags=BP_MODE_FLAG_SINGLE, kind=BP_MODE_KIND_HOOKS,
                      player_lives=0, player_max_bombs=0, player_bomb_range=0,
                      time_limit_seconds=0.0):
        desc = BpModeDesc()
        desc.size = C.sizeof(BpModeDesc)
        desc.id = encode(mode_id)
        desc.name = encode(name)
        desc.hint = encode(hint)
        desc.description = encode(description)
        desc.kind = kind
        desc.flags = flags
        desc.playerLives = player_lives
        desc.playerMaxBombs = player_max_bombs
        desc.playerBombRange = player_bomb_range
        desc.timeLimitSeconds = time_limit_seconds
        return self.api.register_mode(C.byref(desc))

    # ---- 铺地图（只能在 bp_generate_map 期间调） ----
    def set_tile(self, row, col, tile):
        return self.api.set_tile(row, col, tile)

    def set_player_start(self, row, col):
        return self.api.set_player_start(row, col)
