#pragma once
#include <MainWindow.h>
#include <playerInfo.h>
#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QShowEvent>

class PlayerWindow :public MainWindow
{
private:
	MainWindow* mainWin;
	QPushButton* backBtn;
	QPushButton* bagBtn;
public:
	PlayerWindow(MainWindow* mainWin, QWidget* parent = nullptr);
	void setupUI();
	void showEvent(QShowEvent* event) override;
};
