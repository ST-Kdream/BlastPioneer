#pragma once
#include "DataLoader.h"
#include "SettingsManager.h"
#include <QWidget>
#include <QTimer>
#include <QThread>
#include <QQuickWidget>
#include <QQmlContext>

class StartupEffectWindow :public QQuickWidget
{
	Q_OBJECT

private:
	QTimer* minTimer;
	QTimer* maxTimer;
	QThread* loaderThread;
	DataLoader* dataLoader;
	bool isDataLoaded;
	bool isMinTimePassed;

public:
	explicit StartupEffectWindow(QWidget* parent = nullptr);
	~StartupEffectWindow();
	void start();

signals:
	void dataLoaded(const PlayerInfo& playerInfo, const QList<Item>& items);
	void finished();

private slots:
	void minTimeout();
	void maxTimeout();
	void onDataLoaded(const PlayerInfo& info, const QList<Item>& items);
	void closeWindow();
};
