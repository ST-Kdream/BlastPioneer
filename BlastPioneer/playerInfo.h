#pragma once
#include <QString>
#include <QMap>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonDocument>
#include <QMessageBox>

//玩家信息类，处理所有与玩家信息相关的逻辑，主要依赖于json
class PlayerInfo
{
private:
	QString userName;
	QString rank;
	int EP;
	int passedLevels;
	int coins;
	QMap<QString, int> inventory;
	QDateTime createDate;
	QDateTime lastLogin;

public:
	PlayerInfo();
	bool loadFromJson(const QJsonObject& json);
	QJsonObject toJson() const;
	void setDefaults();     //设定默认值

	//获取玩家信息的函数接口
	QString getUserName() const { return userName; }
	QString getRank() const { return rank; }
	int getCoins() const { return coins; }
	int getEP() const { return EP; }
	int getPassedLevels() const { return passedLevels; }
	QDateTime getCreateDate() const { return createDate; }
	QDateTime getLastLogin() const { return lastLogin; }
	QMap<QString, int> getInventory() const { return inventory; }

	//设置玩家信息的函数
	void setUserName(const QString& name) { userName = name; }
	void setRank(const QString& rk) { rank = rk; }    //段位系统类返回玩家段位
	void setCoins(int gold) { coins = gold; }
	void setEP(int ep) { EP = ep; }
	void setPassedLevels(int passedlev) { passedLevels = passedlev; }
	void setCreateDate() { createDate = QDateTime::currentDateTime(); }
	void setLastLogin() { lastLogin = QDateTime::currentDateTime(); }

	//玩家背包相关函数接口
	void addItem(const QString& itemName, int count = 1);
	void removeItem(const QString& itemName, int count = 1);
};
