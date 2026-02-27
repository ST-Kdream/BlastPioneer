#pragma once
#include <QObject>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include "PlayerInfo.h"
#include "Item.h"

class DataLoader :public QObject
{
	Q_OBJECT

public:
	explicit DataLoader(QObject* parent = nullptr);

public slots:
	void load();

signals:
	void finished(const PlayerInfo& playerInfo, const QList<Item> items);
};
