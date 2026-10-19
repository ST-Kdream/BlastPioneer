#pragma once
#include "ScreenBase.h"
#include "GameLoop.h"
#include "SingleGameEngine.h"

#include <memory>

class GameCanvas;
class GameHud;
class BagScreen;
class QKeyEvent;

// ============================================================
// 单人关卡界面（阶段三之后：薄壳）
//
// 这里只做四件事：
//   ① 持有引擎 / 帧循环 / 画布，串起 onEnter→开局、onExit→收尾
//   ② 把按键转成引擎的 InputState（键盘由 AppHost 统一分发）
//   ③ 背包覆盖层（游戏内按 B 打开）
//   ④ 通关/失败的结算与提示
//
// 规则一概不在这里 —— 全部在 bp::SingleGameEngine 里（阶段三已搬走）
// ============================================================
class SingleGameScreen : public ScreenBase
{
	Q_OBJECT

public:
	explicit SingleGameScreen(QWidget* parent = nullptr);
	~SingleGameScreen();

	QString screenId() const override { return "SingleGame"; }
	QString displayTitle() const override;
	bool persistent() const override { return true; }

	void onEnter(const QVariantMap& args) override;
	void onExit() override;
	//Esc：返回关卡选择
	bool handleBack() override;

	//键盘（由 AppHost 分发，不依赖控件焦点）
	void handleKeyPress(QKeyEvent* event) override;
	void handleKeyRelease(QKeyEvent* event) override;

	//背包覆盖层要用的引擎指针（道具效果直接作用在引擎上）
	bp::SingleGameEngine* engine() const { return m_engine.get(); }

	//当前关卡号
	int level() const { return m_level; }

	//当前是不是"模组模式"（不是关卡）。模组模式没有关卡概念、不结算关卡奖励
	bool isModMode() const { return !m_modeKey.isEmpty(); }
	const QString& modModeKey() const { return m_modeKey; }

private slots:
	//引擎每步推进后刷新 HUD
	void onEngineStepped();

private:
	void startLevel(int level);
	//开始一个模组模式（"modId/modeId"）；失败时给玩家一句人话并退回上一屏
	bool startModMode(const QString& key);
	void openBag();
	void closeBag();
	//通关/失败结算（由引擎回调触发）
	void onLevelFinished(bool victory);

	GameCanvas* m_canvas = nullptr;
	GameHud* m_hud = nullptr;
	BagScreen* m_bag = nullptr;

	std::unique_ptr<bp::SingleGameEngine> m_engine;
	std::unique_ptr<bp::GameLoop> m_loop;

	bp::InputState m_input;
	int m_level = 1;
	//模组模式：全局键 "modId/modeId"（空 = 普通关卡）
	QString m_modeKey;
	QString m_modeName;
	//防止同一局重复结算（胜利/失败只处理一次）
	bool m_settled = false;
};
