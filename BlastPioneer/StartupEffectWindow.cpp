#include "StartupEffectWindow.h"

//构造函数
StartupEffectWindow::StartupEffectWindow(QWidget* parent) :QWidget(parent), timer(nullptr)
{
	setWindowFlags(Qt::FramelessWindowHint);
	setFixedSize(800, 600);

	timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &StartupEffectWindow::onTimeout);
}

//展示
void StartupEffectWindow::showup(int displayTime)
{
	show();
	timer->start(displayTime);
}

//超时处理函数
void StartupEffectWindow::onTimeout()
{
	timer->stop();
	emit finished();
	close();
}