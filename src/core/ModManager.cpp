#include "ModManager.h"
#include "DirectoryLayout.h"
#include "FileManager.h"
#include "LaunchGuard.h"
#include "SettingsManager.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonValue>
#include <algorithm>

//单例访问
ModManager& ModManager::instance()
{
	static ModManager s_instance;
	return s_instance;
}

// ---------- 加载 ----------

bool ModManager::load()
{
	if (m_loaded) { return !m_entries.isEmpty(); }

	m_entries.clear();
	m_itemPatches.clear();
	m_levelPatches.clear();
	m_gameplayPatches.clear();
	m_uiOverrides.clear();
	m_problems.clear();

	//顺序很重要：先知道"本构建支持什么"，再去比对每个 mod
	loadDeclaration();
	scanMods();

	m_loaded = true;

	FileManager::instance().log("mod", QString("模组加载完成：发现 %1 个，生效 %2 个，问题 %3 条")
		.arg(m_entries.size())
		.arg(activeMods().size())
		.arg(m_problems.size()));
	return !m_entries.isEmpty();
}

//重载：玩家在设置里改了开关、或往 mods 里丢了新模组之后，不必重启程序
void ModManager::reload()
{
	m_loaded = false;
	load();
}

//兼容性声明：磁盘优先，缺失/写坏时用内置兜底
void ModManager::loadDeclaration()
{
	m_declaration = bp::ModApiDeclaration::builtin();
	m_declarationProblem.clear();

	const QString path = DirectoryLayout::instance().dataDir() + "/mod_api.json";
	if (!FileManager::instance().exists(path))
	{
		//没有声明文件也能跑：内置兜底与仓库里那份保持一致
		FileManager::instance().log("mod", QString("未找到 mod_api.json，使用内置兼容性声明（接口 v%1）")
			.arg(bp::kModApiVersion));
		return;
	}

	bool ok = false;
	const QJsonObject obj = FileManager::instance().readJsonObject(path, QString(), &ok);
	if (!ok)
	{
		m_declarationProblem = QStringLiteral("mod_api.json 解析失败，已回退到内置声明");
		FileManager::instance().log("mod", m_declarationProblem);
		return;
	}

	QString error;
	m_declaration = bp::ModApiDeclaration::fromJson(obj, &error);
	m_declarationProblem = error;
	if (!error.isEmpty())
	{
		FileManager::instance().log("mod", QStringLiteral("mod_api.json: %1").arg(error));
	}
}

//作者建议的默认值 vs 玩家的开关
bool ModManager::resolveEnabled(const bp::ModManifest& manifest) const
{
	SettingsManager& settings = SettingsManager::instance();

	//玩家没动过开关：按作者建议（mod.json 的 enabledByDefault）
	if (!settings.hasModsRecord()) { return manifest.enabledByDefault; }

	//玩家动过：以设置里的列表为准
	return settings.enabledMods().contains(manifest.id);
}

//读一份可选配置；返回是否读成功（文件不存在不算失败）
static bool readOptionalJson(const QString& path, QJsonObject& out, QStringList* warnings,
							 const QString& source)
{
	if (!QFileInfo::exists(path)) { return false; }

	bool ok = false;
	out = FileManager::instance().readJsonObject(path, QString(), &ok);
	if (!ok)
	{
		if (warnings) { warnings->append(QStringLiteral("%1 解析失败，已忽略").arg(source)); }
		return false;
	}
	return true;
}

//扫描 mods/*/mod.json
void ModManager::scanMods()
{
	const QString modsDir = DirectoryLayout::instance().modsDir();
	QDir dir(modsDir);

	if (!dir.exists())
	{
		//没有 mods 目录是最常见的情况，不是错误
		FileManager::instance().log("mod", QString("未找到 mods 目录（这是正常的）: %1").arg(modsDir));
		return;
	}

	//按目录名排序，保证结果稳定（与文件系统枚举顺序无关）
	const QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

	for (const QString& name : entries)
	{
		const QString modDir = dir.absoluteFilePath(name);
		const QString manifestPath = modDir + "/mod.json";

		if (!QFileInfo::exists(manifestPath)) { continue; }   //不是模组，跳过（不记问题）

		ModEntry entry;
		entry.directory = modDir;
		entry.manifest.id = name;

		bool ok = false;
		const QJsonObject obj = FileManager::instance().readJsonObject(manifestPath, QString(), &ok);
		if (!ok)
		{
			entry.manifest.name = name;
			entry.blockedReason = QStringLiteral("mod.json 解析失败");
			m_entries.append(entry);
			m_problems.append(QStringLiteral("%1: mod.json 解析失败").arg(name));
			continue;
		}

		QString error;
		entry.manifest = bp::ModManifest::fromJson(obj, &error);
		//目录名兜底：作者忘了写 id 时用目录名，这样开关与日志仍然可读
		if (entry.manifest.id.isEmpty()) { entry.manifest.id = name; }
		if (entry.manifest.name.isEmpty()) { entry.manifest.name = entry.manifest.id; }

		entry.enabled = resolveEnabled(entry.manifest);

		//1) 先比对本构建的兼容性声明：不支持就**不加载**（不是"加载了但行为诡异"）
		const QString blocked = m_declaration.check(entry.manifest);
		if (!blocked.isEmpty())
		{
			entry.blocked = true;
			entry.blockedReason = blocked;
			m_entries.append(entry);
			m_problems.append(QStringLiteral("%1: %2").arg(entry.manifest.id, blocked));
			continue;
		}

		//1b) 安全模式：原生模组**整个**跳过，连它的数据覆盖也不并入。
		//为什么不留"数据生效、逻辑不生效"的半个模组：那样界面会显示"已启用"，
		//而实际行为与作者的预期不一致 —— 排障时最容易被这种东西误导（契约 §3.4 同一个理由）
		if (entry.manifest.native && LaunchGuard::instance().blockNativeMods())
		{
			entry.blocked = true;
			entry.blockedReason = QStringLiteral("因崩溃保护被跳过（安全模式：本次不加载原生模组）");
			m_entries.append(entry);
			m_problems.append(QStringLiteral("%1: %2").arg(entry.manifest.id, entry.blockedReason));
			continue;
		}

		//2) 读三份可选配置（读到什么算什么）
		const QString gameplayPath = modDir + "/config/gameplay.json";
		const QString itemsPath = modDir + "/config/items.json";
		const QString levelsPath = modDir + "/config/levels.json";

		if (readOptionalJson(gameplayPath, entry.config.gameplay, &entry.warnings,
							 QStringLiteral("%1: config/gameplay.json").arg(entry.manifest.id)))
		{
			entry.config.hasGameplay = true;
			entry.files.append(QStringLiteral("config/gameplay.json"));
			entry.featuresUsed.append(bp::ModFeature::Gameplay);
		}
		if (readOptionalJson(itemsPath, entry.config.items, &entry.warnings,
							 QStringLiteral("%1: config/items.json").arg(entry.manifest.id)))
		{
			entry.config.hasItems = true;
			entry.files.append(QStringLiteral("config/items.json"));
			entry.featuresUsed.append(bp::ModFeature::Items);
		}
		if (readOptionalJson(levelsPath, entry.config.levels, &entry.warnings,
							 QStringLiteral("%1: config/levels.json").arg(entry.manifest.id)))
		{
			entry.config.hasLevels = true;
			entry.files.append(QStringLiteral("config/levels.json"));
			entry.featuresUsed.append(bp::ModFeature::Levels);
		}

		//界面外观：令牌覆盖（config/ui.json）
		const QString uiTokensPath = modDir + "/config/ui.json";
		if (readOptionalJson(uiTokensPath, entry.ui.tokens, &entry.warnings,
							 QStringLiteral("%1: config/ui.json").arg(entry.manifest.id)))
		{
			entry.ui.hasTokens = true;
			entry.ui.modId = entry.manifest.id;
			entry.files.append(QStringLiteral("config/ui.json"));
			entry.featuresUsed.append(bp::ModFeature::Ui);
		}

		//界面外观：追加样式表（ui/override.qss）—— 原样读文本，不做解析
		const QString uiQssPath = modDir + "/ui/override.qss";
		if (QFileInfo::exists(uiQssPath))
		{
			const QString text = FileManager::instance().readText(uiQssPath);
			if (!text.isEmpty())
			{
				entry.ui.hasQss = true;
				entry.ui.modId = entry.manifest.id;
				entry.ui.qssPath = uiQssPath;
				entry.ui.qss = text;
				entry.files.append(QStringLiteral("ui/override.qss"));
				if (!entry.featuresUsed.contains(bp::ModFeature::Ui))
				{
					entry.featuresUsed.append(bp::ModFeature::Ui);
				}
			}
		}

		//能力校验再做一遍，这次把"按文件推断出来的能力"也算上 ——
		//否则作者不写 features 就能绕过校验
		QStringList effectiveFeatures = entry.manifest.features;
		for (const QString& feature : entry.featuresUsed)
		{
			if (!effectiveFeatures.contains(feature)) { effectiveFeatures.append(feature); }
		}
		entry.manifest.features = effectiveFeatures;

		const QString inferredBlocked = m_declaration.check(entry.manifest);
		if (!inferredBlocked.isEmpty())
		{
			entry.blocked = true;
			entry.blockedReason = inferredBlocked;
			m_entries.append(entry);
			m_problems.append(QStringLiteral("%1: %2").arg(entry.manifest.id, inferredBlocked));
			continue;
		}

		if (!entry.enabled)
		{
			entry.blockedReason = QStringLiteral("已禁用（在设置界面的模组页里打开）");
			m_entries.append(entry);
			continue;
		}

		entry.active = true;
		m_entries.append(entry);
	}

	//priority 小的先应用、大的后应用（大的覆盖小的）；同 priority 按 id 排
	std::stable_sort(m_entries.begin(), m_entries.end(),
		[](const ModEntry& a, const ModEntry& b)
		{
			if (a.manifest.priority != b.manifest.priority)
			{
				return a.manifest.priority < b.manifest.priority;
			}
			return a.manifest.id < b.manifest.id;
		});

	//按排好的顺序把生效模组的覆盖并进总表
	for (ModEntry& entry : m_entries)
	{
		if (entry.blocked) { continue; }   //被声明拒绝的：内容都没读，没什么可校验的

		//启用 → 并进总表；禁用 → 只校验与统计（看得见，但不影响游戏）
		applyEntry(entry, entry.active);

		for (const QString& warning : entry.warnings) { m_problems.append(warning); }
	}
}

//校验某个 mod 的三份覆盖；merge=true 时才真的并进总表
//这是唯一"真正改变游戏行为"的地方，所以所有校验都在这里做，并且只记问题、不改结构
void ModManager::applyEntry(ModEntry& entry, bool merge)
{
	const QString id = entry.manifest.id;

	// ---------- 玩法数值 ----------
	if (entry.config.hasGameplay)
	{
		const QJsonObject obj = entry.config.gameplay;
		for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
		{
			const QString key = it.key();
			if (key.startsWith(QLatin1Char('_'))) { continue; }   //"_comment" 之类的注释键

			if (!bp::isGameplayPatchKey(key))
			{
				entry.warnings.append(QStringLiteral("%1: 未知的玩法键「%2」，已忽略").arg(id, key));
				continue;
			}

			const QJsonValue value = it.value();
			double number = 0.0;
			if (value.isBool())             { number = value.toBool() ? 1.0 : 0.0; }
			else if (value.isDouble())      { number = value.toDouble(); }
			else
			{
				//字符串/数组/对象：这一条覆盖不了，但要让人知道
				entry.warnings.append(QStringLiteral("%1: 玩法键「%2」不是数值，已忽略").arg(id, key));
				continue;
			}

			if (merge)
			{
				m_gameplayPatches.append(bp::GameplayPatch(key, number,
					QStringLiteral("%1/config/gameplay.json").arg(id)));
			}
			++entry.patchesApplied;
		}
	}

	// ---------- 道具：按道具名覆盖字段 ----------
	if (entry.config.hasItems)
	{
		const QJsonObject obj = entry.config.items;
		for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
		{
			if (it.key().startsWith(QLatin1Char('_'))) { continue; }

			if (!it.value().isObject())
			{
				entry.warnings.append(QStringLiteral("%1: items.json 的「%2」不是对象，已忽略").arg(id, it.key()));
				continue;
			}

			if (merge)
			{
				QJsonObject merged = m_itemPatches.value(it.key());
				const QJsonObject patch = it.value().toObject();
				for (auto field = patch.constBegin(); field != patch.constEnd(); ++field)
				{
					merged.insert(field.key(), field.value());
				}
				m_itemPatches.insert(it.key(), merged);
			}
			++entry.patchesApplied;
		}
	}

	// ---------- 关卡：按关卡号覆盖 / 新增 ----------
	if (entry.config.hasLevels)
	{
		const QJsonObject obj = entry.config.levels;
		for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
		{
			if (it.key().startsWith(QLatin1Char('_'))) { continue; }

			bool ok = false;
			const int levelId = it.key().toInt(&ok);
			if (!ok || levelId <= 0)
			{
				entry.warnings.append(QStringLiteral("%1: levels.json 的键「%2」不是关卡号，已忽略")
					.arg(id, it.key()));
				continue;
			}

			if (!it.value().isObject())
			{
				entry.warnings.append(QStringLiteral("%1: levels.json 的关卡 %2 不是对象，已忽略")
					.arg(id).arg(levelId));
				continue;
			}

			if (merge)
			{
				QJsonObject merged = m_levelPatches.value(levelId);
				const QJsonObject patch = it.value().toObject();
				for (auto field = patch.constBegin(); field != patch.constEnd(); ++field)
				{
					merged.insert(field.key(), field.value());
				}
				m_levelPatches.insert(levelId, merged);
			}
			++entry.patchesApplied;
		}
	}

	// ---------- 界面外观：令牌覆盖 + 追加样式表 ----------
	//这里只登记，真正的应用在 ThemeManager 里（它才知道主题与令牌的合并顺序）
	if (entry.ui.hasTokens || entry.ui.hasQss)
	{
		if (merge) { m_uiOverrides.append(entry.ui); }
		++entry.patchesApplied;
	}
}

// ---------- 叠加结果 ----------

//默认配置 + 所有生效 mod 的玩法覆盖
//每次调用都重新算一遍：这样"在设置里打开/关闭一个 mod"不需要重启程序
bp::GameConfig ModManager::gameConfig() const
{
	bp::GameConfig config;
	bp::applyGameplayPatches(config, m_gameplayPatches, nullptr);
	return config;
}

QList<bp::ModManifest> ModManager::activeMods() const
{
	QList<bp::ModManifest> list;
	for (const ModEntry& entry : m_entries)
	{
		if (entry.active) { list.append(entry.manifest); }
	}
	return list;
}

// ---------- 玩家开关 ----------

void ModManager::setEnabled(const QString& modId, bool enabled)
{
	SettingsManager::instance().setModEnabled(modId, enabled);
}

QStringList ModManager::enabledIds() const
{
	return SettingsManager::instance().enabledMods();
}
