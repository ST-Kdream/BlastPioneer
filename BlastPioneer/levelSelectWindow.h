#pragma once
#include "MainWindow.h";
#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QGridLayout>
#include <QMessageBox>
#include <QShowEvent>

class levelSelectWindow :public QWidget
{
	Q_OBJECT

private:
	MainWindow* mainWin;
	QLabel* title;
	QPushButton* backBtn;
	QGridLayout* levelsBtn[6];

	void setupUI();
	void setuplevelBtn();
	void showEvent(QShowEvent* event) override;

public:
	explicit levelSelectWindow(MainWindow* mainWin,QWidget* parent=nullptr);

private slots:
	void goBack();
	void goGameWindow();
};
