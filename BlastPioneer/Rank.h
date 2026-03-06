#pragma once
#include <QString>
#include <QMap>

class Rank
{
private:
	QMap<int, QString> rankData;

public:
	Rank();
	QString updateRankName(const int EP) const;
};
