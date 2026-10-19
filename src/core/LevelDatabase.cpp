#include "LevelDatabase.h"
#include "DirectoryLayout.h"
#include "FileManager.h"
#include "ModManager.h"

#include <QDir>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>

//单例访问
LevelDatabase& LevelDatabase::instance()
{
	static LevelDatabase s_instance;
	return s_instance;
}

//从文件名里猜关卡号：level_07.json / 7.json / level-7.json 都认
//为什么要猜：关卡号写在 JSON 里当然是首选，但玩家手写文件时经常忘，
//这时用文件名兜底比"整关被丢掉"友好得多
static int levelIdFromFileName(const QString& fileName)
{
	static const QRegularExpression digits(QStringLiteral("(\\d+)"));
	const QRegularExpressionMatch match = digits.match(fileName);
	if (!match.hasMatch()) { return 0; }
	return match.captured(1).toInt();
}

//加载（幂等）
//
// 三个阶段，顺序不能换：
//   1) 读磁盘上的关卡文件（原始 JSON）
//   2) 把 L1 模组的覆盖**按字段**并进那些 JSON（只写差异；磁盘上没有的关卡号 = 模组新增一关）
//   3) 再解析成 LevelDefinition 并校验
// 为什么第 2 步必须发生在解析之前：模组的 levels.json 只写它要改的字段，
// 如果先解析磁盘、再用补丁重新解析，没被覆盖的字段就丢了 —— 那就成了"整关替换"，
// 与"mod 只写差异、引擎升级后自动跟上"的设计相反。
bool LevelDatabase::load()
{
	if (m_loaded) { return !m_levels.isEmpty(); }

	m_problems.clear();
	m_levels.clear();

	// ---------- 1) 读磁盘 ----------
	struct RawLevel
	{
		QString     file;
		int         id = 0;
		QJsonObject obj;
	};
	QList<RawLevel> raws;

	const QString dir = DirectoryLayout::instance().levelsDir();
	QDir levelsDir(dir);

	if (!levelsDir.exists())
	{
		//不是错误：旧版本没有这个目录，退回程序化关卡即可
		FileManager::instance().log("level", QString("未找到关卡目录，将使用程序化随机关卡: %1").arg(dir));
	}
	else
	{
		//按文件名排序，保证"第 1 关"永远是第 1 关（与文件系统枚举顺序无关）
		const QStringList files = levelsDir.entryList(QStringList() << QStringLiteral("*.json"),
													  QDir::Files, QDir::Name);

		for (const QString& name : files)
		{
			const QString absolute = levelsDir.absoluteFilePath(name);

			bool ok = false;
			const QJsonObject obj = FileManager::instance().readJsonObject(absolute, QString(), &ok);
			if (!ok)
			{
				//文件坏了：记一条问题、跳过它，绝不影响其它关卡
				m_problems.append(QStringLiteral("%1: JSON 解析失败").arg(name));
				FileManager::instance().log("level", QStringLiteral("关卡文件解析失败，已跳过: %1").arg(name));
				continue;
			}

			//关卡号：JSON 里写了就用它，没写用文件名里的数字
			int id = obj.value(QStringLiteral("id")).toInt(0);
			if (id <= 0) { id = levelIdFromFileName(name); }
			if (id <= 0)
			{
				m_problems.append(QStringLiteral("%1: 缺少关卡号（id 或文件名里的数字）").arg(name));
				continue;
			}

			RawLevel raw;
			raw.file = name;
			raw.id = id;
			raw.obj = obj;
			raws.append(raw);
		}
	}

	// ---------- 2) 并进模组覆盖（字段级） ----------
	const QHash<int, QJsonObject>& patches = ModManager::instance().levelPatches();
	int patched = 0;
	int added = 0;

	for (auto it = patches.constBegin(); it != patches.constEnd(); ++it)
	{
		const int id = it.key();
		const QJsonObject patch = it.value();

		bool merged = false;
		for (RawLevel& raw : raws)
		{
			if (raw.id != id) { continue; }

			for (auto field = patch.constBegin(); field != patch.constEnd(); ++field)
			{
				raw.obj.insert(field.key(), field.value());   //同名键：模组的值覆盖数据里的值
			}
			merged = true;
			++patched;
			break;
		}

		if (!merged)
		{
			//磁盘上没有这一关：整关由模组提供（新增关卡）
			QJsonObject obj = patch;
			obj.insert(QStringLiteral("id"), id);
			RawLevel raw;
			raw.file = QStringLiteral("(模组新增)");
			raw.id = id;
			raw.obj = obj;
			raws.append(raw);
			++added;
		}
	}

	if (raws.isEmpty())
	{
		m_loaded = true;
		return false;
	}

	// ---------- 3) 解析与校验 ----------
	for (const RawLevel& raw : raws)
	{
		QString error;
		bp::LevelDefinition def = bp::LevelDefinition::fromJson(raw.obj, &error);

		def.id = raw.id;
		if (def.name.isEmpty()) { def.name = QStringLiteral("关卡 %1").arg(def.id); }

		if (!error.isEmpty())
		{
			//有结构性错误（例如手写地图行列不对）：仍然收下这一关，
			//地图生成器会自己退回随机生成 —— "能玩"永远优先于"精确"
			m_problems.append(QStringLiteral("%1: %2（将退回随机地图）").arg(raw.file, error));
			FileManager::instance().log("level", QStringLiteral("关卡 %1 地图有问题: %2").arg(def.id).arg(error));
		}

		m_levels.append(def);
	}

	//按关卡号升序（文件枚举顺序不完全等于关卡顺序）
	std::sort(m_levels.begin(), m_levels.end(),
			  [](const bp::LevelDefinition& a, const bp::LevelDefinition& b) { return a.id < b.id; });

	//同号关卡只保留第一个（mod 覆盖与手写文件同时存在时常见），后面的记一条问题
	QList<bp::LevelDefinition> unique;
	for (const bp::LevelDefinition& def : m_levels)
	{
		bool duplicate = false;
		for (const bp::LevelDefinition& kept : unique)
		{
			if (kept.id == def.id) { duplicate = true; break; }
		}
		if (duplicate)
		{
			m_problems.append(QStringLiteral("关卡号 %1 重复，已忽略后一个").arg(def.id));
			continue;
		}
		unique.append(def);
	}
	m_levels = unique;

	m_loaded = true;

	FileManager::instance().log("level", QStringLiteral("关卡数据加载完成：%1 关（模组覆盖 %2 关、新增 %3 关，问题 %4 条）")
		.arg(m_levels.size()).arg(patched).arg(added).arg(m_problems.size()));
	return !m_levels.isEmpty();
}

//强制重新加载（设置界面开关 mod 之后用）
bool LevelDatabase::reload()
{
	m_loaded = false;
	return load();
}

//按关卡号查找
const bp::LevelDefinition* LevelDatabase::find(int level) const
{
	for (const bp::LevelDefinition& def : m_levels)
	{
		if (def.id == level) { return &def; }
	}
	return nullptr;
}

//关卡名
QString LevelDatabase::nameOf(int level) const
{
	const bp::LevelDefinition* def = find(level);
	return def ? def->name : QString();
}
