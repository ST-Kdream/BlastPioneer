"""
shim_check.py —— 验证"用 Python 写的模组"真的能被当成原生模组加载

它做的事和游戏**完全一样**（只是宿主换成 Python 写的假引擎）：

    加载 hello_python.dll（= bp_python_shim.dll）
      -> bp_mod_entry(版本, 宿主表)
      -> init(...)            转身去启动 CPython、跑 mod.py 的 bp_init
      -> 推事件 / on_tick / 铺地图 / 敌人思考 / 判胜负
      -> shutdown

为什么这一步必须单独验证：
    `abi_check.py` 证明的是"ABI 对 Python 友好"（Python 当**宿主**驱动 C++ 模组）。
    本脚本证明的是另一半：**Python 当模组**（shim 把 C 回调转成 Python 函数调用）。
    两件事都过了，"多语言"才算落地。

用法（任意 Python 3.8+，零依赖；需要先编出 shim）：
    # 1) 编译 shim
    sdk\\python_shim\\build_shim.bat
    # 2) 组一个模组目录（把 shim 改名、拷上 bp_mod_api.py）
    #    例如 sdk\\samples\\hello_python\\ 里放 hello_python.dll 与 bp_mod_api.py
    python sdk\\python\\shim_check.py --mod sdk\\samples\\hello_python
"""

import argparse
import ctypes as C
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import bp_mod_api as api  # noqa: E402  （同目录的 ctypes 声明，作者也用它）


# ---------------------------------------------------------------- 假引擎（宿主）

class FakeEngine:
    """最小的宿主：记下模组做了什么，并给出可预期的回答。"""

    def __init__(self):
        self.logs = []                  # (level, text)
        self.subscriptions = []         # (type, callback)
        self.surfaces = {}              # (row, col) -> tile
        self.enemy_spawns = []          # (row, col, hp)
        self.player_start = None
        self.modes = []                 # BpModeDesc 的副本
        self.time_scales = []
        self.pauses = []
        self._keep_alive = []

        self.host = api.BpHostApi()
        self.host.size = C.sizeof(api.BpHostApi)

        bindings = {
            "has_feature": self._has_feature,
            "abi_minor": lambda: 1,
            "log": self._log,
            "alloc": self._alloc,
            "free_": lambda ptr: None,
            "board_width": lambda: 13,
            "board_height": lambda: 11,
            "tile_at": self._tile_at,
            "bomb_count_at": lambda row, col: 0,
            "enemies_snapshot": self._enemies,
            "drops_snapshot": lambda out, capacity: 0,
            "player_state": self._player_state,
            "entity_count": lambda: len(self.enemy_spawns),
            "subscribe": self._subscribe,
            "set_var_i32": lambda name, value: api.BP_OK,
            "set_var_f64": lambda name, value: api.BP_OK,
            "spawn_enemy": self._spawn_enemy,
            "remove_entity": lambda entity_id: api.BP_ERR_REJECTED,
            "damage_entity": lambda entity_id, amount: api.BP_ERR_REJECTED,
            "damage_player": lambda index, amount: api.BP_ERR_REJECTED,
            "spawn_drop": lambda row, col, name: api.BP_OK,
            "set_time_scale": self._time_scale,
            "request_pause": self._pause,
            "apply_item_effect": lambda name: api.BP_OK,
            "rng_next": lambda: 12345,
            "rng_range": lambda low, high: low,
            "register_mode": self._register_mode,
            "level_index": lambda: 1,
            "phase": lambda: api.BP_PHASE_RUNNING,
            "step_index": lambda: 0,
            "player_count": lambda: 1,
            "bombs_snapshot": lambda out, capacity: 0,
            "set_tile": self._set_tile,
            "set_player_start": self._set_player_start,
        }
        field_types = dict(api.BpHostApi._fields_)
        for name, function in bindings.items():
            wrapped = field_types[name](function)
            self._keep_alive.append(wrapped)
            setattr(self.host, name, wrapped)
        self.host.reserved_mapgen = None
        self.host.reserved_ai = None
        self.host.reserved_win = None

    # ---- 内部 ----
    def _has_feature(self, name):
        text = api.decode(name)
        return 1 if text in ("event.subscribe", "world.read", "mode.register", "mode.hooks",
                             "world.read.bombs", "world.read.meta", "world.read.players") else 0

    def _log(self, level, message):
        self.logs.append((level, api.decode(message)))

    def _alloc(self, size):
        return C.cast(C.create_string_buffer(size), C.c_void_p)

    def _tile_at(self, row, col):
        return self.surfaces.get((row, col), api.BP_TILE_EMPTY)

    def _enemies(self, out, capacity):
        total = len(self.enemy_spawns)
        if not out or capacity <= 0:
            return total
        for index in range(min(capacity, total)):
            row, col, hp = self.enemy_spawns[index]
            out[index].size = C.sizeof(api.BpEnemyInfo)
            out[index].entityId = index + 1
            out[index].row, out[index].col = row, col
            out[index].hp, out[index].maxHp, out[index].alive = hp, hp, 1
        return total

    def _player_state(self, index, out):
        if not out or index != 0:
            return api.BP_ERR_INVALID_ARG
        out.contents.size = C.sizeof(api.BpPlayerState)
        out.contents.row, out.contents.col = 5, 6
        out.contents.lives, out.contents.maxLives = 3, 3
        out.contents.bombRange, out.contents.maxBombs = 2, 3
        out.contents.ghost = 0
        return api.BP_OK

    def _subscribe(self, event_type, callback, user):
        self.subscriptions.append((event_type, callback))
        return api.BP_OK

    def _spawn_enemy(self, row, col, hp):
        self.enemy_spawns.append((row, col, hp))
        return api.BP_OK

    def _set_tile(self, row, col, tile):
        self.surfaces[(row, col)] = tile
        return api.BP_OK

    def _set_player_start(self, row, col):
        self.player_start = (row, col)
        return api.BP_OK

    def _register_mode(self, desc_ptr):
        desc = desc_ptr.contents
        self.modes.append({
            "id": api.decode(desc.id),
            "name": api.decode(desc.name),
            "flags": desc.flags,
            "lives": desc.playerLives,
        })
        return api.BP_OK

    def _time_scale(self, scale):
        self.time_scales.append(scale)
        return api.BP_OK

    def _pause(self, paused):
        self.pauses.append(paused)
        return api.BP_OK

    def fire_event(self, event_type, payload):
        """按宿主的做法：把载荷包进 BpGameEvent 再回调"""
        event = api.BpGameEvent()
        event.size = C.sizeof(api.BpGameEvent)
        event.type = event_type
        event.payload = C.cast(C.pointer(payload), C.c_void_p)

        for subscribed, callback in self.subscriptions:
            if subscribed in (api.BP_EVENT_ANY, event_type):
                callback(C.byref(event), None)


# ---------------------------------------------------------------- 测试主体

def main():
    parser = argparse.ArgumentParser(description="验证 Python 模组能被加载（shim 端到端）")
    parser.add_argument("--mod", required=True, help="模组目录（含 mod.json / mod.py / <id>.dll / bp_mod_api.py）")
    parser.add_argument("--keep", action="store_true", help="保留临时目录（排查用）")
    args = parser.parse_args()

    mod_source = os.path.abspath(args.mod)
    mod_json_path = os.path.join(mod_source, "mod.json")
    if not os.path.isfile(mod_json_path):
        print(f"[失败] 找不到 {mod_json_path}")
        return 2

    # 模组 id 与 DLL 名从 mod.json 读（和游戏的规则一致）
    import json
    with open(mod_json_path, "r", encoding="utf-8") as handle:
        manifest = json.load(handle)
    mod_id = manifest.get("id", os.path.basename(mod_source))
    native_lib = manifest.get("nativeLib") or mod_id

    # 每个模组一个临时目录：绝不往仓库里写东西
    workdir = tempfile.mkdtemp(prefix="bp-python-shim-")
    try:
        for name in ("mod.py", "mod.json", "bp_mod_api.py", native_lib + ".dll"):
            source = os.path.join(mod_source, name)
            if not os.path.isfile(source):
                print(f"[失败] 模组目录缺少 {name}（参考 sdk/samples/hello_python）")
                return 2
            shutil.copy2(source, os.path.join(workdir, name))

        checks = []

        def check(condition, what):
            checks.append((bool(condition), what))
            print(f"  {'[OK]  ' if condition else '[FAIL]'} {what}")
            return bool(condition)

        dll_path = os.path.join(workdir, native_lib + ".dll")
        engine = FakeEngine()

        print("=== 1) 加载 shim 并做版本握手 ===")
        try:
            library = C.CDLL(dll_path)
        except OSError as error:
            print(f"[失败] 加载 DLL 失败：{error}")
            return 2

        check(hasattr(library, "bp_mod_entry"), "DLL 导出了 bp_mod_entry")
        entry = library.bp_mod_entry
        entry.argtypes = [C.c_char_p, C.POINTER(api.BpHostApi)]
        entry.restype = C.POINTER(api.BpModApi)

        check(not entry(b"9.9.9", C.byref(engine.host)), "版本不匹配时 shim 返回 NULL（明确拒绝）")
        mod = entry(api.BP_MOD_API_VERSION, C.byref(engine.host))
        check(bool(mod), "版本匹配时拿到函数表")
        if not mod:
            return 1

        print()
        print("=== 2) init：shim 去启动 CPython 并跑 mod.py 的 bp_init ===")
        info = api.BpModInfo()
        info.size = C.sizeof(api.BpModInfo)
        info.abiVersionMajor = 0
        info.abiVersionMinor = 1
        info.pointerSize = C.sizeof(C.c_void_p)
        info.endianLittle = 1
        info.buildId = api.build_id()
        info.id = api.encode(mod_id)
        info.name = api.encode("python shim check")
        info.version = api.encode("0.1.0")
        info.modDir = api.encode(workdir)

        result = mod.contents.init(C.byref(info), C.byref(engine.host))
        check(result == 0, f"init 返回 0（实际 {result}；非 0 说明找不到 CPython 或 mod.py 报错）")

        joined = " | ".join(text for _, text in engine.logs)
        check("你好" in joined, f"mod.py 的 bp_init 跑到了（日志：{joined[:120]}）")
        check("read=1" in joined, "模组问到了能力（world.read）")
        check(len(engine.modes) == 1, f"模组注册了 1 个模式（实际 {len(engine.modes)}）")
        if engine.modes:
            check(engine.modes[0]["id"] == "python_arena", f"模式 id 正确（{engine.modes[0]['id']}）")
            check(engine.modes[0]["lives"] == 3, "模式声明的默认生命数读对了")
        check(len(engine.subscriptions) == 1, "模组订阅了事件（ANY 一次就够）")

        print()
        print("=== 3) 事件：C -> Python ===")
        before = len(engine.logs)
        boom = api.BpEventExplosion()
        boom.size = C.sizeof(api.BpEventExplosion)
        boom.row, boom.col, boom.destroyedBricks, boom.range = 3, 4, 0, 2
        engine.fire_event(api.BP_EVENT_BOMB_EXPLODED, boom)
        new_lines = [text for _, text in engine.logs[before:]]
        check(any("爆炸" in text and "(3, 4)" in text for text in new_lines),
              f"Python 收到了爆炸事件的载荷：{new_lines}")

        before = len(engine.logs)
        used = api.BpEventDrop()
        used.size = C.sizeof(api.BpEventDrop)
        used.entityId = -1
        used.row, used.col = 1, 2
        used.itemName = api.encode("生命药水")
        engine.fire_event(api.BP_EVENT_ITEM_USED, used)
        new_lines = [text for _, text in engine.logs[before:]]
        check(any("生命药水" in text for text in new_lines),
              f"道具名跨语言传对了：{new_lines}")

        print()
        print("=== 4) on_tick ===")
        mod.contents.on_tick(1.0 / 60.0)
        check(True, "on_tick 可调用（mod.py 没实现也不崩）")

        print()
        print("=== 5) 模式钩子：地图 / AI / 胜负 ===")
        if mod.contents.on_generate_map:
            request = api.BpMapGenRequest()
            request.size = C.sizeof(api.BpMapGenRequest)
            request.level = 1
            request.rows, request.cols = 11, 13
            request.seed = 1234
            request.brickCount, request.enemyCount = 20, 4

            gen_result = api.BpMapGenResult()
            gen_result.size = C.sizeof(api.BpMapGenResult)
            answered = mod.contents.on_generate_map(C.byref(request), C.byref(gen_result))
            check(answered == 1 and gen_result.ok == 1, "Python 接管了地图生成")
            check(len(engine.enemy_spawns) == 4, f"它登记了 4 个敌人（实际 {len(engine.enemy_spawns)}）")
            bricks = sum(1 for tile in engine.surfaces.values() if tile == api.BP_TILE_BRICK)
            check(bricks > 0, f"它铺了 {bricks} 块砖")
            check(engine.player_start is not None, f"它定了出生点：{engine.player_start}")

        if mod.contents.on_enemy_think:
            think_request = api.BpEnemyThinkRequest()
            think_request.size = C.sizeof(api.BpEnemyThinkRequest)
            think_request.enemyId = 7
            think_request.row, think_request.col = 2, 2       # 玩家在 (5,6)：应该向下/向右
            think_request.x, think_request.y = 2.5, 2.5
            think_request.hp, think_request.maxHp = 3, 3
            think_request.stepIndex = 10
            think_request.frameTime = 1.0 / 60.0

            think_result = api.BpEnemyThinkResult()
            think_result.size = C.sizeof(api.BpEnemyThinkResult)
            acted = mod.contents.on_enemy_think(C.byref(think_request), C.byref(think_result))
            check(acted == 1 and think_result.acted == 1, "Python 接管了敌人 AI")
            check(think_result.direction in (1, 3), f"它选了朝玩家的方向（{think_result.direction}）")

        if mod.contents.on_check_win:
            win_result = api.BpWinResult()
            win_result.size = C.sizeof(api.BpWinResult)
            decided = mod.contents.on_check_win(C.byref(win_result))
            # 场景里还有 4 个敌人、玩家活着 -> Python 应该回答"还没结论"
            check(decided == 0, f"还没到胜负条件时它不抢着判（返回 {decided}）")

            engine.enemy_spawns.clear()
            win_result2 = api.BpWinResult()
            win_result2.size = C.sizeof(api.BpWinResult)
            decided2 = mod.contents.on_check_win(C.byref(win_result2))
            check(decided2 == 1 and win_result2.decided == 1 and win_result2.victory == 1,
                  "敌人清空后它判了胜利")

        print()
        print("=== 6) shutdown ===")
        if mod.contents.shutdown:
            mod.contents.shutdown()
            check(any("收工" in text for _, text in engine.logs), "bp_shutdown 跑到了")

        failed = [what for ok, what in checks if not ok]
        print()
        print("-" * 60)
        print(f"检查了 {len(checks)} 项，失败 {len(failed)} 项")
        for what in failed:
            print(f"  [FAIL] {what}")
        return 1 if failed else 0
    finally:
        if args.keep:
            print(f"（临时目录保留：{workdir}）")
        else:
            shutil.rmtree(workdir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
