#include "SettingsScreen.h"
#include "SettingsManager.h"
#include "DirectoryLayout.h"
#include "ThemeManager.h"
#include "SideTabWidget.h"
#include <QCheckBox>
#include <QSlider>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QProcess>
#include <QFileInfo>
#include <QLineEdit>
#include <QRegularExpression>
#include <QSignalBlocker>

SettingsScreen::SettingsScreen(QWidget* parent) : ScaffoldScreen(parent)
{
	setupUI();
	loadSettings();
}

void SettingsScreen::setupUI()
{
	setPageTitle(QStringLiteral("设置"), QStringLiteral("自定义个性化设置"));

	m_tabs = new SideTabWidget(this);

	// ---------------- 外观 ----------------
	QWidget* appearance = new QWidget(this);
	QVBoxLayout* appearanceLayout = new QVBoxLayout(appearance);
	appearanceLayout->setContentsMargins(20, 20, 20, 20);
	appearanceLayout->setSpacing(10);

	m_themeCombo = new QComboBox(appearance);
	for (const ThemeManager::ThemeInfo& info : ThemeManager::themes())
	{
		m_themeCombo->addItem(info.name, info.id);
		m_themeCombo->setItemData(m_themeCombo->count() - 1, info.description, Qt::ToolTipRole);
	}

	m_baseStyleCombo = new QComboBox(appearance);
	for (const QString& key : ThemeManager::baseStyles())
	{
		m_baseStyleCombo->addItem(key == QStringLiteral("system") ? QStringLiteral("跟随系统") : key, key);
	}

	m_baseStyleHint = new QLabel(appearance);
	m_baseStyleHint->setObjectName("fieldHint");
	m_baseStyleHint->setWordWrap(true);
	//这条提示是有用的信息（不是开发细节）：告诉玩家为什么这里改了不当场变
	m_baseStyleHint->setText(QStringLiteral("这一项的改动会在重启程序后生效。"));
	m_baseStyleHint->setToolTip(QStringLiteral(
		"切换界面风格需要重建所有控件的外观，当场切换会有明显卡顿，所以改为下次启动生效。"));

	m_gridCheck = new QCheckBox(QStringLiteral("游戏时显示网格"), appearance);
	m_pageFadeCheck = new QCheckBox(QStringLiteral("页面切换淡入"), appearance);

	appearanceLayout->addWidget(new QLabel(QStringLiteral("配色主题"), appearance));
	appearanceLayout->addWidget(m_themeCombo);
	appearanceLayout->addSpacing(6);
	appearanceLayout->addWidget(new QLabel(QStringLiteral("界面风格"), appearance));
	appearanceLayout->addWidget(m_baseStyleCombo);
	appearanceLayout->addWidget(m_baseStyleHint);
	appearanceLayout->addSpacing(6);
	appearanceLayout->addWidget(m_gridCheck);
	appearanceLayout->addWidget(m_pageFadeCheck);
	appearanceLayout->addStretch(1);

	// ---------------- 游戏 ----------------
	QWidget* game = new QWidget(this);
	QVBoxLayout* gameLayout = new QVBoxLayout(game);
	gameLayout->setContentsMargins(20, 20, 20, 20);
	gameLayout->setSpacing(10);

	m_frameRateSlider = new QSlider(Qt::Horizontal, game);
	m_frameRateSlider->setRange(30, 120);
	m_frameRateSlider->setTickInterval(30);
	m_frameRateSlider->setTickPosition(QSlider::TicksBelow);
	m_frameRateLabel = new QLabel(game);

	m_effectQualityCombo = new QComboBox(game);
	m_effectQualityCombo->addItem(QStringLiteral("低"));
	m_effectQualityCombo->addItem(QStringLiteral("中"));
	m_effectQualityCombo->addItem(QStringLiteral("高"));

	m_confirmExitCheck = new QCheckBox(QStringLiteral("退出前先确认"), game);

	// 原生模组开关：模组页那句"需要在设置里打开「允许原生模组」"指的就是这一项。
	// 默认关（DLL 会完全接管进程，崩溃一起带走），改开关要重启才生效。
	m_allowNativeCheck = new QCheckBox(QStringLiteral("允许原生模组（DLL，与游戏同进程）"), game);
	m_allowNativeCheck->setObjectName(QStringLiteral("allowNativeCheck"));
	m_allowNativeCheck->setToolTip(QStringLiteral(
		"打开后才会加载 mods/ 里的原生模组（.dll）。它会完全接管游戏进程，崩溃会带走整个游戏，"
		"只启用你信任的来源。改这一项需要重启游戏才生效。"));

	QLabel* nativeHint = new QLabel(
		QStringLiteral("原生模组默认关闭；打开后需重启游戏生效。模组页里标着「[原生]」的那些就是它。"),
		game);
	nativeHint->setObjectName("fieldHint");
	nativeHint->setWordWrap(true);

	gameLayout->addWidget(new QLabel(QStringLiteral("画面帧率"), game));
	gameLayout->addWidget(m_frameRateSlider);
	gameLayout->addWidget(m_frameRateLabel);
	gameLayout->addSpacing(6);
	gameLayout->addWidget(new QLabel(QStringLiteral("特效质量"), game));
	gameLayout->addWidget(m_effectQualityCombo);
	gameLayout->addWidget(m_confirmExitCheck);
	gameLayout->addSpacing(6);
	gameLayout->addWidget(m_allowNativeCheck);
	gameLayout->addWidget(nativeHint);
	gameLayout->addStretch(1);

	// ---------------- 声音 ----------------
	QWidget* sound = new QWidget(this);
	QVBoxLayout* soundLayout = new QVBoxLayout(sound);
	soundLayout->setContentsMargins(20, 20, 20, 20);
	soundLayout->setSpacing(10);

	m_volumeSlider = new QSlider(Qt::Horizontal, sound);
	m_volumeSlider->setRange(0, 100);
	m_volumeSlider->setTickInterval(10);
	m_volumeSlider->setTickPosition(QSlider::TicksBelow);
	m_volumeLabel = new QLabel(sound);

	QLabel* soundNote = new QLabel(QStringLiteral("音效与音乐尚未加入，音量会先保存下来，加入后自动生效。"), sound);
	soundNote->setObjectName("fieldHint");
	soundNote->setWordWrap(true);

	soundLayout->addWidget(new QLabel(QStringLiteral("音量"), sound));
	soundLayout->addWidget(m_volumeSlider);
	soundLayout->addWidget(m_volumeLabel);
	soundLayout->addWidget(soundNote);
	soundLayout->addStretch(1);

	// ---------------- 更新 ----------------
	// 这里只做两件事：改"从哪些地址找更新"和"要不要保留备份"。
	// 真正的下载/校验/回滚全在 Updater.exe 里 —— 主程序不碰更新逻辑，
	// 这样更新器崩了也不会影响游戏本体，而且更新器可以单独发给别人用。
	QWidget* updatePage = new QWidget(this);
	QVBoxLayout* updateLayout = new QVBoxLayout(updatePage);
	updateLayout->setContentsMargins(20, 20, 20, 20);
	updateLayout->setSpacing(10);

	m_updateSourcesEdit = new QLineEdit(updatePage);
	m_updateSourcesEdit->setObjectName(QStringLiteral("updateSources"));
	m_updateSourcesEdit->setPlaceholderText(
		QStringLiteral("留空 = 用内置的源"));
	m_updateSourcesEdit->setToolTip(
		QStringLiteral("多个地址用英文逗号分隔，越靠前越先试。比如：https://my.mirror/blastpioneer"));

	m_keepBackupCheck = new QCheckBox(QStringLiteral("更新后保留旧文件的备份"), updatePage);
	m_keepBackupCheck->setObjectName(QStringLiteral("keepBackupCheck"));
	m_useMirrorCheck = new QCheckBox(QStringLiteral("是否启用镜像源"), updatePage);
	m_useMirrorCheck->setObjectName(QStringLiteral("useMirrorCheck"));
	m_prereleaseCheck = new QCheckBox(QStringLiteral("是否追踪预发布版本（如alpha、beta、rc）"), updatePage);
	m_prereleaseCheck->setObjectName(QStringLiteral("prereleaseCheck"));

	QPushButton* checkUpdateBtn = new QPushButton(QStringLiteral("检查更新"), updatePage);
	checkUpdateBtn->setObjectName(QStringLiteral("primaryBtn"));
	checkUpdateBtn->setCursor(Qt::PointingHandCursor);

	m_updateHint = new QLabel(updatePage);
	m_updateHint->setObjectName("fieldHint");
	m_updateHint->setWordWrap(true);
	m_updateHint->setText(QStringLiteral(
		"更新器会和游戏放在同一个目录（Updater.exe）。检查/下载/替换都在那里完成，"
		"失败会自动把文件还原。这里改的设置它下次运行就会读到。"));

	updateLayout->addWidget(new QLabel(QStringLiteral("更新源（从哪找新版本）"), updatePage));
	updateLayout->addWidget(m_updateSourcesEdit);
	updateLayout->addWidget(m_keepBackupCheck);
	updateLayout->addWidget(m_useMirrorCheck);
	updateLayout->addWidget(m_prereleaseCheck);
	updateLayout->addSpacing(8);
	updateLayout->addWidget(checkUpdateBtn);
	updateLayout->addWidget(m_updateHint);
	updateLayout->addStretch(1);

	//启动更新器：路径就是游戏目录下的 Updater.exe。
	//用 --window 让玩家看得见进度（静默失败最难解释）；找不到就如实说明，不假装"已是最新"。
	//关键：先把**当前界面上**的更新设置写进 settings.ini 再启动 —— 更新器读的是文件，
	//如果等玩家先点"确定"，就会出现"改了源却不生效、必须点确定再点检查"的怪行为。
	connect(checkUpdateBtn, &QPushButton::clicked, this, [this]()
	{
		saveUpdateSettingsFromUi();

		const QString updater = DirectoryLayout::instance().root() + QStringLiteral("/Updater.exe");
		if (!QFileInfo::exists(updater))
		{
			QMessageBox::information(this, QStringLiteral("没有更新器"),
				QStringLiteral("这个目录里没有 Updater.exe。\n"
							   "如果你是源码运行，它会在 build\\Release\\ 里，跟着发布包一起分发。"));
			return;
		}
		if (!QProcess::startDetached(updater, { QStringLiteral("--window"), QStringLiteral("--check") }))
		{
			QMessageBox::warning(this, QStringLiteral("启动失败"),
				QStringLiteral("没能启动更新器：%1").arg(updater));
		}
	});

	m_tabs->addPage(appearance, QStringLiteral("外观"));
	m_tabs->addPage(game, QStringLiteral("游戏"));
	m_tabs->addPage(sound, QStringLiteral("声音"));
	m_tabs->addPage(updatePage, QStringLiteral("更新"));

	contentLayout()->addWidget(m_tabs, 1);

	// ---------------- 底部按钮 ----------------
	// 只有「确定」和「返回」：「取消」和「返回」原来是两个作用相同的按钮（都回上一页）。
	// 现在「返回」负责问"改动的要不要保存"，「确定」= 保存并返回。
	QPushButton* okBtn = addFooterButton(QStringLiteral("确定"), QStringLiteral("primaryBtn"));
	connect(okBtn, &QPushButton::clicked, this, &SettingsScreen::onOkClicked);

	addFooterStretch();

	QPushButton* backBtn = addFooterButton(QStringLiteral("返回"), QStringLiteral("accentBtn"));
	connect(backBtn, &QPushButton::clicked, this, &SettingsScreen::onBackRequested);

	connect(m_themeCombo, &QComboBox::currentIndexChanged, this, &SettingsScreen::onThemeChanged);
	connect(m_baseStyleCombo, &QComboBox::currentIndexChanged, this, &SettingsScreen::onBaseStyleChanged);

	connect(m_frameRateSlider, &QSlider::valueChanged, this,
			[this](int value) { m_frameRateLabel->setText(QStringLiteral("%1 帧/秒").arg(value)); });
	connect(m_volumeSlider, &QSlider::valueChanged, this,
			[this](int value) { m_volumeLabel->setText(QStringLiteral("%1%").arg(value)); });
}

void SettingsScreen::onEnter(const QVariantMap& args)
{
	Q_UNUSED(args);
	loadSettings();
}

void SettingsScreen::loadSettings()
{
	SettingsManager& settings = SettingsManager::instance();

	//屏蔽信号：这里是"把已保存的值填进控件"，不该被当成用户改动
	const QSignalBlocker blockTheme(m_themeCombo);
	const QSignalBlocker blockStyle(m_baseStyleCombo);
	const QSignalBlocker blockGrid(m_gridCheck);
	const QSignalBlocker blockFade(m_pageFadeCheck);

	m_confirmExitCheck->setChecked(settings.confirmOnExit());
	if (m_allowNativeCheck) { m_allowNativeCheck->setChecked(settings.allowNativeMods()); }
	m_gridCheck->setChecked(settings.showGrid());
	m_pageFadeCheck->setChecked(settings.pageFade());
	m_effectQualityCombo->setCurrentIndex(qBound(0, settings.effectQuality(), 2));
	m_volumeSlider->setValue(settings.volume());
	if (m_updateSourcesEdit) { m_updateSourcesEdit->setText(settings.updateSources().join(QStringLiteral(", "))); }
	if (m_keepBackupCheck) { m_keepBackupCheck->setChecked(settings.updateKeepBackup()); }
	if (m_useMirrorCheck) { m_useMirrorCheck->setChecked(settings.updateUseMirror()); }
	if (m_prereleaseCheck) { m_prereleaseCheck->setChecked(settings.updateTrackPrerelease()); }
	m_frameRateSlider->setValue(settings.frameRate());

	m_frameRateLabel->setText(QStringLiteral("%1 帧/秒").arg(m_frameRateSlider->value()));
	m_volumeLabel->setText(QStringLiteral("%1%").arg(m_volumeSlider->value()));

	const QString savedStyle = settings.uiBaseStyle().isEmpty()
		? ThemeManager::defaultBaseStyle() : settings.uiBaseStyle();
	const int styleIndex = m_baseStyleCombo->findData(savedStyle);
	m_baseStyleCombo->setCurrentIndex(styleIndex >= 0 ? styleIndex : 0);

	const QString savedTheme = settings.uiTheme().isEmpty()
		? ThemeManager::defaultThemeId() : settings.uiTheme();
	const int themeIndex = m_themeCombo->findData(savedTheme);
	m_themeCombo->setCurrentIndex(themeIndex >= 0 ? themeIndex : 0);
}

void SettingsScreen::saveSettings()
{
	SettingsManager& settings = SettingsManager::instance();

	settings.setConfirmOnExit(m_confirmExitCheck->isChecked());
	if (m_allowNativeCheck) { settings.setAllowNativeMods(m_allowNativeCheck->isChecked()); }
	settings.setShowGrid(m_gridCheck->isChecked());
	settings.setPageFade(m_pageFadeCheck->isChecked());
	settings.setEffectQuality(m_effectQualityCombo->currentIndex());
	settings.setVolume(m_volumeSlider->value());
	settings.setFrameRate(m_frameRateSlider->value());
	settings.setUiTheme(m_themeCombo->currentData().toString());
	settings.setUiBaseStyle(m_baseStyleCombo->currentData().toString());
	if (m_updateSourcesEdit) { settings.setUpdateSources(m_updateSourcesEdit->text().split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts)); }
	if (m_keepBackupCheck) { settings.setUpdateKeepBackup(m_keepBackupCheck->isChecked()); }
	if (m_useMirrorCheck) { settings.setUpdateUseMirror(m_useMirrorCheck->isChecked()); }
	if (m_prereleaseCheck) { settings.setUpdateTrackPrerelease(m_prereleaseCheck->isChecked()); }
}

//配色：立刻生效并保存（这类改动必须当场看见才判断得出好不好看）
void SettingsScreen::onThemeChanged()
{
	const QString theme = m_themeCombo->currentData().toString();

	SettingsManager& settings = SettingsManager::instance();
	settings.setUiTheme(theme);
	ThemeManager::instance().applyThemeInstant(theme);

	//网格与淡入也是"当场看得见"的，一起存
	settings.setShowGrid(m_gridCheck->isChecked());
	settings.setPageFade(m_pageFadeCheck->isChecked());
}

//基座风格：只保存，下次启动生效（提示语已经把这件事告诉玩家了）
void SettingsScreen::onBaseStyleChanged()
{
	SettingsManager::instance().setUiBaseStyle(m_baseStyleCombo->currentData().toString());
}

//只把「更新」这一页的控件写进 settings.ini。
//为什么单独一个函数：点「检查更新」之前必须让它生效（更新器读的是文件），
//但又不想顺手把别的页里还没确认的改动存下去。
void SettingsScreen::saveUpdateSettingsFromUi()
{
	SettingsManager& settings = SettingsManager::instance();
	if (m_updateSourcesEdit)
	{
		settings.setUpdateSources(m_updateSourcesEdit->text().split(
			QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts));
	}
	if (m_keepBackupCheck) { settings.setUpdateKeepBackup(m_keepBackupCheck->isChecked()); }
	if (m_useMirrorCheck) { settings.setUpdateUseMirror(m_useMirrorCheck->isChecked()); }
	if (m_prereleaseCheck) { settings.setUpdateTrackPrerelease(m_prereleaseCheck->isChecked()); }
}

//当前界面上的值与本机已保存的值是否不同（"返回时要问要不要保存"靠它判断）
bool SettingsScreen::hasUnsavedChanges() const
{
	SettingsManager& settings = SettingsManager::instance();

	if (m_confirmExitCheck->isChecked() != settings.confirmOnExit()) { return true; }
	if (m_allowNativeCheck && m_allowNativeCheck->isChecked() != settings.allowNativeMods()) { return true; }
	if (m_gridCheck->isChecked() != settings.showGrid()) { return true; }
	if (m_pageFadeCheck->isChecked() != settings.pageFade()) { return true; }
	if (m_effectQualityCombo->currentIndex() != settings.effectQuality()) { return true; }
	if (m_volumeSlider->value() != settings.volume()) { return true; }
	if (m_frameRateSlider->value() != settings.frameRate()) { return true; }

	if (m_updateSourcesEdit)
	{
		const QStringList ui = m_updateSourcesEdit->text().split(
			QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts);
		if (ui != settings.updateSources()) { return true; }
	}
	if (m_keepBackupCheck && m_keepBackupCheck->isChecked() != settings.updateKeepBackup()) { return true; }
	if (m_useMirrorCheck && m_useMirrorCheck->isChecked() != settings.updateUseMirror()) { return true; }
	if (m_prereleaseCheck && m_prereleaseCheck->isChecked() != settings.updateTrackPrerelease()) { return true; }

	return false;
}

//「返回」按钮与 Esc：有改动就先问一句，避免"以为保存了其实没有"
void SettingsScreen::onBackRequested()
{
	if (!hasUnsavedChanges())
	{
		emit requestBack();
		return;
	}

	QMessageBox box(this);
	box.setWindowTitle(QStringLiteral("还没保存"));
	box.setText(QStringLiteral("设置改过了，要保存吗？"));
	QPushButton* saveButton = box.addButton(QStringLiteral("保存并返回"), QMessageBox::AcceptRole);
	QPushButton* discardButton = box.addButton(QStringLiteral("不保存"), QMessageBox::DestructiveRole);
	QPushButton* stayButton = box.addButton(QStringLiteral("继续编辑"), QMessageBox::RejectRole);
	box.setDefaultButton(saveButton);
	box.exec();

	if (box.clickedButton() == saveButton)
	{
		saveSettings();
		emit requestBack();
	}
	else if (box.clickedButton() == discardButton)
	{
		//配色是即时生效的：放弃时要把主题恢复成已保存的那一份，否则"不保存"看起来没生效
		loadSettings();
		SettingsManager::instance().setUiTheme(m_themeCombo->currentData().toString());
		ThemeManager::instance().applyThemeInstant(m_themeCombo->currentData().toString());
		emit requestBack();
	}
	else if (box.clickedButton() == stayButton)
	{
		return;
	}
}

//Esc 也走同一条路（否则 Esc 会绕过"要不要保存"）
bool SettingsScreen::handleBack()
{
	onBackRequested();
	return true;
}

void SettingsScreen::onOkClicked()
{
	saveSettings();
	emit requestBack();
}
