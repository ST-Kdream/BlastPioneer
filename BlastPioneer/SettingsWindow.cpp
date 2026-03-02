#include "SettingsWindow.h"

//构造函数
SettingsWindow::SettingsWindow(QWidget* parent) :QDialog(parent)
{
	setupUI();
	loadSettings();
	connect(okBtn, &QPushButton::clicked, this, &SettingsWindow::change);
	connect(cancelBtn, &QPushButton::clicked, this, &SettingsWindow::cancel);
}

//UI设置
void SettingsWindow::setupUI()
{
	setWindowTitle("设置");

	QByteArray savedGeo = SettingsManager::instance()->loadWindowGeometry();
	if (!savedGeo.isEmpty())
	{
		restoreGeometry(savedGeo);
	}
	else
	{
		resize(800, 600);
		move(100, 100);
	}

	//创建控件
	startupEffectCheck = new QCheckBox("显示启动特效并预加载");

	frameRateSlider = new QSlider(Qt::Horizontal);
	frameRateSlider->setRange(30, 120);
	frameRateSlider->setTickInterval(30);
	frameRateSlider->setTickPosition(QSlider::TicksBelow);

	volumeSlider = new QSlider(Qt::Horizontal);
	volumeSlider->setRange(0, 100);
	volumeSlider->setTickInterval(10);
	volumeSlider->setTickPosition(QSlider::TicksBelow);

	effectQualityCombo = new QComboBox();
	effectQualityCombo->addItem("低");
	effectQualityCombo->addItem("中");
	effectQualityCombo->addItem("高");

	okBtn = new QPushButton("确定");
	cancelBtn = new QPushButton("取消");

	//布局
	QVBoxLayout* mainLayout = new QVBoxLayout();
	mainLayout->addWidget(startupEffectCheck);

	mainLayout->addWidget(new QLabel("特效质量："));
	mainLayout->addWidget(effectQualityCombo);

	mainLayout->addWidget(new QLabel("音量："));
	mainLayout->addWidget(volumeSlider);

	mainLayout->addWidget(new QLabel("帧率："));
	mainLayout->addWidget(frameRateSlider);

	QHBoxLayout* btnLine = new QHBoxLayout();
	btnLine->addWidget(okBtn);
	btnLine->addWidget(cancelBtn);
	mainLayout->addLayout(btnLine);
	setLayout(mainLayout);
}

//载入设置
void SettingsWindow::loadSettings()
{
	startupEffectCheck->setChecked(SettingsManager::instance()->showStartupEffect());
	effectQualityCombo->setCurrentIndex(SettingsManager::instance()->effectQuality());
	volumeSlider->setValue(SettingsManager::instance()->volume());
	frameRateSlider->setValue(SettingsManager::instance()->frameRate());
}

//保存设置
void SettingsWindow::saveSettings()
{
	SettingsManager::instance()->setShowStartupEffect(startupEffectCheck->isChecked());
	SettingsManager::instance()->setEffectQuality(effectQualityCombo->currentIndex());
	SettingsManager::instance()->setVolume(volumeSlider->value());
	SettingsManager::instance()->setFrameRate(frameRateSlider->value());
}

//保存按钮
void SettingsWindow::change()
{
	saveSettings();
	accept();
}

//取消按钮
void SettingsWindow::cancel()
{
	reject();
}

//关闭窗口
void SettingsWindow::closeEvent(QCloseEvent* event)
{
	SettingsManager::instance()->saveWindowGeometry(saveGeometry());
	QWidget::closeEvent(event);
}