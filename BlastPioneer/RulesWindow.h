#pragma once
#include "MainWindow.h"
#include "SettingsManager.h"
#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QTextEdit>
#include <QMessageBox>
#include <QSettings>
#include <string>
#include <sstream>
#include <fstream>

class RulesWindow :public QWidget
{
	Q_OBJECT

private:
	MainWindow* mainWin;
	QPushButton* backBtn;
	QTextEdit* Qrules;
	void setupUI();
	void getRules();
	void closeEvent(QCloseEvent* event) override;

public:
	explicit RulesWindow(MainWindow* mainWin, QWidget* parent = nullptr);
	~RulesWindow();

private slots:
	void goBack();
};
