#pragma once
#include "RulesWindow.h"
#include "settings.h"
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>

class MainWindow :public QWidget
{
	Q_OBJECT

private:
	//5个按钮和一个标题
	QLabel* mainTitle;
	QPushButton* btn1;
	QPushButton* btn2;
	QPushButton* btn3;
	QPushButton* btn4;
	QPushButton* btn5;
	void setupUI();

public:
	explicit MainWindow(QWidget* parent = nullptr);
	void btnConnect();

private slots:
	//窗口跳转函数
	void GoPlayerWindow();
	void GoSingleGame();
	void GoInternetGame();
	void GoRulesWindow();
	void GoSettingsWindow();
};
