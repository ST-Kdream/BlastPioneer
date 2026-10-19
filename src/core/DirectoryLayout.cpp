#include "DirectoryLayout.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

//单例访问（函数内静态，首次调用时构造，程序结束时自动析构）
DirectoryLayout& DirectoryLayout::instance()
{
	static DirectoryLayout s_instance;
	return s_instance;
}

//解析根目录
void DirectoryLayout::resolve(const QString& rootOverride)
{
	QString root;

	//优先级1：命令行 --root=<目录>
	if (!rootOverride.isEmpty())
	{
		root = rootOverride;
	}

	//优先级2：环境变量 BP_ROOT（开发时指向源码树，避免反复复制 Assets/Data）
	if (root.isEmpty())
	{
		root = QString::fromLocal8Bit(qgetenv("BP_ROOT")).trimmed();
	}

	//优先级3：exe 所在目录（与旧版本行为一致）
	if (root.isEmpty())
	{
		root = QCoreApplication::applicationDirPath();
	}

	//统一成不带结尾斜杠的绝对路径
	m_root = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
}

//按需创建目录（多级）
bool DirectoryLayout::ensureDir(const QString& dir) const
{
	if (dir.isEmpty()) { return false; }
	QDir d(dir);
	if (d.exists()) { return true; }
	return d.mkpath(".");
}

//确保所有运行时目录存在
void DirectoryLayout::ensureRuntimeDirs() const
{
	ensureDir(dataDir());
	ensureDir(assetsDir());
	ensureDir(modsDir());
	ensureDir(cacheDir());
	ensureDir(logsDir());
}
