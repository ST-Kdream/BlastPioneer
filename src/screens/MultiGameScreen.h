#pragma once
#include "ScreenBase.h"
#include "GameLoop.h"
#include "MultiGameEngine.h"
#include "NetworkProtocol.h"

#include <QHostAddress>
#include <QMap>
#include <QMetaObject>

#include <memory>

class GameCanvas;
class GameHud;
class QUdpSocket;
class QKeyEvent;

// ============================================================
// 联机对战界面（阶段二起：薄壳）
//
// 这里只做四件事：
//   ① 从 NetSession 取出本次对局的参数（是否主机、玩家表、socket、对局种子）
//   ② 持有引擎 / 帧循环 / 画布，串起 onEnter→开局、onExit→收尾
//   ③ 只有它碰网络：主机广播状态、客户端上报输入
//   ④ 对局结束弹个结果框，然后回大厅
//
// 规则一行都不在这里 —— 全部在 bp::MultiGameEngine 里（阶段二已搬走）
// ============================================================
class MultiGameScreen : public ScreenBase
{
	Q_OBJECT

public:
	explicit MultiGameScreen(QWidget* parent = nullptr);
	~MultiGameScreen();

	QString screenId() const override { return "MultiGame"; }
	QString displayTitle() const override { return "联机对战"; }
	//常驻：初始化时统一创建；每次进入由 onEnter 重新开局
	bool persistent() const override { return true; }

	void onEnter(const QVariantMap& args) override;
	void onExit() override;
	//Esc：通知对手并回大厅
	bool handleBack() override;

	//键盘输入：由 AppHost 统一分发（Qt 的按键只送有焦点的子控件）
	void handleKeyPress(QKeyEvent* event) override;
	void handleKeyRelease(QKeyEvent* event) override;

	//当前引擎（调试/测试用）
	bp::MultiGameEngine* engine() const { return m_engine.get(); }

private slots:
	void readNetworkData();
	//对局结束（由引擎回调触发）：停循环，推迟到本轮事件处理结束后再弹框
	void onMatchFinished(bool victory);
	//弹结果框 + 返回大厅
	void showMatchResult();

private:
	//每推进若干逻辑步做一次网络收发
	void onEngineStepped(int advancedSteps);
	//引擎状态 -> 网络快照（主机）
	GameStateSnapshot makeNetworkSnapshot() const;
	//网络快照 -> 引擎（客户端）
	void applyNetworkSnapshot(const GameStateSnapshot& snap);

	void broadcastGameState();
	void sendInputToHost();
	void sendGameOver(int winnerId);
	//把按键状态交给引擎（主机）或留着等上报（客户端）
	void syncLocalInput();

	GameCanvas* m_canvas = nullptr;
	GameHud* m_hud = nullptr;

	std::unique_ptr<bp::MultiGameEngine> m_engine;
	std::unique_ptr<bp::GameLoop> m_loop;

	bp::InputState m_input;
	//本局是否已经收尾（避免重复弹框、重复返回）
	bool m_finished = false;
	//距上次广播/上报已经推进了几步（控制网络频率）
	int m_netStepCounter = 0;

	//网络（socket 由 NetSession 持有，这里只是引用）
	bool isHost = false;
	QUdpSocket* socket = nullptr;
	QMetaObject::Connection readyReadConnection;
	QHostAddress hostAddress;
	quint16 hostPort = 0;
	QMap<int, ClientEndpoint> clientEndpoints;
	int myPlayerId = 0;
};
