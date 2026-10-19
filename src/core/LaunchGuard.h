#pragma once
#include <QString>
#include <QStringList>
#include <QMutex>

// ============================================================
// 崩溃保护与安全模式（契约 §3.3）
//
// 要解决的真实场景：原生模组一崩，整个进程跟着死；如果它每次启动都崩，
// 玩家连"关掉它"的机会都没有。安全模式不是"防崩溃"（进程内做不到），
// 而是**保证游戏还能启动、并且能逐个排除**。
//
// 三个状态（状态机的全部）：
//   Clean     上次正常退出，或成功进过一次主页   -> 正常加载全部原生模组
//   Suspected **第一次**异常结束（计数 = 1）      -> 仍然正常加载，但弹一次提示
//   SafeMode  **连续第二次**异常结束，或手动要求  -> 本次不加载任何原生模组
//
// 迁移（就这四条）：
//   Clean --异常结束--> Suspected --再次异常结束--> SafeMode
//     ^                    |                           |
//     +--- 成功进入主页（计数清零）<--------------------+
//     ^
//     +--- 手动（--safe-mode 或启动时按住 Shift）：本次强制 SafeMode，不写进设置
//
// 三条设计要点：
//   1. **只对本次运行有效**，不落盘成设置项 —— 否则玩家会卡在里面出不来；
//   2. 计数只有在"成功到达主页"时才清零 —— 这是"到达主页"作为健康信号的用途；
//   3. 打点必须在动作**之前**落盘（加载第 N 个模组之前写 stage），
//      因为崩溃时来不及执行任何代码，文件里那一行就是唯一的线索。
//
// 写入一律"临时文件 + 原子替换"（FileManager::writeJson 用 QSaveFile），
// 所以读到的永远是完整的旧内容或完整的新内容；文件坏掉一律当 Clean，
// **绝不因为这份文件坏掉就拒绝启动**。
// ============================================================
class LaunchGuard
{
public:
	enum class State { Clean, Suspected, SafeMode };

	static LaunchGuard& instance();

	// ---------- 本次启动 ----------
	// 读上次结论 -> 判定 -> 写下本次启动记录（running=true, stage="startup"）。
	// manualSafeMode = 命令行 --safe-mode 或启动时按住 Shift。
	// statePath 留空 = Data/launch_state.json（测试会传临时目录里的文件）。
	// **必须在 NativeModHost::loadAll() 之前调用**（它在启动管线的工作线程里跑）
	State startup(bool manualSafeMode, const QString& statePath = QString());

	State state() const;
	bool  blockNativeMods() const;       // true = 本次不加载任何原生模组
	bool  manualSafeMode() const;        // 本次是不是被手动要求的安全模式
	int   consecutiveFailures() const;
	QString     lastExit() const;        // 上次是怎么结束的：clean | crash | unknown
	QString     lastStage() const;       // 上次最后写下的阶段（崩溃定位用）
	QStringList lastNativeMods() const;  // 上次打算加载的原生模组 id
	QString     notes() const;           // 给人看的一句话

	// ---------- 打点（每次打点都立即落盘） ----------
	void markStage(const QString& stage);                 // startup / bootstrap / running / exiting
	void setPlannedNativeMods(const QStringList& ids);    // 加载前记下"本次要加载谁"
	void markNativeModLoading(const QString& modId);      // **每个 LoadLibrary 之前**
	void markNativeModLoaded(const QString& modId);       // 某个模组 init 成功之后
	void markReachedHome();                               // 健康信号：计数清零
	void markCleanExit();                                 // 正常退出流程开始

	// Suspected 状态下的一次性提示：只有第一次返回 true
	bool consumeCrashPrompt();

	// 玩家在崩溃提示里选了"用安全模式启动"：记进文件（下一次启动的判定会用到），
	// 注意它**不写设置** —— 安全模式只对本次运行有效，落盘成设置项玩家就出不来了
	void noteSafeModeRequest();

	// ---------- 诊断 ----------
	QString statePath() const;
	QString lastExitText() const;         // "上次崩在加载 xxx 时" 这类结论
	static QString defaultStatePath();

	// ---------- 测试/调试 ----------
	void resetForTest();

private:
	LaunchGuard() = default;
	LaunchGuard(const LaunchGuard&) = delete;
	LaunchGuard& operator=(const LaunchGuard&) = delete;

	// 读上次记录（文件缺失/坏掉/schema 不认识 -> 当 Clean 并记一条日志）
	void readLastRecord();
	// 把当前内存状态原子落盘；失败只记警告（打点失败不该拖垮游戏）
	void writeRecord();

	// 锁是可变的：所有只读接口也要加锁（打点来自工作线程）
	mutable QMutex m_mutex;

	QString m_statePath;
	State   m_state = State::Clean;
	bool    m_manual = false;
	bool    m_reachedHome = false;
	bool    m_promptConsumed = false;
	bool    m_writeWarned = false;
	bool    m_running = true;                            // 写进文件：本次还没正常结束

	QString m_startedAt;
	QString m_stage;                                     // 本次最后写下的阶段
	QString m_lastExitRead = QStringLiteral("unknown");  // 本次启动读到的结论（只读展示）
	QString m_lastExit = QStringLiteral("unknown");      // 写进文件的结论（本次结束时改写）
	QString m_lastStage;                                 // 上次最后写下的阶段
	QStringList m_lastNativeMods;                        // 上次打算加载的原生模组
	QString m_notes;

	QStringList m_nativeMods;                            // 本次打算加载的
	QStringList m_loadedOk;                              // 本次已经 init 成功的
	int     m_failures = 0;                              // 连续异常结束次数
};
