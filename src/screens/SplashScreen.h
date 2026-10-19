#pragma once
#include "ScreenBase.h"
#include "BootstrapTask.h"

class QQuickWidget;
class QLabel;
class QThread;

//启动界面（强制显示）：显示 QML 粒子特效，同时在工作线程里执行初始化
//至少显示 3 秒；初始化未完成时继续等待；超过 15 秒按失败处理，直接进主页
class SplashScreen : public ScreenBase
{
	Q_OBJECT

public:
	explicit SplashScreen(QWidget* parent = nullptr);
	~SplashScreen();

	QString screenId() const override { return "Splash"; }
	QString displayTitle() const override { return "启动中"; }
	//常驻：启动界面只跑一次，留在缓存里不销毁（它自己的 onEnter 会用 m_finished 防止重复启动）
	bool persistent() const override { return true; }
	bool canGoBack() const override { return false; }
	bool handleBack() override { return true; }   //启动中按 Esc 不做任何事

	void onEnter(const QVariantMap& args) override;
	void onExit() override;

private slots:
	//初始化进度
	void onBootstrapProgress(int percent, const QString& stepText);
	//初始化完成
	void onBootstrapFinished(const BootstrapResult& result);
	//最短显示时间到
	void onMinTimeReached();
	//看门狗超时
	void onTimeout();

private:
	//启动初始化
	void startBootstrap();
	//停止工作线程（在工作线程里 deleteLater 任务对象）
	void stopBootstrap();
	//条件满足时进入下一个界面
	void tryFinish();
	//上次异常结束（只发生一次）时的一次性提示：安全模式 / 禁用这些模组 / 仍然正常启动
	void showCrashPrompt();
	//按"安全模式"重启进程（把 --safe-mode 加到当前命令行之后）
	void restartInSafeMode();

	QQuickWidget* m_effect = nullptr;
	QLabel* m_stepLabel = nullptr;
	QLabel* m_tipLabel = nullptr;

	QThread* m_thread = nullptr;
	BootstrapTask* m_task = nullptr;

	bool m_minTimeReached = false; //是否已经显示满 3 秒
	bool m_dataReady = false;      //初始化是否已完成
	bool m_finished = false;       //是否已经切走（防止重复触发）

	//--play-mode=<modId>/<modeId>：初始化完成后直接进这个模组模式（调试用）
	QString m_playMode;

	BootstrapResult m_result;
};
