#include "FileManager.h"
#include "DirectoryLayout.h"
#include <QFile>
#include <QSaveFile>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QMutexLocker>

//单例访问
FileManager& FileManager::instance()
{
	static FileManager s_instance;
	return s_instance;
}

//是否存在
bool FileManager::exists(const QString& absPath) const
{
	if (absPath.isEmpty()) { return false; }
	return QFile::exists(absPath);
}

//读取原始字节：磁盘优先，其次 Qt 资源
QByteArray FileManager::read(const QString& absPath, const QString& qrcFallback)
{
	//磁盘
	if (!absPath.isEmpty() && QFile::exists(absPath))
	{
		QFile file(absPath);
		if (file.open(QIODevice::ReadOnly))
		{
			QByteArray data = file.readAll();
			file.close();
			return data;
		}
	}

	//Qt 资源（内置引导数据）
	if (!qrcFallback.isEmpty())
	{
		QFile file(qrcFallback);
		if (file.open(QIODevice::ReadOnly))
		{
			QByteArray data = file.readAll();
			file.close();
			return data;
		}
	}

	return QByteArray();
}

//读取 JSON 对象
QJsonObject FileManager::readJsonObject(const QString& absPath, const QString& qrcFallback, bool* ok)
{
	if (ok) { *ok = false; }
	QByteArray data = read(absPath, qrcFallback);
	if (data.isEmpty()) { return QJsonObject(); }

	QJsonParseError error;
	QJsonDocument doc = QJsonDocument::fromJson(data, &error);
	if (error.error != QJsonParseError::NoError || !doc.isObject())
	{
		log("file", QString("JSON 对象解析失败: %1 (%2)").arg(absPath, error.errorString()));
		return QJsonObject();
	}

	if (ok) { *ok = true; }
	return doc.object();
}

//读取 JSON 数组
QJsonArray FileManager::readJsonArray(const QString& absPath, const QString& qrcFallback, bool* ok)
{
	if (ok) { *ok = false; }
	QByteArray data = read(absPath, qrcFallback);
	if (data.isEmpty()) { return QJsonArray(); }

	QJsonParseError error;
	QJsonDocument doc = QJsonDocument::fromJson(data, &error);
	if (error.error != QJsonParseError::NoError || !doc.isArray())
	{
		log("file", QString("JSON 数组解析失败: %1 (%2)").arg(absPath, error.errorString()));
		return QJsonArray();
	}

	if (ok) { *ok = true; }
	return doc.array();
}

//读取文本
QString FileManager::readText(const QString& absPath, const QString& qrcFallback)
{
	QByteArray data = read(absPath, qrcFallback);
	if (data.isEmpty()) { return QString(); }
	return QString::fromUtf8(data);
}

//原子写入 JSON
bool FileManager::writeJson(const QString& absPath, const QJsonDocument& doc)
{
	QSaveFile file(absPath);
	if (!file.open(QIODevice::WriteOnly))
	{
		log("file", QString("无法写入: %1 (%2)").arg(absPath, file.errorString()));
		return false;
	}

	file.write(doc.toJson(QJsonDocument::Indented));
	if (!file.commit())
	{
		log("file", QString("提交写入失败: %1 (%2)").arg(absPath, file.errorString()));
		return false;
	}
	return true;
}

//原子写入文本
bool FileManager::writeText(const QString& absPath, const QString& text)
{
	QSaveFile file(absPath);
	if (!file.open(QIODevice::WriteOnly))
	{
		log("file", QString("无法写入: %1 (%2)").arg(absPath, file.errorString()));
		return false;
	}

	file.write(text.toUtf8());
	if (!file.commit())
	{
		log("file", QString("提交写入失败: %1 (%2)").arg(absPath, file.errorString()));
		return false;
	}
	return true;
}

//追加日志（线程安全）
void FileManager::log(const QString& tag, const QString& message) const
{
	QMutexLocker locker(&m_logMutex);

	QString path = DirectoryLayout::instance().gameLogPath();
	QDir().mkpath(QFileInfo(path).absolutePath());

	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
	{
		return;
	}

	QString line = QString("[%1][%2] %3\n")
		.arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz"), tag, message);
	file.write(line.toUtf8());
	file.close();
}
