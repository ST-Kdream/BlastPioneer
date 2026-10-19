#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <functional>

class QTimer;

namespace bp
{

class GameEngine;

// ============================================================
// 渲染插值比例的来源：视图拿它把"上一逻辑帧 → 当前逻辑帧"平滑过渡
//
// 为什么单独抽一个接口：视图需要的只是"现在落在两个逻辑步之间的哪个位置"，
// 用不着整个帧循环对象（它还能 start/stop/pump）。接口收窄之后，
// 视图能做的事就只剩"读一个 0~1 的数"，改不了循环状态。
// ============================================================
class IInterpolationSource
{
public:
	virtual ~IInterpolationSource() = default;
	//0~1：当前渲染时刻落在这两个逻辑步之间的比例
	virtual double alpha() const = 0;
};

// ============================================================
// 游戏主循环：把"真实经过的时间"切成固定大小的逻辑步
//
// 为什么逻辑必须固定步长：
//   规则按"每步 × 步长"推进，步长随帧率变会让同一串输入产生不同结果
//   （旧代码"改帧率就改速度、改放弹频率"就是这么来的）。
//   固定 60Hz 后：同一颗种子 + 同一串输入 = 完全一致的一局，
//   单测能确定性复现，以后联机也能靠重演对齐。
//
// 渲染帧率可以随意：视图用 accumulator / step 在前后两个逻辑帧之间插值。
//
// 两种驱动方式共用同一段代码：
//   start()/stop() 由 QTimer 驱动（运行时）
//   advanceStep()  手动推进（测试用，毫秒级跑完 1000 步）
//
// 同样不使用 Q_OBJECT：用 std::function 回调，测试工程无需 moc。
// ============================================================
class GameLoop : public QObject, public IInterpolationSource
{
public:
	explicit GameLoop(GameEngine* engine, QObject* parent = nullptr);

	// ---------- 回调（替代信号） ----------
	void setSteppedCallback(std::function<void(int)> callback) { m_onStepped = std::move(callback); }
	void setRunningChangedCallback(std::function<void(bool)> callback) { m_onRunningChanged = std::move(callback); }

	// ---------- 逻辑步长 ----------
	double stepSeconds() const { return m_stepSeconds; }
	void   setStepSeconds(double seconds);

	//一次 update 最多补几步逻辑（防止卡顿后实体瞬移一屏）
	int  maxStepsPerTick() const { return m_maxStepsPerTick; }
	void setMaxStepsPerTick(int steps) { m_maxStepsPerTick = qMax(1, steps); }

	// ---------- 驱动 ----------
	void start(int renderIntervalMs = 0);   //0 = 用逻辑步长作为定时器间隔
	void stop();
	bool isRunning() const { return m_running; }

	//暂停/恢复：引擎规则冻结，但循环继续推进"纯表现"计时
	//（这样暂停时爆炸痕不会僵在半帧状态）
	void pause();
	void resume();

	// ---------- 时间倍率（L3：模组的 time.scale） ----------
	//1.0 = 正常，0.0 = 冻结（与 pause 不同：这里不改变"暂停"语义，只是把时间轴放慢/停住），
	//上限由引擎夹在 [0, 4]：再快就等于"一帧补四步"，会把确定性搞坏
	void   setTimeScale(double scale);
	double timeScale() const { return m_timeScale; }

	//手动推进一逻辑步（测试用；运行时不调用）
	void advanceStep();

	//给一次"真实经过时间"喂给累加器（测试可以模拟卡顿）
	void pump(double elapsedSeconds);

	// ---------- 插值比例 ----------
	//渲染层用它做平滑：位置 = 上一逻辑帧位置 * (1 - alpha) + 当前 * alpha
	double accumulator() const { return m_accumulator; }
	double alpha() const override { return m_stepSeconds > 0.0 ? (m_accumulator / m_stepSeconds) : 0.0; }

	// ---------- 实测帧率（秒） ----------
	double smoothedFrameSeconds() const { return m_smoothedFrameSeconds; }
	double measuredFps() const { return m_smoothedFrameSeconds > 0.0 ? (1.0 / m_smoothedFrameSeconds) : 0.0; }

	//被丢弃的逻辑步数（说明设备跟不上；可用于提示或降画质）
	int droppedSteps() const { return m_droppedSteps; }

private:
	void onTimeout();
	void resetAccumulator();

	GameEngine* m_engine = nullptr;
	QTimer*     m_timer = nullptr;
	QElapsedTimer m_clock;
	bool   m_running = false;
	double m_stepSeconds = 1.0 / 60.0;
	double m_accumulator = 0.0;
	double m_timeScale = 1.0;
	int    m_maxStepsPerTick = 4;
	int    m_droppedSteps = 0;
	bool   m_clockValid = false;

	//实测帧率（滑动平均）
	double m_smoothedFrameSeconds = 1.0 / 60.0;

	//回调
	std::function<void(int)>  m_onStepped;
	std::function<void(bool)> m_onRunningChanged;
};

} // namespace bp
