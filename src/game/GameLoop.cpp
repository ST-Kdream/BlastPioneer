#include "GameLoop.h"
#include "GameEngine.h"

#include <QTimer>
#include <cmath>

namespace bp
{

GameLoop::GameLoop(GameEngine* engine, QObject* parent)
	: QObject(parent), m_engine(engine)
{
	m_timer = new QTimer(this);
	connect(m_timer, &QTimer::timeout, this, &GameLoop::onTimeout);
}

void GameLoop::setStepSeconds(double seconds)
{
	if (seconds > 0.0) { m_stepSeconds = seconds; }
}

void GameLoop::start(int renderIntervalMs)
{
	if (m_running) { return; }

	const int interval = (renderIntervalMs > 0) ? renderIntervalMs
												: qMax(1, static_cast<int>(m_stepSeconds * 1000.0));
	m_running = true;
	resetAccumulator();
	m_timer->start(interval);

	if (m_onRunningChanged) { m_onRunningChanged(true); }
}

void GameLoop::stop()
{
	if (!m_running) { return; }
	m_running = false;
	m_timer->stop();
	if (m_onRunningChanged) { m_onRunningChanged(false); }
}

//暂停：引擎冻结规则，但循环继续跑（这样爆炸痕/动画这类纯表现计时仍会推进）
void GameLoop::pause()
{
	if (m_engine && m_engine->phase() == Phase::Running)
	{
		m_engine->setPhase(Phase::Paused);
	}
}

//恢复：清空累加器，避免把暂停期间的时间补成一大堆逻辑步
void GameLoop::resume()
{
	if (m_engine && m_engine->phase() == Phase::Paused)
	{
		resetAccumulator();
		m_engine->setPhase(Phase::Running);
	}
}

//重置累加器与时钟（开始/恢复播放时调用，避免把"暂停期间的时间"补进去）
void GameLoop::resetAccumulator()
{
	m_accumulator = 0.0;
	m_clock.restart();
	m_clockValid = true;
}

//时间倍率：把"真实经过的时间"乘一个系数再进累加器。
//为什么夹在 [0, 4]：再快就等于每帧补一大堆逻辑步，确定性会被破坏
//（而且玩家看到的是"瞬移"）；再慢到 0 就是冻结，与 pause 的区别是
//pause 会改引擎阶段（界面据此显示"已暂停"），时间倍率不会。
void GameLoop::setTimeScale(double scale)
{
	if (!(scale >= 0.0)) { scale = 1.0; }     //NaN 兜底
	m_timeScale = qBound(0.0, scale, 4.0);
}

//手动推进一逻辑步（测试用）
void GameLoop::advanceStep()
{
	if (!m_engine) { return; }

	m_engine->tick(m_stepSeconds);
	if (m_onStepped) { m_onStepped(1); }
}

void GameLoop::onTimeout()
{
	if (!m_engine) { return; }

	if (!m_clockValid)
	{
		resetAccumulator();
		return;
	}

	const double elapsed = m_clock.restart() / 1000.0;
	pump(elapsed);
}

//把一次"真实经过时间"喂给累加器
void GameLoop::pump(double elapsedSeconds)
{
	if (!m_engine) { return; }
	if (elapsedSeconds < 0.0) { elapsedSeconds = 0.0; }

	//实测帧率：滑动平均，用于自适应画质与显示
	if (elapsedSeconds > 0.0)
	{
		m_smoothedFrameSeconds = m_smoothedFrameSeconds * 0.9 + elapsedSeconds * 0.1;
	}

	m_accumulator += elapsedSeconds * m_timeScale;

	//一次最多补 maxStepsPerTick 步；超出的丢弃（宁可慢一点，也不要瞬移一屏）
	int advanced = 0;
	while (m_accumulator >= m_stepSeconds && advanced < m_maxStepsPerTick)
	{
		m_engine->tick(m_stepSeconds);
		m_accumulator -= m_stepSeconds;
		++advanced;
	}

	if (m_accumulator >= m_stepSeconds)
	{
		//还有欠账：说明设备跟不上，丢弃并记录
		const double dropped = std::floor(m_accumulator / m_stepSeconds);
		m_droppedSteps += static_cast<int>(dropped);
		m_accumulator = std::fmod(m_accumulator, m_stepSeconds);
	}

	if (advanced > 0 && m_onStepped) { m_onStepped(advanced); }
}

} // namespace bp
