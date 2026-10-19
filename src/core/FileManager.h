#pragma once
#include <QString>
#include <QByteArray>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>

//文件管理单例：全项目唯一的磁盘读写入口
//所有接口都接收绝对路径（由 DirectoryLayout 提供），避免各处硬拼路径
//写入一律使用 QSaveFile 原子落盘，防止写一半崩溃产生半截文件
class FileManager
{
private:
	FileManager() = default;

	mutable QMutex m_logMutex;   //日志写入可能来自工作线程，需要加锁

public:
	//单例访问
	static FileManager& instance();

	//是否存在
	bool exists(const QString& absPath) const;

	//读取：磁盘优先，找不到时回落到 Qt 资源路径（qrcFallback，如 ":/Data/itemInfo.json"）
	QByteArray  read(const QString& absPath, const QString& qrcFallback = QString());
	QJsonObject readJsonObject(const QString& absPath, const QString& qrcFallback = QString(), bool* ok = nullptr);
	QJsonArray  readJsonArray(const QString& absPath, const QString& qrcFallback = QString(), bool* ok = nullptr);
	QString     readText(const QString& absPath, const QString& qrcFallback = QString());

	//写入（QSaveFile 原子落盘）
	bool writeJson(const QString& absPath, const QJsonDocument& doc);
	bool writeText(const QString& absPath, const QString& text);

	//追加一行日志到 logs/game.log（带时间戳，线程安全）
	void log(const QString& tag, const QString& message) const;
};
