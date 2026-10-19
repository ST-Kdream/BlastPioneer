#pragma once
#include "ScaffoldScreen.h"

class QCheckBox;
class QSlider;
class QComboBox;
class QPushButton;
class QLabel;
class SideTabWidget;

//设置界面：左侧分类 + 右侧内容
//（用 ui/SideTabWidget 而不是 QTabWidget::West —— 后者会把标签文字竖排、页多了还出现上下箭头、
//  滚轮悬停时会直接切页；具体毛病写在 SideTabWidget.h 里）
//
// 内容分三类：外观 / 游戏 / 声音。「模组」不在这里：它是独立页面（主页 → 模组）。
class SettingsScreen : public ScaffoldScreen
{
	Q_OBJECT

public:
	explicit SettingsScreen(QWidget* parent = nullptr);

	QString screenId() const override { return "Settings"; }
	QString displayTitle() const override { return "设置"; }
	bool persistent() const override { return true; }

	void onEnter(const QVariantMap& args) override;

private:
	void setupUI();
	//把已保存的设置载入控件（会屏蔽信号，避免"载入"被当成"用户改动"）
	void loadSettings();
	//把控件当前值写入设置
	void saveSettings();

	SideTabWidget* m_tabs = nullptr;

	//外观
	QComboBox* m_themeCombo = nullptr;
	QComboBox* m_baseStyleCombo = nullptr;
	QLabel* m_baseStyleHint = nullptr;
	QCheckBox* m_gridCheck = nullptr;
	QCheckBox* m_pageFadeCheck = nullptr;

	//游戏
	QSlider* m_frameRateSlider = nullptr;
	QLabel* m_frameRateLabel = nullptr;
	QComboBox* m_effectQualityCombo = nullptr;
	QCheckBox* m_confirmExitCheck = nullptr;

	//声音
	QSlider* m_volumeSlider = nullptr;
	QLabel* m_volumeLabel = nullptr;
	//??:???(????)+ ?????? + "????"??
	class QLineEdit* m_updateSourcesEdit = nullptr;
	QCheckBox* m_keepBackupCheck = nullptr;
	QLabel* m_updateHint = nullptr;

private slots:
	//配色：改一下立刻生效（便宜 —— 只换调色板与样式表）
	void onThemeChanged();
	//基座风格：保存下来、下次启动生效（当场切换要重新 polish 所有控件，明显卡顿）
	void onBaseStyleChanged();
	void onOkClicked();
	void onCancelClicked();
};
