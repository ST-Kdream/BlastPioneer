#pragma once
#include "SettingsManager.h"
#include <QDialog>
#include <QCheckBox>
#include <QSlider>
#include <QComboBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>
#include <QCloseEvent>

class SettingsWindow :public QDialog
{
	Q_OBJECT

private:
	//控件
	QCheckBox* startupEffectCheck;
	QCheckBox* gridCheck;
	QSlider* frameRateSlider;
	QSlider* volumeSlider;
	QComboBox* effectQualityCombo;
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