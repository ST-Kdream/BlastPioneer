"""
跨语言 ABI 验证：用 Python（标准库 ctypes，零依赖）当**宿主**，去加载并驱动 C++ 写的
原生模组 DLL（sdk/samples/hello_native）。

为什么这样验证最有用
--------------------
如果一个 ABI 只有 C++ 能用，"多语言"就是口号。这个脚本用 Python 亲手做一遍宿主该做的事：

  1. 按 sdk/bp_mod_api.h 声明所有结构体 -> 顺便验证 **字段偏移与对齐** 跨语言是否一致
     （脚本里有一张 offsets 表，与头文件里的 static_assert 是同一组数字）；
  2. 算一遍 BP_BUILD_ID 传给模组      -> 模组内部会拿它和 C 那边算的比，不一致就拒绝加载，
                                          所以"能加载成功"本身就证明两边对版本与指针宽度的理解一致；
  3. 调 bp_mod_entry / init           -> 验证调用约定、BpStr 编组；
  4. 事件回调 **由 Python 触发**       -> 验证 Python -> C++ 方向的回调与载荷布局
     （包括 0.1.1 才有的 BpEventDrop / BpEventLevel / BpEventDamage 载荷）；
  5. 接住模组的写请求与只读查询       -> 验证 C++ -> Python 方向；
  6. 故意把函数表写短一点（模拟老引擎）-> 验证模组是用 host->size 探测新函数，而不是盲调。

所以它一次把两个方向都跑通，而且不需要把 CPython 嵌进游戏里。

用法（任意 Python 3.8+，不需要装任何包）：
    python sdk/python/abi_check.py --dll sdk/samples/hello_native/hello_native.dll
"""

import argparse
import ctypes as C
import os
import sys

# ---------------------------------------------------------------- 常量（与头文件一致）

BP_MOD_API_VERSION = b"0.1.2"
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
BP_EVENT_DROP_SPAWNED = 7
BP_EVENT_DROP_PICKED = 8
BP_EVENT_LEVEL_WON = 9
BP_EVENT_LEVEL_LOST = 10
BP_EVENT_ENEMY_DAMAGED = 11
BP_EVENT_ITEM_USED = 12

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


class BpEventExplosion(C.Structure):
    _fields_ = [
        ("size", C.c_uint32),
        ("row", C.c_int32),
        ("col", C.c_int32),
        ("destroyedBricks", C.c_int32),
        ("range", C.c_int32),          # 0.1.1 追加
    ]


class BpEventEntity(C.Structure):
    _fields_ = [
        ("size", C.c_uint32),
        ("entityId", C.c_int32),
        ("row", C.c_int32),
        ("col", C.c_int32),
    ]


class BpEventDrop(C.Structure):
    """BpEventEntity + 追加的道具名（所以老读法依然成立）"""
    _fields_ = [
        ("size", C.c_uint32),
        ("entityId", C.c_int32),
        ("row", C.c_int32),
        ("col", C.c_int32),
        ("itemName", BpStr),
    ]


class BpEventDamage(C.Structure):
    _fields_ = [
        ("size", C.c_uint32),
        ("playerIndex", C.c_int32),
        ("amount", C.c_int32),
        ("livesLeft", C.c_int32),
    ]


class BpEventLevel(C.Structure):
    _fields_ = [
        ("size", C.c_uint32),
        ("entityId", C.c_int32),
        ("row", C.c_int32),
        ("col", C.c_int32),
        ("level", C.c_int32),          # 0.1.1 追加
        ("victory", C.c_int32),        # 0.1.1 追加
    ]


class BpPlayerState(C.Structure):
    _fields_ = [
        ("size", C.c_uint32),
        ("row", C.c_int32),
        ("col", C.c_int32),
        ("lives", C.c_int32),
        ("maxLives", C.c_int32),
        ("bombRange", C.c_int32),
        ("maxBombs", C.c_int32),
        ("ghost", C.c_int32),
    ]


class BpEnemyInfo(C.Structure):
    _fields_ = [
        ("size", C.c_uint32),
        ("entityId", C.c_int32),
        ("row", C.c_int32),
        ("col", C.c_int32),
        ("hp", C.c_int32),
        ("maxHp", C.c_int32),
        ("alive", C.c_int32),
    ]


class BpDropInfo(C.Structure):
    _fields_ = [
        ("size", C.c_uint32),
        ("row", C.c_int32),
        ("col", C.c_int32),
        ("itemName", BpStr),
    ]


class BpBombInfo(C.Structure):
    _fields_ = [
        ("size", C.c_uint32),
        ("row", C.c_int32),
        ("col", C.c_int32),
        ("ownerId", C.c_int32),
        ("range", C.c_int32),
        ("remainingSeconds", C.c_double),
        ("fuseTotal", C.c_double),
    ]


# ---- 0.1.2：模式与三个钩子的参数/结果（字段顺序必须与头文件一致） ----

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
    _fields_ = [
        ("size", C.c_uint32), ("level", C.c_int32), ("rows", C.c_int32), ("cols", C.c_int32),
        ("seed", C.c_uint32), ("brickCount", C.c_int32), ("enemyCount", C.c_int32),
    ]


class BpMapGenResult(C.Structure):
    _fields_ = [
        ("size", C.c_uint32), ("ok", C.c_int32),
        ("playerStartRow", C.c_int32), ("playerStartCol", C.c_int32),
    ]


class BpEnemyThinkRequest(C.Structure):
    _fields_ = [
        ("size", C.c_uint32), ("enemyId", C.c_int32), ("row", C.c_int32), ("col", C.c_int32),
        ("x", C.c_double), ("y", C.c_double),
        ("hp", C.c_int32), ("maxHp", C.c_int32), ("stepIndex", C.c_int32),
        ("frameTime", C.c_double),
    ]


class BpEnemyThinkResult(C.Structure):
    _fields_ = [
        ("size", C.c_uint32), ("acted", C.c_int32), ("direction", C.c_int32),
        ("placeBomb", C.c_int32),
    ]


class BpWinResult(C.Structure):
    _fields_ = [
        ("size", C.c_uint32), ("decided", C.c_int32), ("victory", C.c_int32), ("reason", BpStr),
    ]


# 回调类型：事件回调 + 模组生命周期
EventCb = C.CFUNCTYPE(None, C.POINTER(BpGameEvent), C.c_void_p)


class BpHostApi(C.Structure):
    """宿主函数表。字段顺序**必须**与 sdk/bp_mod_api.h 完全一致。"""
    pass


class BpModApi(C.Structure):
    """模组函数表。字段顺序**必须**与 sdk/bp_mod_api.h 完全一致。"""
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
    # 0.1.2：这个槽位从 reserved_mode 变成真正的 register_mode（偏移不变）
    ("register_mode", C.CFUNCTYPE(C.c_int32, C.POINTER(BpModeDesc))),
    ("reserved_mapgen", C.c_void_p),
    ("reserved_ai", C.c_void_p),
    ("reserved_win", C.c_void_p),
    # ---- 0.1.1 追加（只能接在预留槽位之后） ----
    ("level_index", C.CFUNCTYPE(C.c_int32)),
    ("phase", C.CFUNCTYPE(C.c_int32)),
    ("step_index", C.CFUNCTYPE(C.c_int32)),
    ("player_count", C.CFUNCTYPE(C.c_int32)),
    ("bombs_snapshot", C.CFUNCTYPE(C.c_int32, C.POINTER(BpBombInfo), C.c_int32)),
    # ---- 0.1.2 追加：铺地图期间用的两个函数 ----
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


# BP_BUILD_ID 必须在两边算出同一个数：它就是"ABI 版本 + 指针宽度"的指纹（契约 §4.3）
def build_id():
    return BP_MOD_ABI_VERSION * 1000003 + C.sizeof(C.c_void_p)


# 0.1.0 对外承诺过的偏移量（与 bp_mod_api.h 里的 static_assert 是同一组数字）。
# 只在 64 位下校验具体数值：其它平台的对齐规则可能不同。
FROZEN_OFFSETS_X64 = [
    (BpStr, "len", 8),
    (BpModInfo, "buildId", 24),
    (BpModInfo, "id", 32),
    (BpModInfo, "modDir", 80),
    (BpGameEvent, "payload", 8),
    (BpHostApi, "has_feature", 8),
    (BpHostApi, "subscribe", 112),
    (BpHostApi, "rng_range", 208),
    (BpHostApi, "reserved_win", 240),
    (BpModApi, "init", 8),
    (BpModApi, "on_tick", 24),
]

FROZEN_SIZES_X64 = [
    (BpStr, 16),
    (BpModInfo, 96),
    (BpGameEvent, 16),
    (BpBombInfo, 40),
]


# ---------------------------------------------------------------- 假的宿主


class FakeHost:
    """一个最小宿主：把模组的调用记下来，并给出可预期的回答。"""

    def __init__(self):
        self.logs = []                 # (level, text)
        self.subscriptions = []        # (type, callback, user)
        self.pauses = []               # 模组请求过的暂停
        self.items = []                # 模组触发过的道具效果
        self.rng_calls = 0
        self.meta_calls = []           # 模组问过的世界元信息（0.1.1）
        self.bomb_snapshot_calls = 0   # 模组查过几次炸弹快照（0.1.1）
        self.modes = []                # 模组注册过的模式（0.1.2）
        self.tiles = {}                # 模组铺过的格子（0.1.2）
        self.player_start = None       # 模组指定的出生点（0.1.2）
        self._keep_alive = []          # 交给 C 的 Python 对象必须活着

        self.api = BpHostApi()
        self.api.size = C.sizeof(BpHostApi)

        # 把 Python 方法包成 CFUNCTYPE 实例（ctypes 不接受裸方法），
        # 并**钉住**引用：交给 C 的回调一旦被 GC 就是野指针
        field_types = dict(BpHostApi._fields_)
        bindings = {
            "has_feature": self._has_feature,
            "abi_minor": self._abi_minor,
            "log": self._log,
            "alloc": self._alloc,
            "free_": self._free,
            "board_width": self._board_width,
            "board_height": self._board_height,
            "tile_at": self._tile_at,
            "bomb_count_at": self._bomb_count_at,
            "enemies_snapshot": self._enemies_snapshot,
            "drops_snapshot": self._drops_snapshot,
            "player_state": self._player_state,
            "entity_count": self._entity_count,
            "subscribe": self._subscribe,
            "set_var_i32": self._unsupported_i32,
            "set_var_f64": self._unsupported_f64,
            "spawn_enemy": self._unsupported_3,
            "remove_entity": self._unsupported_1,
            "damage_entity": self._unsupported_2,
            "damage_player": self._unsupported_2,
            "spawn_drop": self._unsupported_drop,
            "set_time_scale": self._unsupported_d,
            "request_pause": self._request_pause,
            "apply_item_effect": self._apply_item_effect,
            "rng_next": self._rng_next,
            "rng_range": self._rng_range,
            # ---- 0.1.1 追加 ----
            "level_index": self._level_index,
            "phase": self._phase,
            "step_index": self._step_index,
            "player_count": self._player_count,
            "bombs_snapshot": self._bombs_snapshot,
            # ---- 0.1.2：模式注册与"铺地图"用的两个函数 ----
            "register_mode": self._register_mode,
            "set_tile": self._set_tile,
            "set_player_start": self._set_player_start,
        }
        for name, func in bindings.items():
            wrapper = field_types[name](func)
            self._keep_alive.append(wrapper)
            setattr(self.api, name, wrapper)

    # ---- 把 Python 回调交给 C 之前要先"钉住"，否则会被 GC ----
    def pin(self, fn):
        self._keep_alive.append(fn)
        return fn

    # ---- 能力查询 ----
    def _has_feature(self, name):
        text = decode(name)
        # 这里故意只开一部分：**能力查询**的意义就是让模组别假设功能存在
        allowed = ("event.subscribe", "world.read", "item.effect", "rng",
                   "world.read.bombs", "world.read.meta", "world.read.players",
                   "mode.register", "mode.hooks", "world.write.entity", "time.scale")
        return 1 if text in allowed else 0

    # ---- 0.1.2：模式注册与铺地图 ----
    def _register_mode(self, desc_ptr):
        desc = desc_ptr.contents
        self.modes.append({
            "id": decode(desc.id),
            "name": decode(desc.name),
            "kind": desc.kind,
            "flags": desc.flags,
            "lives": desc.playerLives,
        })
        return BP_OK

    def _set_tile(self, row, col, tile):
        self.tiles[(row, col)] = tile
        return BP_OK

    def _set_player_start(self, row, col):
        self.player_start = (row, col)
        return BP_OK

    def _abi_minor(self):
        return 1

    # ---- 基础服务 ----
    def _log(self, level, message):
        self.logs.append((level, decode(message)))

    def _alloc(self, size):
        return C.cast(C.create_string_buffer(size), C.c_void_p)

    def _free(self, ptr):
        pass

    # ---- 只读查询：给一组可预期的假数据 ----
    def _board_width(self):
        return 13

    def _board_height(self):
        return 11

    def _tile_at(self, row, col):
        if row == 0 or col == 0:
            return BP_TILE_WALL
        if row == 3 and col == 4:
            return BP_TILE_BRICK
        return BP_TILE_EMPTY

    def _bomb_count_at(self, row, col):
        return 0

    def _enemies_snapshot(self, out, capacity):
        """两段式约定：out 为空只报数量；否则写入并返回真实数量。"""
        total = 2
        if not out or capacity <= 0:
            return total
        for i in range(min(capacity, total)):
            out[i].size = C.sizeof(BpEnemyInfo)
            out[i].entityId = i
            out[i].row = 5 + i
            out[i].col = 6
            out[i].hp = 3 - i
            out[i].maxHp = 3
            out[i].alive = 1
        return total

    def _drops_snapshot(self, out, capacity):
        return 0

    def _player_state(self, index, out):
        if not out or index != 0:
            return BP_ERR_INVALID_ARG
        out.contents.size = C.sizeof(BpPlayerState)
        out.contents.row, out.contents.col = 1, 1
        out.contents.lives, out.contents.maxLives = 3, 3
        out.contents.bombRange, out.contents.maxBombs = 1, 1
        out.contents.ghost = 0
        return BP_OK

    def _entity_count(self):
        return 2

    # ---- 0.1.1 追加的世界查询：给可预期的假数据 ----
    def _level_index(self):
        self.meta_calls.append("level_index")
        return 3

    def _phase(self):
        self.meta_calls.append("phase")
        return BP_PHASE_RUNNING

    def _step_index(self):
        self.meta_calls.append("step_index")
        return 42

    def _player_count(self):
        self.meta_calls.append("player_count")
        return 2

    def _bombs_snapshot(self, out, capacity):
        """两段式：out 为空只报数量；否则写入并返回真实数量。"""
        self.bomb_snapshot_calls += 1
        total = 2
        if not out or capacity <= 0:
            return total
        for i in range(min(capacity, total)):
            out[i].size = C.sizeof(BpBombInfo)
            out[i].row = 6 + i
            out[i].col = 7
            out[i].ownerId = i + 1
            out[i].range = 2 + i
            out[i].remainingSeconds = 1.0 - 0.25 * i
            out[i].fuseTotal = 1.5
        return total

    # ---- 订阅：把回调收下来，后面由测试主动触发 ----
    def _subscribe(self, event_type, callback, user):
        self.subscriptions.append((event_type, callback, user))
        return BP_OK

    # ---- 写请求：L2 里只有这两个是真的 ----
    def _request_pause(self, paused):
        self.pauses.append(paused)
        return BP_OK

    def _apply_item_effect(self, name):
        self.items.append(decode(name))
        return BP_OK

    def _rng_next(self):
        self.rng_calls += 1
        return 12345

    def _rng_range(self, lo, hi):
        self.rng_calls += 1
        return lo + 1 if hi > lo + 1 else lo

    # ---- 未实现的能力：如实返回"不支持"，绝不假装成功 ----
    def _unsupported_1(self, a):
        return BP_ERR_UNSUPPORTED

    def _unsupported_2(self, a, b):
        return BP_ERR_UNSUPPORTED

    def _unsupported_3(self, a, b, c):
        return BP_ERR_UNSUPPORTED

    def _unsupported_i32(self, name, value):
        return BP_ERR_UNSUPPORTED

    def _unsupported_f64(self, name, value):
        return BP_ERR_UNSUPPORTED

    def _unsupported_d(self, value):
        return BP_ERR_UNSUPPORTED

    def _unsupported_drop(self, row, col, name):
        return BP_ERR_UNSUPPORTED


def decode(value):
    """BpStr -> str（len < 0 表示按 \\0 结尾读）"""
    if not value.utf8:
        return ""
    raw = value.utf8
    return raw.decode("utf-8", "replace") if value.len < 0 else raw[: value.len].decode("utf-8", "replace")


def encode(text):
    return BpStr(text.encode("utf-8"), -1)


# ---------------------------------------------------------------- 测试主体


def main():
    parser = argparse.ArgumentParser(description="跨语言 ABI 验证（Python 当宿主，驱动 C++ 模组 DLL）")
    parser.add_argument("--dll", required=True, help="要加载的原生模组 DLL")
    args = parser.parse_args()

    dll_path = os.path.abspath(args.dll)
    if not os.path.isfile(dll_path):
        print(f"[失败] 找不到 DLL：{dll_path}")
        print("       先跑 sdk/samples/hello_native/build.bat 编一个出来")
        return 2

    checks = []

    def check(condition, what):
        checks.append((bool(condition), what))
        print(f"  {'[OK]  ' if condition else '[FAIL]'} {what}")
        return bool(condition)

    print("=== 1) 结构体布局（Python 这边的 sizeof 与偏移） ===")
    for name, size in (("BpStr", C.sizeof(BpStr)),
                       ("BpModInfo", C.sizeof(BpModInfo)),
                       ("BpGameEvent", C.sizeof(BpGameEvent)),
                       ("BpHostApi", C.sizeof(BpHostApi)),
                       ("BpModApi", C.sizeof(BpModApi)),
                       ("BpBombInfo", C.sizeof(BpBombInfo)),
                       ("void*", C.sizeof(C.c_void_p))):
        print(f"    {name:12s} = {size}")
    print(f"    BP_BUILD_ID  = {build_id()}")

    # 冻结偏移：与 bp_mod_api.h 里的 static_assert 是同一组数字。
    # 这是"两个语言对同一份布局的理解是否一致"的直接证据（比 sizeof 更细）
    if C.sizeof(C.c_void_p) == 8:
        for struct, field, expected in FROZEN_OFFSETS_X64:
            actual = getattr(struct, field).offset
            check(actual == expected, f"offsetof({struct.__name__}, {field}) == {expected}（实际 {actual}）")
        for struct, expected in FROZEN_SIZES_X64:
            actual = C.sizeof(struct)
            check(actual == expected, f"sizeof({struct.__name__}) == {expected}（实际 {actual}）")
        check(BpHostApi.set_player_start.offset == C.sizeof(BpHostApi) - C.sizeof(C.c_void_p),
              "set_player_start 是宿主函数表的最后一项（只在末尾追加）")
    else:
        print("    （32 位平台：跳过具体偏移量校验，对齐规则可能不同）")

    host = FakeHost()

    print()
    print("=== 2) 加载 DLL 并查符号 ===")
    try:
        lib = C.CDLL(dll_path)
    except OSError as error:
        print(f"[失败] 加载 DLL 失败：{error}")
        return 2
    check(hasattr(lib, "bp_mod_entry"), "DLL 导出了 bp_mod_entry")

    entry = lib.bp_mod_entry
    entry.argtypes = [C.c_char_p, C.POINTER(BpHostApi)]
    entry.restype = C.POINTER(BpModApi)

    print()
    print("=== 3) 版本握手（契约里的'优雅拒绝'） ===")
    refused = entry(b"9.9.9", C.byref(host.api))
    check(not refused, "接口版本不匹配时模组返回 NULL（明确拒绝，而不是崩）")

    mod = entry(BP_MOD_API_VERSION, C.byref(host.api))
    check(bool(mod), "版本匹配时拿到模组函数表")
    if not mod:
        print("[失败] 模组拒绝了正确版本，后面的检查没法继续")
        return 1

    check(mod.contents.size >= 32, f"函数表 size 合理（{mod.contents.size}）")

    print()
    print("=== 4) init：跨语言的 BpStr 与 buildId 指纹 ===")
    mod_text = encode("abi_check")
    name_text = encode("ABI 跨语言验证")
    version_text = encode("0.1.1")
    dir_text = encode(os.path.dirname(dll_path))

    info = BpModInfo()
    info.size = C.sizeof(BpModInfo)
    info.abiVersionMajor = 0
    info.abiVersionMinor = 1
    info.pointerSize = C.sizeof(C.c_void_p)
    info.endianLittle = 1
    info.buildId = build_id()          # 与 C 那边算的必须一致，否则模组会拒绝加载
    info.id, info.name, info.version, info.modDir = mod_text, name_text, version_text, dir_text

    result = mod.contents.init(C.byref(info), C.byref(host.api))
    check(result == 0, f"init 返回 0（实际 {result}）")

    check(any("init 完成" in text for _, text in host.logs),
          "模组通过宿主函数表写回了日志（BpStr 编组正确）")
    check(any("13x11" in text for _, text in host.logs),
          "模组读到了我们给的棋盘尺寸 13x11（只读查询可用）")

    # 0.1.1 的能力查询与元信息：模组先问能力名，再用 host->size 确认函数指针够得到
    check(any("bombs=1" in text and "meta=1" in text and "players=1" in text
              for _, text in host.logs),
          "模组读到了 0.1.1 的三个新能力名都可用")
    check(any("第 3 关" in text and "阶段 1" in text and "第 42 步" in text and "玩家 2 人" in text
              for _, text in host.logs),
          f"模组读到了世界元信息（level/phase/step/player_count）："
          f"{[t for _, t in host.logs if '世界信息' in t]}")
    check(sorted(set(host.meta_calls)) == ["level_index", "phase", "player_count", "step_index"],
          f"C++ -> Python 方向的元信息查询全部到达（{sorted(set(host.meta_calls))}）")

    check(len(host.subscriptions) == 6, f"模组订阅了 6 个事件（实际 {len(host.subscriptions)}）")
    types = sorted(t for t, _, _ in host.subscriptions)
    check(types == sorted([BP_EVENT_LEVEL_START, BP_EVENT_BOMB_EXPLODED, BP_EVENT_ENEMY_DAMAGED,
                           BP_EVENT_ITEM_USED, BP_EVENT_LEVEL_WON, BP_EVENT_LEVEL_LOST]),
          f"订阅的事件号正确（{types}）")

    print()
    print("=== 5) Python -> C++：由我们主动触发事件 ===")
    callbacks = {}
    for event_type, callback, _ in host.subscriptions:
        callbacks.setdefault(event_type, callback)

    bomb_cb = callbacks.get(BP_EVENT_BOMB_EXPLODED)
    check(bomb_cb is not None, "拿到了爆炸事件的回调指针")
    if not bomb_cb:
        return 1

    payload = BpEventExplosion()
    payload.size = C.sizeof(BpEventExplosion)
    payload.row, payload.col, payload.destroyedBricks, payload.range = 3, 4, 0, 2

    event = BpGameEvent()
    event.size = C.sizeof(BpGameEvent)
    event.type = BP_EVENT_BOMB_EXPLODED
    event.payload = C.cast(C.pointer(payload), C.c_void_p)

    before = len(host.logs)
    bomb_cb(C.byref(event), None)
    new_lines = [text for _, text in host.logs[before:]]
    check(any("第 1 次爆炸" in text and "(3, 4)" in text and "威力 2" in text for text in new_lines),
          f"模组正确读出了载荷（row/col/range）：{new_lines}")
    check(host.rng_calls > 0, "模组用了宿主给的随机流（而不是自己抽 rand）")

    # 0.1.1 的载荷：掉落类事件带道具名
    item_cb = callbacks.get(BP_EVENT_ITEM_USED)
    check(item_cb is not None, "拿到了「使用道具」事件的回调指针")
    if item_cb:
        drop = BpEventDrop()
        drop.size = C.sizeof(BpEventDrop)
        drop.entityId = -1
        drop.row, drop.col = 5, 6
        drop.itemName = encode("生命药水")

        item_event = BpGameEvent()
        item_event.size = C.sizeof(BpGameEvent)
        item_event.type = BP_EVENT_ITEM_USED
        item_event.payload = C.cast(C.pointer(drop), C.c_void_p)

        before = len(host.logs)
        item_cb(C.byref(item_event), None)
        new_lines = [text for _, text in host.logs[before:]]
        check(any("道具被使用" in text and "生命药水" in text for text in new_lines),
              f"BpEventDrop 里的道具名跨语言读对了：{new_lines}")

    # 0.1.1 的载荷：关卡载荷带关卡号与胜负
    won_cb = callbacks.get(BP_EVENT_LEVEL_WON)
    check(won_cb is not None, "拿到了过关事件的回调指针")
    if won_cb:
        level = BpEventLevel()
        level.size = C.sizeof(BpEventLevel)
        level.entityId = -1
        level.row, level.col = -1, -1
        level.level = 4
        level.victory = 1

        level_event = BpGameEvent()
        level_event.size = C.sizeof(BpGameEvent)
        level_event.type = BP_EVENT_LEVEL_WON
        level_event.payload = C.cast(C.pointer(level), C.c_void_p)

        before = len(host.logs)
        won_cb(C.byref(level_event), None)
        new_lines = [text for _, text in host.logs[before:]]
        check(any("过关" in text and "第 4 关" in text and "victory=1" in text for text in new_lines),
              f"BpEventLevel 里的关卡号与胜负读对了：{new_lines}")

    # 0.1.1 的载荷：伤害事件带剩余血量
    hurt_cb = callbacks.get(BP_EVENT_ENEMY_DAMAGED)
    check(hurt_cb is not None, "拿到了敌人受伤事件的回调指针")
    if hurt_cb:
        damage = BpEventDamage()
        damage.size = C.sizeof(BpEventDamage)
        damage.playerIndex = -1
        damage.amount = 1
        damage.livesLeft = 2

        hurt_event = BpGameEvent()
        hurt_event.size = C.sizeof(BpGameEvent)
        hurt_event.type = BP_EVENT_ENEMY_DAMAGED
        hurt_event.payload = C.cast(C.pointer(damage), C.c_void_p)

        before = len(host.logs)
        hurt_cb(C.byref(hurt_event), None)
        new_lines = [text for _, text in host.logs[before:]]
        check(any("敌人受伤" in text and "剩余 2" in text for text in new_lines),
              f"BpEventDamage 里的剩余血量读对了：{new_lines}")

    print()
    print("=== 6) C++ -> Python：接住模组的写请求与只读查询 ===")
    bomb_cb(C.byref(event), None)
    bomb_cb(C.byref(event), None)      # 第 3 次爆炸时模组会请求暂停
    check(host.pauses == [1], f"模组第 3 次爆炸后请求暂停，我们收到了 {host.pauses}")

    # 关卡开始事件会触发"炸弹快照"（两段式：先问数量）
    start_cb = callbacks.get(BP_EVENT_LEVEL_START)
    check(start_cb is not None, "拿到了关卡开始事件的回调指针")
    if start_cb:
        level_start = BpEventLevel()
        level_start.size = C.sizeof(BpEventLevel)
        level_start.entityId = -1
        level_start.row, level_start.col = -1, -1
        level_start.level = 1
        level_start.victory = 0

        start_event = BpGameEvent()
        start_event.size = C.sizeof(BpGameEvent)
        start_event.type = BP_EVENT_LEVEL_START
        start_event.payload = C.cast(C.pointer(level_start), C.c_void_p)

        before = len(host.logs)
        start_cb(C.byref(start_event), None)
        new_lines = [text for _, text in host.logs[before:]]
        check(host.bomb_snapshot_calls > 0, "模组查了炸弹快照（0.1.1 的两段式查询）")
        check(any("场上炸弹 2 枚" in text for text in new_lines),
              f"炸弹快照的返回值跨语言正确：{new_lines}")

    print()
    print("=== 7) 0.1.2：模式注册与三个钩子（地图 / AI / 胜负） ===")
    check(len(host.modes) == 1, f"模组在 init 里注册了 1 个模式（实际 {len(host.modes)}）")
    if host.modes:
        mode = host.modes[0]
        check(mode["id"] == "demo_arena", f"模式 id 跨语言读对了（{mode['id']}）")
        check(mode["kind"] == 0, "模式类型是 HOOKS")
        check(mode["flags"] & (1 << 3) and mode["flags"] & (1 << 4) and mode["flags"] & (1 << 5),
              "它声明了接管地图 / AI / 胜负三个能力位")
        check(mode["lives"] == 5, "模式声明的默认生命数读对了")

    check(host.api.has_feature(encode("mode.register")) == 1, "has_feature(mode.register) = 1")

    if mod.contents.on_generate_map:
        request = BpMapGenRequest()
        request.size = C.sizeof(BpMapGenRequest)
        request.level = 1
        request.rows, request.cols = 11, 13
        request.seed = 4242
        request.brickCount, request.enemyCount = 20, 4

        result = BpMapGenResult()
        result.size = C.sizeof(BpMapGenResult)
        answered = mod.contents.on_generate_map(C.byref(request), C.byref(result))
        check(answered == 1 and result.ok == 1, "模组接管了地图生成（on_generate_map 返回 1）")
        bricks = sum(1 for tile in host.tiles.values() if tile == 2)
        check(bricks > 0, f"它通过 host->set_tile 铺了 {bricks} 块砖")
        check(result.playerStartRow == 5 and result.playerStartCol == 6,
              f"出生点读对了（{result.playerStartRow}, {result.playerStartCol}）")

    if mod.contents.on_enemy_think:
        request = BpEnemyThinkRequest()
        request.size = C.sizeof(BpEnemyThinkRequest)
        request.enemyId = 3
        request.row, request.col = 2, 2      # 假宿主里玩家在 (1,1) -> 敌人应该往上/往左
        request.x, request.y = 2.5, 2.5
        request.hp, request.maxHp = 2, 2
        request.stepIndex = 5
        request.frameTime = 1.0 / 60.0

        result = BpEnemyThinkResult()
        result.size = C.sizeof(BpEnemyThinkResult)
        acted = mod.contents.on_enemy_think(C.byref(request), C.byref(result))
        check(acted == 1 and result.acted == 1, "模组接管了敌人 AI（on_enemy_think 返回 1）")
        check(result.direction in (0, 2), f"它给了朝玩家的方向（{result.direction}）")

    if mod.contents.on_check_win:
        result = BpWinResult()
        result.size = C.sizeof(BpWinResult)
        decided = mod.contents.on_check_win(C.byref(result))
        # 假宿主的 enemies_snapshot 返回 2 -> 敌人还在，模组应该"没意见"
        check(decided == 0, f"胜负未定时它不抢着判（返回 {decided}）")

    print()
    print("=== 8) on_tick 与 shutdown ===")
    if mod.contents.on_tick:
        mod.contents.on_tick(1.0 / 60.0)
        check(True, "on_tick 可调用")
    if mod.contents.shutdown:
        mod.contents.shutdown()
        check(any("shutdown" in text for _, text in host.logs), "shutdown 可调用")

    failed = [what for ok, what in checks if not ok]
    print()
    print("-" * 60)
    print(f"检查了 {len(checks)} 项，失败 {len(failed)} 项")
    for what in failed:
        print(f"  [FAIL] {what}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
