#pragma once
#include "levelSelectWindow.h"
#include <QWidget>

class GameWindow :public QWidget
{
	Q_OBJECT

private:
	int level = 0;

public:
	GameWindow(int level, QWidget* parent = nullptr);
	void setLevel(int level);
};
