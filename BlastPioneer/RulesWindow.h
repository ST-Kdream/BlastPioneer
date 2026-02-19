#pragma once
#include "MainWindow.h"
#include "settings.h"
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

public:
	explicit RulesWindow(MainWindow* mainWin, QWidget* parent = nullptr);
	~RulesWindow();

private slots:
	void goBack();
};
