"""
hello_python —— 用 Python 写的原生模组示例

它和 sdk/samples/hello_native（C++ 版）做同一件事，只是逻辑全在 Python 里：
订阅事件、读世界、注册一个模式（自己铺地图、追人的 AI、自己判胜负）。

目录形态（一个 Python 原生模组就是这样）：
    hello_python/
    ├── mod.json          "native": true，nativeLib 指向随目录附带的 DLL
    ├── hello_python.dll  ← bp_python_shim.dll 的副本（改了名）
    ├── bp_mod_api.py     ← 随模组分发（sdk/python/bp_mod_api.py 的副本）
    └── mod.py            ← 作者写的一切（就是本文件）

写模组时只要记住三件事：
  1. `bp_init` 里把宿主函数表收下来（`api.Host(host_ptr)`），返回 0 表示加载成功；
  2. 所有回调都在**游戏线程**、每逻辑步固定 1/60 秒 —— 不要自己开线程；
  3. 随机数必须用 `HOST.rng()`：自己抽 rand 会让联机各端不同步。
"""

import bp_mod_api as api

HOST = None


# ---------------------------------------------------------------- 生命周期

def bp_init(host_ptr, mod_dir):
    """必须：返回 0 = 加载成功；非 0 = 我拒绝加载（引擎会记下原因，不会崩）"""
    global HOST
    HOST = api.Host(host_ptr, mod_dir)

    HOST.log("你好，我是 Python 模组（%s）" % mod_dir)

    # 先问能力再用（和 C 模组同一条规矩：不要假设功能存在）
    HOST.log("能力：read=%d bombs=%d mode=%d" % (
        HOST.has("world.read"), HOST.has("world.read.bombs"), HOST.has("mode.register")))

    # 事件：**只写下面那个 bp_on_event 就够了** —— shim 会自动订阅全部事件并调它。
    # （想按类型细分订阅可以调 HOST.subscribe(...)，但同一类事件不要两边都收）

    # 注册一个模式：会出现在「开始游戏 → 选择模式」里
    result = HOST.register_mode(
        "python_arena",
        "示例：Python 竞技场",
        hint="模组提供的新玩法：一块空场 + 四个缓慢的追兵",
        description="这个模式的地图、敌人 AI 与胜负判定都由 Python 编写。",
        flags=api.BP_MODE_FLAG_SINGLE | api.BP_MODE_FLAG_TAKE_OVER_MAP
              | api.BP_MODE_FLAG_TAKE_OVER_AI | api.BP_MODE_FLAG_TAKE_OVER_WIN,
        player_lives=3,
    )
    HOST.log("注册模式：%s" % ("成功" if result == api.BP_OK else "失败(%%d)" % result))
    return 0


def bp_shutdown():
    HOST.log("python 模组收工")


def bp_tick(dt):
    # 每逻辑步一次。这里什么都不做：回调越少，对帧率的影响越小
    pass


# ---------------------------------------------------------------- 事件

def bp_on_event(event_type, row, col, amount, text):
    """所有事件都从这里进来（shim 替我们订阅了 BP_EVENT_ANY）。

    text 的含义随事件变：道具事件是道具名，伤害事件是剩余生命，关卡事件是 victory。"""
    if event_type == api.BP_EVENT_BOMB_EXPLODED:
        HOST.log("爆炸：(%d, %d) 威力 %d" % (row, col, amount))
    elif event_type == api.BP_EVENT_ITEM_USED:
        HOST.log("用了道具：%s" % text)
    elif event_type == api.BP_EVENT_LEVEL_WON:
        HOST.log("过关：第 %d 关" % amount)


# ---------------------------------------------------------------- 模式钩子

def bp_mode_start(mode_id):
    HOST.log("模式开始：%s" % mode_id)


def bp_mode_stop(mode_id):
    HOST.log("模式结束：%s" % mode_id)


def bp_generate_map(level, rows, cols, brick_count, enemy_count):
    """自己铺地图：返回 0 表示"我不管，引擎用默认地图"

    用 host.set_tile 写格子、host.spawn_enemy 放敌人、host.set_player_start 定出生点。
    种子请用 HOST.rng()（本函数没给 seed 参数时也能抽；要可复现就用 HOST.rng()）。"""
    bricks = 0
    # 注意：砖块不要压在敌人要站的格子上（引擎会拒绝在砖块上放敌人 —— 这是对的）
    for row in range(3, rows - 2, 2):
        for col in range(2, cols - 2, 4):
            if HOST.set_tile(row, col, api.BP_TILE_BRICK) == api.BP_OK:
                bricks += 1

    enemies = 0
    for (row, col) in ((2, 2), (2, cols - 3), (rows - 3, 2), (rows - 3, cols - 3)):
        if HOST.spawn_enemy(row, col, 3) == api.BP_OK:
            enemies += 1

    HOST.set_player_start(rows // 2, cols // 2)
    HOST.log("Python 铺好了地图：%d 块砖、%d 个敌人" % (bricks, enemies))
    return 1


def bp_enemy_think(enemy_id, row, col, x, y, hp, max_hp, step, dt):
    """决定这个敌人这一步往哪走。

    返回 api.think(direction, place_bomb)：
      0 = 没意见（交回引擎默认 AI）
      方向 0 上 / 1 下 / 2 左 / 3 右；direction=-1 表示原地不动
    走位与撞墙判定仍由引擎负责，所以这里不可能把敌人送进墙里。"""
    me = HOST.player_state(0)
    if me is None:
        return 0

    if me.col != col:
        direction = 3 if me.col > col else 2
    elif me.row != row:
        direction = 1 if me.row > row else 0
    else:
        direction = -1

    return api.think(direction, place_bomb=(me.col == col and me.row == row))


def bp_check_win():
    """0 = 我还没意见；1 = 本局赢了；2 = 输了"""
    me = HOST.player_state(0)
    if me is not None and me.lives <= 0:
        return 2
    if HOST.api.enemies_snapshot(None, 0) == 0:
        return 1
    return 0
