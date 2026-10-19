/* ============================================================================
 * bp_python_shim.c —— 让"用 Python 写的模组"能被游戏当成原生模组加载
 *
 * 它是**通用胶水**，不含任何模组专属逻辑：
 *   游戏加载本 DLL -> 我们返回自己的 BpModApi -> 每个回调转发给 mod.py 里的同名函数
 *
 * 为什么做成 DLL 而不是把 CPython 编进游戏：
 *   1. 游戏本体不需要 CPython 的头文件/导入库，也不需要跟着 Python 版本走；
 *   2. 不装 Python 的玩家完全不受影响（找不到运行时就返回错误，模组页显示原因）；
 *   3. 这是"ABI 对多语言友好"这件事真正的落地证明 —— 同一张函数表，C++ 与 Python 共用。
 *
 * 四条实现约定（都来自契约或踩过的坑）：
 *   · 不跨边界抛异常：Python 的异常在这里 print 掉，绝不穿过 ABI；
 *   · 不保存引擎给的指针：事件载荷当场解析成普通值再交给 Python；
 *   · **每次碰 Python 之前都 Ensure 一次 GIL**：游戏线程不认识 Python，
 *     而用 ctypes 当宿主时 ctypes 会在调用期间释放 GIL —— 不 Ensure 就是未定义行为
 *     （实测表现：init 里读一个指针就 Access Violation）；
 *   · 不调用 Py_FinalizeEx：DLL 永不卸载，解释器活到进程结束最省心。
 *
 * 本文件含中文注释，所以**必须带 /utf-8 编译**（build_shim.bat 已经加了）。
 * 不带它 MSVC 会按本地代码页解码，某个汉字的字节会吃掉字符串的结尾引号，
 * 报出来的错还完全不相关（真的踩过：C2055 "应输入形参表"）。
 * ========================================================================== */

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bp_mod_api.h"

/* ------------------------------------------------------------------ CPython */

/* 需要 12 个符号。都在稳定 ABI 里（python3.dll 也导出），所以不必链接导入库。 */
typedef void  (*fn_Py_InitializeEx)(int);
typedef int   (*fn_Py_IsInitialized)(void);
typedef int   (*fn_PyRun_SimpleStringFlags)(const char*, void*);
typedef void* (*fn_PyImport_ImportModule)(const char*);
typedef void* (*fn_PyObject_GetAttrString)(void*, const char*);
typedef void* (*fn_PyObject_CallFunction)(void*, const char*, ...);
typedef void  (*fn_Py_DecRef)(void*);
typedef void* (*fn_PyErr_Occurred)(void);
typedef void  (*fn_PyErr_Print)(void);
typedef long  (*fn_PyLong_AsLong)(void*);
typedef int   (*fn_PyGILState_Ensure)(void);
typedef void  (*fn_PyGILState_Release)(int);
/* 初始化完必须把 GIL 还回去：模组的 init 跑在启动管线的**工作线程**里，
 * 而那个线程随后就结束了。如果它一直持有 GIL，游戏线程下一次 PyGILState_Ensure
 * 就会永远等下去 —— 实测表现是"模式一开始就卡住、日志停在切界面"。
 * PyEval_SaveThread() 释放 GIL 并交出线程状态，之后任何线程用 Ensure 都能进。 */
typedef void* (*fn_PyEval_SaveThread)(void);

static struct
{
    HMODULE library;
    fn_Py_InitializeEx initializeEx;
    fn_Py_IsInitialized isInitialized;
    fn_PyRun_SimpleStringFlags runString;
    fn_PyImport_ImportModule importModule;
    fn_PyObject_GetAttrString getAttr;
    fn_PyObject_CallFunction callFunction;
    fn_Py_DecRef decRef;
    fn_PyErr_Occurred errOccurred;
    fn_PyErr_Print errPrint;
    fn_PyLong_AsLong asLong;
    fn_PyGILState_Ensure gilEnsure;
    fn_PyGILState_Release gilRelease;
    fn_PyEval_SaveThread saveThread;
} g_py;

static const BpHostApi* g_host = NULL;
static char g_modDir[MAX_PATH * 2] = { 0 };
static int  g_pythonReady = 0;

/* mod.py 里的约定函数（找不到就是 NULL -> 对应能力自动变成"交回引擎默认"） */
static void* g_modModule = NULL;
static void* g_fnShutdown = NULL;
static void* g_fnTick = NULL;
static void* g_fnEvent = NULL;
static void* g_fnGenerateMap = NULL;
static void* g_fnEnemyThink = NULL;
static void* g_fnCheckWin = NULL;
static void* g_fnModeStart = NULL;
static void* g_fnModeStop = NULL;

static void shim_log(int level, const char* text)
{
    if (g_host && g_host->log)
    {
        BpStr message;
        message.utf8 = text;
        message.len = -1;
        g_host->log(level, message);
    }
}

/* ------------------------------------------------------------------ 符号与 GIL */

static int resolve_symbols(void)
{
    if (!g_py.library) { return 0; }

    g_py.initializeEx = (fn_Py_InitializeEx)GetProcAddress(g_py.library, "Py_InitializeEx");
    g_py.isInitialized = (fn_Py_IsInitialized)GetProcAddress(g_py.library, "Py_IsInitialized");
    g_py.runString = (fn_PyRun_SimpleStringFlags)GetProcAddress(g_py.library, "PyRun_SimpleStringFlags");
    g_py.importModule = (fn_PyImport_ImportModule)GetProcAddress(g_py.library, "PyImport_ImportModule");
    g_py.getAttr = (fn_PyObject_GetAttrString)GetProcAddress(g_py.library, "PyObject_GetAttrString");
    g_py.callFunction = (fn_PyObject_CallFunction)GetProcAddress(g_py.library, "PyObject_CallFunction");
    g_py.errOccurred = (fn_PyErr_Occurred)GetProcAddress(g_py.library, "PyErr_Occurred");
    g_py.errPrint = (fn_PyErr_Print)GetProcAddress(g_py.library, "PyErr_Print");
    g_py.asLong = (fn_PyLong_AsLong)GetProcAddress(g_py.library, "PyLong_AsLong");
    g_py.gilEnsure = (fn_PyGILState_Ensure)GetProcAddress(g_py.library, "PyGILState_Ensure");
    g_py.gilRelease = (fn_PyGILState_Release)GetProcAddress(g_py.library, "PyGILState_Release");
    g_py.saveThread = (fn_PyEval_SaveThread)GetProcAddress(g_py.library, "PyEval_SaveThread");

    /* Py_DecRef 在有些版本里叫 _Py_DecRef：两个名字都试 */
    g_py.decRef = (fn_Py_DecRef)GetProcAddress(g_py.library, "Py_DecRef");
    if (!g_py.decRef) { g_py.decRef = (fn_Py_DecRef)GetProcAddress(g_py.library, "_Py_DecRef"); }

    /* 少一个就当这个候选不可用（宁可整块不用，也不要半残） */
    if (!g_py.initializeEx || !g_py.isInitialized || !g_py.runString || !g_py.importModule
        || !g_py.getAttr || !g_py.callFunction || !g_py.decRef || !g_py.errOccurred
        || !g_py.errPrint || !g_py.asLong || !g_py.gilEnsure || !g_py.gilRelease || !g_py.saveThread)
    {
        return 0;
    }
    return 1;
}

/* 进/出 Python 的世界。所有回调都必须包一层（见文件头的第 3 条约定） */
static int  enter_gil(void) { return g_py.gilEnsure ? g_py.gilEnsure() : -1; }
static void leave_gil(int state) { if (g_py.gilRelease && state >= 0) { g_py.gilRelease(state); } }

/* 出了问题就打印出来（绝不外抛），返回 0 */
static void print_python_error(void)
{
    if (g_py.errOccurred && g_py.errOccurred() && g_py.errPrint)
    {
        g_py.errPrint();
    }
}

/* ------------------------------------------------------------------ 找 CPython */

/* 试一个候选 DLL：**加载 + 解析符号**都成功才算可用。
 * 为什么"加载成功"还不够：python3.dll 只是稳定 ABI 的转发 DLL，
 * 它不一定导出我们需要的对象层调用（PyObject_CallFunction 之类）。 */
static int try_candidate(const char* path)
{
    HMODULE module;

    if (!path || !*path) { return 0; }

    module = LoadLibraryA(path);
    if (!module) { return 0; }

    g_py.library = module;
    if (resolve_symbols()) { return 1; }

    FreeLibrary(module);
    memset(&g_py, 0, sizeof(g_py));
    return 0;
}

/* 在某个目录里按版本从上往下试 python3XX.dll */
static int try_candidate_versioned(const char* dir)
{
    static const int versions[] = { 314, 313, 312, 311, 310, 39, 38 };
    char path[MAX_PATH * 2];
    int i;

    for (i = 0; i < (int)(sizeof(versions) / sizeof(versions[0])); ++i)
    {
        sprintf_s(path, sizeof(path), "%s\\python%d.dll", dir, versions[i]);
        if (try_candidate(path)) { return 1; }
    }
    return 0;
}

/* 注册表：官方安装器记下的 PythonCore 安装位置（py -0p 用的同一份信息） */
static int try_load_from_registry(void)
{
    static const char* roots[] = { "Software\\Python\\PythonCore", "SOFTWARE\\Python\\PythonCore" };
    static const HKEY hives[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    static const char* versions[] = { "3.14", "3.13", "3.12", "3.11", "3.10", "3.9", "3.8" };
    char subkey[256];
    char dir[MAX_PATH * 2];
    int h;
    int v;

    for (h = 0; h < 2; ++h)
    {
        for (v = 0; v < (int)(sizeof(versions) / sizeof(versions[0])); ++v)
        {
            HKEY key = NULL;
            DWORD size = sizeof(dir);
            DWORD type = 0;
            LONG read;
            size_t length;

            sprintf_s(subkey, sizeof(subkey), "%s\\%s\\InstallPath", roots[h], versions[v]);
            if (RegOpenKeyExA(hives[h], subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) { continue; }

            read = RegQueryValueExA(key, NULL, NULL, &type, (LPBYTE)dir, &size);
            RegCloseKey(key);
            if (read != ERROR_SUCCESS) { continue; }

            /* 注册表里的值可能带结尾反斜杠 */
            length = strlen(dir);
            if (length > 0 && (dir[length - 1] == '\\' || dir[length - 1] == '/'))
            {
                dir[length - 1] = '\0';
            }
            if (try_candidate_versioned(dir)) { return 1; }
        }
    }
    return 0;
}

/* 五级优先级（交接文档 §3.2）：模组自带 > exe 同级 > PATH > 注册表 > 默认安装目录 */
static int find_python(void)
{
    char exePath[MAX_PATH * 2];
    char dir[MAX_PATH * 2];
    char path[MAX_PATH * 2];
    const char* localAppData;

    /* 1) 模组自带一份运行时：<modDir>\python\ */
    sprintf_s(dir, sizeof(dir), "%s\\python", g_modDir);
    if (try_candidate_versioned(dir)) { return 1; }
    sprintf_s(path, sizeof(path), "%s\\python3.dll", dir);
    if (try_candidate(path)) { return 1; }

    /* 2) 游戏 exe 同级目录（将来随游戏分发一份） */
    if (GetModuleFileNameA(NULL, exePath, sizeof(exePath)) > 0)
    {
        char* slash;
        strcpy_s(dir, sizeof(dir), exePath);
        slash = strrchr(dir, '\\');
        if (slash) { *slash = '\0'; }
        if (try_candidate_versioned(dir)) { return 1; }
        sprintf_s(path, sizeof(path), "%s\\python3.dll", dir);
        if (try_candidate(path)) { return 1; }
    }

    /* 3) PATH 上的 python3.dll：它是转发 DLL，可能缺对象层调用。
     *    所以先顺着它找到完整 runtime 的目录，优先用那里的 python3XX.dll */
    {
        HMODULE forwarder = LoadLibraryA("python3.dll");
        if (forwarder)
        {
            char forwarderPath[MAX_PATH * 2];
            if (GetModuleFileNameA(forwarder, forwarderPath, sizeof(forwarderPath)) > 0)
            {
                char* slash = strrchr(forwarderPath, '\\');
                if (slash) { *slash = '\0'; }
                if (try_candidate_versioned(forwarderPath))
                {
                    FreeLibrary(forwarder);
                    return 1;
                }
            }
            FreeLibrary(forwarder);
        }
    }

    /* 4) 官方安装器记在注册表里的位置 */
    if (try_load_from_registry()) { return 1; }

    /* 5) "仅为我安装"的默认目录 */
    localAppData = getenv("LOCALAPPDATA");
    if (localAppData)
    {
        static const char* versions[] = { "Python314", "Python313", "Python312", "Python311",
                                          "Python310", "Python39", "Python38" };
        int i;
        for (i = 0; i < (int)(sizeof(versions) / sizeof(versions[0])); ++i)
        {
            sprintf_s(dir, sizeof(dir), "%s\\Programs\\Python\\%s", localAppData, versions[i]);
            if (try_candidate_versioned(dir)) { return 1; }
        }
    }

    return 0;
}

/* ------------------------------------------------------------------ 小工具 */

/* 把 BpStr 拷进 C 缓冲区（载荷只在回调期间有效，要留就复制） */
static void copy_bpstr(const BpStr* text, char* out, size_t capacity)
{
    int length;
    size_t count;

    if (!out || capacity == 0) { return; }
    out[0] = '\0';
    if (!text || !text->utf8) { return; }

    length = text->len;
    if (length < 0)
    {
        strcpy_s(out, capacity, text->utf8);
        return;
    }
    count = (size_t)length < capacity - 1 ? (size_t)length : capacity - 1;
    memcpy(out, text->utf8, count);
    out[count] = '\0';
}

/* 把字符串转义成 Python 单引号字面量（只处理反斜杠与单引号，路径够用） */
static void escape_for_python(const char* input, char* output, size_t capacity)
{
    size_t out = 0;
    size_t i;

    if (capacity < 3) { return; }

    output[out++] = '\'';
    for (i = 0; input[i] != '\0' && out + 3 < capacity; ++i)
    {
        if (input[i] == '\\' || input[i] == '\'') { output[out++] = '\\'; }
        output[out++] = input[i];
    }
    output[out++] = '\'';
    output[out] = '\0';
}

/* 取 mod 模块里的一个函数（没有就是 NULL）。调用方必须持有 GIL */
static void* find_mod_function(const char* name)
{
    void* function;
    if (!g_py.getAttr || !g_modModule) { return NULL; }

    function = g_py.getAttr(g_modModule, name);
    if (!function) { print_python_error(); }
    return function;
}

/* ------------------------------------------------------------------ 启动 Python */

/* 跑设置脚本 + 调 bp_init。调用方必须持有 GIL */
static int start_python_body(const char* modDir)
{
    char script[2048];
    char escapedDir[1024];
    void* apiModule;
    void* resultObject;
    long result;

    escape_for_python(modDir, escapedDir, sizeof(escapedDir));

    /* 关键技巧：把宿主函数表**当作整数**传进去，Python 侧用 ctypes.cast 还原。
       这样就不需要 PyLong_FromVoidPtr 之类的对象 API，符号表能压到 12 个 */
    sprintf_s(script, sizeof(script),
        "import sys\n"
        "sys.path.insert(0, %s)\n"
        "import bp_mod_api\n"
        "bp_mod_api._HOST_PTR = %llu\n"
        "bp_mod_api._MOD_DIR = %s\n"
        "import mod\n"
        "bp_mod_api._MOD = mod\n"
        "bp_mod_api._RESULT = mod.bp_init(%llu, %s)\n",
        escapedDir,
        (unsigned long long)(uintptr_t)g_host,
        escapedDir,
        (unsigned long long)(uintptr_t)g_host,
        escapedDir);

    if (g_py.runString(script, NULL) != 0)
    {
        shim_log(BP_LOG_ERROR, "python shim: mod.py setup failed (see the Python traceback)");
        print_python_error();
        return 0;
    }

    apiModule = g_py.importModule("bp_mod_api");
    if (!apiModule)
    {
        print_python_error();
        return 0;
    }

    resultObject = g_py.getAttr(apiModule, "_RESULT");
    if (!resultObject)
    {
        print_python_error();
        return 0;
    }
    result = g_py.asLong(resultObject);
    g_py.decRef(resultObject);

    if (result != 0)
    {
        shim_log(BP_LOG_WARN, "python shim: mod.bp_init refused to load (non-zero return)");
        return 0;
    }

    g_modModule = g_py.importModule("mod");
    if (!g_modModule)
    {
        print_python_error();
        return 0;
    }

    g_fnShutdown = find_mod_function("bp_shutdown");
    g_fnTick = find_mod_function("bp_tick");
    g_fnEvent = find_mod_function("bp_on_event");
    g_fnGenerateMap = find_mod_function("bp_generate_map");
    g_fnEnemyThink = find_mod_function("bp_enemy_think");
    g_fnCheckWin = find_mod_function("bp_check_win");
    g_fnModeStart = find_mod_function("bp_mode_start");
    g_fnModeStop = find_mod_function("bp_mode_stop");

    g_pythonReady = 1;
    return 1;
}

/* 解释器初始化必须在**没有 GIL** 的线程上做一次；之后所有 Python 调用都要 Ensure */
static int start_python(const char* modDir)
{
    int gil;
    int ok;

    if (!g_py.isInitialized())
    {
        g_py.initializeEx(0);          /* 0 = 不注册信号处理器（我们不是主程序） */
        /* 立刻把 GIL 还回去：调用线程（启动管线的工作线程）马上就会结束 */
        g_py.saveThread();
        if (!g_py.isInitialized())
        {
            shim_log(BP_LOG_ERROR, "python shim: Py_InitializeEx failed");
            return 0;
        }
    }

    gil = enter_gil();
    ok = start_python_body(modDir);
    leave_gil(gil);
    return ok;
}

/* ------------------------------------------------------------------ 事件 */

static void BP_CALL on_event(const BpGameEvent* event, void* user)
{
    int gil;
    int row = -1;
    int col = -1;
    int amount = 0;
    char text[192];
    void* result;

    (void)user;
    if (!g_pythonReady || !g_fnEvent || !event) { return; }

    text[0] = '\0';

    /* 载荷按类型解析成**普通值**再交给 Python：作者不该碰指针 */
    switch (event->type)
    {
    case BP_EVENT_BOMB_PLACED:
    {
        const BpEventBomb* payload = (const BpEventBomb*)event->payload;
        if (payload) { row = payload->row; col = payload->col; amount = payload->range; }
        break;
    }
    case BP_EVENT_BOMB_EXPLODED:
    {
        const BpEventExplosion* payload = (const BpEventExplosion*)event->payload;
        if (payload) { row = payload->row; col = payload->col; amount = payload->range; }
        break;
    }
    case BP_EVENT_ENEMY_DAMAGED:
    case BP_EVENT_PLAYER_DAMAGED:
    case BP_EVENT_PLAYER_DIED:
    {
        const BpEventDamage* payload = (const BpEventDamage*)event->payload;
        if (payload)
        {
            amount = payload->amount;
            sprintf_s(text, sizeof(text), "%d", payload->livesLeft);
        }
        break;
    }
    case BP_EVENT_DROP_SPAWNED:
    case BP_EVENT_DROP_PICKED:
    case BP_EVENT_ITEM_USED:
    {
        const BpEventDrop* payload = (const BpEventDrop*)event->payload;
        if (payload)
        {
            row = payload->row;
            col = payload->col;
            copy_bpstr(&payload->itemName, text, sizeof(text));
        }
        break;
    }
    case BP_EVENT_LEVEL_START:
    case BP_EVENT_LEVEL_WON:
    case BP_EVENT_LEVEL_LOST:
    {
        const BpEventLevel* payload = (const BpEventLevel*)event->payload;
        if (payload)
        {
            amount = payload->level;
            sprintf_s(text, sizeof(text), "%d", payload->victory);
        }
        break;
    }
    default:
    {
        const BpEventEntity* payload = (const BpEventEntity*)event->payload;
        if (payload) { row = payload->row; col = payload->col; }
        break;
    }
    }

    gil = enter_gil();
    /* 格式串是 4 个 int + 1 个 str：type, row, col, amount, text
       （别再往里塞 length —— 那会让 Python 侧多收一个参数，报"6 个参数"） */
    result = g_py.callFunction(g_fnEvent, "iiiis", (int)event->type, row, col, amount, text);
    if (!result) { print_python_error(); }
    else { g_py.decRef(result); }
    leave_gil(gil);
}

/* ------------------------------------------------------------------ 模式钩子 */

static int32_t BP_CALL on_generate_map(const BpMapGenRequest* req, BpMapGenResult* out)
{
    int gil;
    void* result;
    long answered;

    if (!out) { return 0; }
    if (!g_pythonReady || !g_fnGenerateMap || !req) { return 0; }

    gil = enter_gil();
    result = g_py.callFunction(g_fnGenerateMap, "iiiii",
                               (int)req->level, (int)req->rows, (int)req->cols,
                               (int)req->brickCount, (int)req->enemyCount);
    if (!result)
    {
        print_python_error();
        leave_gil(gil);
        return 0;
    }
    answered = g_py.asLong(result);
    g_py.decRef(result);

    if (answered != 0)
    {
        /* Python 侧应该已经用 host.set_tile / set_player_start 铺好了图 */
        out->ok = 1;
        out->playerStartRow = req->rows / 2;
        out->playerStartCol = req->cols / 2;
    }
    leave_gil(gil);

    return answered != 0 ? 1 : 0;
}

/* Python 侧用 bp_mod_api.think(direction, place_bomb) 打包结果：
 *   0 = 没意见；1 = 原地不动；2..5 = 方向 0..3；6..9 = 方向 0..3 且放弹 */
static int32_t BP_CALL on_enemy_think(const BpEnemyThinkRequest* req, BpEnemyThinkResult* out)
{
    int gil;
    void* result;
    long packed;

    if (!out) { return 0; }
    if (!g_pythonReady || !g_fnEnemyThink || !req) { return 0; }

    gil = enter_gil();
    result = g_py.callFunction(g_fnEnemyThink, "iiiddiiid",
                               (int)req->enemyId, (int)req->row, (int)req->col,
                               (double)req->x, (double)req->y,
                               (int)req->hp, (int)req->maxHp, (int)req->stepIndex,
                               (double)req->frameTime);
    if (!result)
    {
        print_python_error();
        leave_gil(gil);
        return 0;
    }
    packed = g_py.asLong(result);
    g_py.decRef(result);
    leave_gil(gil);

    if (packed <= 0) { return 0; }

    out->acted = 1;
    if (packed == 1)
    {
        out->direction = -1;
        out->placeBomb = 0;
    }
    else
    {
        out->direction = (int)((packed - 2) % 4);
        out->placeBomb = (packed >= 6) ? 1 : 0;
    }
    return 1;
}

/* 0 = 没意见，1 = 赢，2 = 输（v1 限制：结束文案不由 Python 提供） */
static int32_t BP_CALL on_check_win(BpWinResult* out)
{
    int gil;
    void* result;
    long verdict;

    if (!out) { return 0; }
    if (!g_pythonReady || !g_fnCheckWin) { return 0; }

    gil = enter_gil();
    result = g_py.callFunction(g_fnCheckWin, "");
    if (!result)
    {
        print_python_error();
        leave_gil(gil);
        return 0;
    }
    verdict = g_py.asLong(result);
    g_py.decRef(result);
    leave_gil(gil);

    if (verdict <= 0) { return 0; }

    out->decided = 1;
    out->victory = (verdict == 1) ? 1 : 0;
    return 1;
}

static void BP_CALL on_mode_start(BpStr modeId)
{
    int gil;
    char id[128];
    void* result;

    if (!g_pythonReady || !g_fnModeStart) { return; }

    copy_bpstr(&modeId, id, sizeof(id));

    gil = enter_gil();
    result = g_py.callFunction(g_fnModeStart, "s", id);
    if (!result) { print_python_error(); }
    else { g_py.decRef(result); }
    leave_gil(gil);
}

static void BP_CALL on_mode_stop(BpStr modeId)
{
    int gil;
    char id[128];
    void* result;

    if (!g_pythonReady || !g_fnModeStop) { return; }

    copy_bpstr(&modeId, id, sizeof(id));

    gil = enter_gil();
    result = g_py.callFunction(g_fnModeStop, "s", id);
    if (!result) { print_python_error(); }
    else { g_py.decRef(result); }
    leave_gil(gil);
}

/* ------------------------------------------------------------------ 生命周期 */

static void BP_CALL shim_tick(double dt)
{
    int gil;
    void* result;

    if (!g_pythonReady || !g_fnTick) { return; }

    gil = enter_gil();
    result = g_py.callFunction(g_fnTick, "d", dt);
    if (!result) { print_python_error(); }
    else { g_py.decRef(result); }
    leave_gil(gil);
}

static void BP_CALL shim_shutdown(void)
{
    int gil;
    void* result;

    if (!g_pythonReady || !g_fnShutdown) { return; }

    gil = enter_gil();
    result = g_py.callFunction(g_fnShutdown, "");
    if (!result) { print_python_error(); }
    else { g_py.decRef(result); }
    leave_gil(gil);

    g_pythonReady = 0;
}

static BpResult BP_CALL shim_init(const BpModInfo* info, const BpHostApi* host)
{
    if (!info || !host) { return BP_ERR_INVALID_ARG; }

    g_host = host;

    /* 0) 头文件/平台对不上就别硬上（与 C++ 模组同一条契约） */
    if (info->buildId != BP_BUILD_ID)
    {
        shim_log(BP_LOG_ERROR, "python shim: ABI build id mismatch");
        return BP_ERR_UNSUPPORTED;
    }

    /* 1) 记下模组目录（找 python/ 与 sys.path 都要它） */
    copy_bpstr(&info->modDir, g_modDir, sizeof(g_modDir));

    /* 2) 找 CPython；找不到就如实失败（模组页会显示原因，游戏照常启动） */
    if (!g_py.library)
    {
        char loaded[MAX_PATH * 2];
        if (!find_python())
        {
            shim_log(BP_LOG_ERROR,
                     "python shim: CPython not found (tried mod/python, exe dir, PATH, registry, "
                     "LOCALAPPDATA). Install Python 3.8+ or put python3XX.dll in the mod's python folder");
            return BP_ERR_UNSUPPORTED;
        }
        if (!resolve_symbols())
        {
            shim_log(BP_LOG_ERROR, "python shim: CPython found but required symbols are missing (need 3.8+)");
            return BP_ERR_UNSUPPORTED;
        }

        /* 记一条"用的是哪个 DLL"：排查 Python 版本问题时这是第一个要看的 */
        if (GetModuleFileNameA(g_py.library, loaded, sizeof(loaded)) > 0)
        {
            char line[MAX_PATH * 2 + 64];
            sprintf_s(line, sizeof(line), "python shim: using %s", loaded);
            shim_log(BP_LOG_INFO, line);
        }
    }

    /* 3) 启动解释器并跑 mod.py 的 bp_init */
    if (!start_python(g_modDir)) { return BP_ERR_INTERNAL; }

    /* 4) 订阅全部事件（订阅一次 ANY 就够：宿主会按 type 分发） */
    if (host->subscribe) { host->subscribe(BP_EVENT_ANY, &on_event, NULL); }

    shim_log(BP_LOG_INFO, "python shim: mod.py is ready");
    return BP_OK;
}

static const BpModApi g_api = {
    sizeof(BpModApi),
    &shim_init,
    &shim_shutdown,
    &shim_tick,
    &on_generate_map,
    &on_enemy_think,
    &on_check_win,
    &on_mode_start,
    &on_mode_stop,
};

BP_EXPORT const BpModApi* BP_CALL bp_mod_entry(const char* hostApiVersion, const BpHostApi* host)
{
    if (!hostApiVersion || !host) { return NULL; }

    /* 版本不合就明确拒绝：这是契约里留给模组的"优雅退出" */
    if (strcmp(hostApiVersion, BP_MOD_API_VERSION) != 0) { return NULL; }

    g_host = host;
    return &g_api;
}
