#pragma once
#include "SettingsManager.h"
#include <QDialog>
#include <QCheckBox>
#include <QSlider>
#include <QComboBox>
#include <QPushButton>

class SettingsWindow :public QDialog
{
	Q_OBJECT

private:
	//控件
	QCheckBox* StartupEffectCheck;
	QSlider* frameRateSlider;
	QSlider* volumeSlider;
	QComboBox* efffectQualityCombo;
	QPushButton* okBtn;
	QPushButton* cancelBtn;

	//函数
	void setupUI();
	void loadSettings();
	void saveSettings();
	void closeEvent(QCloseEvent* event) override;

public:
	explicit SettingsWindow(QWidget* parent = nullptr);

private slots:
	void change();
	void cancel();
};