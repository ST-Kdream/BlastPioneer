#pragma once
#include "ScreenBase.h"

class QLabel;
class QPushButton;

//注册/欢迎界面：首次启动（没有 Data/playerData.json）时显示
//本阶段先做占位：说明 + 「返回」+「开始新手存档」，完整注册流程留到后续阶段
class WelcomeScreen : public ScreenBase
{
	Q_OBJECT

public:
	explicit WelcomeScreen(QWidget* parent = nullptr);

	QString screenId() const override { return "Welcome"; }
	QString displayTitle() const override { return "欢迎"; }
	bool persistent() const override { return true; }
	bool canGoBack() const override { return false; }   //启动分支里的第一屏，没有上一屏

	void onEnter(const QVariantMap& args) override;

private slots:
	//开始新手存档：写入默认数据并进入主页
	void startNewProfile();

private:
	void setupUI();

	QLabel* m_title = nullptr;
	QLabel* m_tip = nullptr;
	QPushButton* m_startBtn = nullptr;
	QPushButton* m_quitBtn = nullptr;
};
