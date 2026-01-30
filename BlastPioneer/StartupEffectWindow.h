#pragma once
#include <QWidget>
#include <QTimer>

class StartupEffectWindow :public QWidget
{
	Q_OBJECT
private:
	QTimer* timer;

public:
	StartupEffectWindow(QWidget* parent = nullptr);
	void showup(int displayTime = 3000);

signals:
	void finished();

private slots:
	void onTimeout();
};
