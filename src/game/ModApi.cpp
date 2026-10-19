#include "ModApi.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QStringList>

namespace bp
{

// ---------- mod.json ----------

ModManifest ModManifest::fromJson(const QJsonObject& obj, QString* error)
{
	ModManifest manifest;

	auto readString = [&obj](const char* key)
	{
		return obj.value(QLatin1String(key)).toString();
	};

	manifest.id = readString("id");
	manifest.name = readString("name");
	manifest.version = readString("version");
	manifest.author = readString("author");
	manifest.description = readString("description");
	manifest.priority = obj.value(QStringLiteral("priority")).toInt(0);
	manifest.enabledByDefault = obj.value(QStringLiteral("enabledByDefault")).toBool(true);
	manifest.apiVersion = readString("apiVersion");
	manifest.native = obj.value(QStringLiteral("native")).toBool(false);
	manifest.nativeLib = readString("nativeLib");

	const QJsonValue features = obj.value(QStringLiteral("features"));
	if (features.isArray())
	{
		for (const QJsonValue& value : features.toArray())
		{
			const QString name = value.toString().trimmed();
			if (!name.isEmpty() && !manifest.features.contains(name))
			{
				manifest.features.append(name);
			}
		}
	}

	if (manifest.name.isEmpty()) { manifest.name = manifest.id; }

	if (error) { error->clear(); }
	return manifest;
}

// ---------- 兼容性声明 ----------

ModApiDeclaration ModApiDeclaration::builtin()
{
	ModApiDeclaration declaration;

	declaration.engineApiVersion = kModApiVersion;
	//加法式演进的实物证据：老 mod 声明 0.1.0 / 0.1.1 也照样加载，所以名单里三个版本都在
	declaration.supportedModApiVersions.append(QStringLiteral("0.1.0"));
	declaration.supportedModApiVersions.append(QStringLiteral("0.1.1"));
	declaration.supportedModApiVersions.append(kModApiVersion);

	//开放的能力：三种纯数据覆盖 + 界面外观 + 原生模组（C ABI）。
	//hooks 从 0.1.0 起开放 —— 这是**加法式**改动（原来是拒绝，现在接受），
	//所以 apiVersion 不用动，老 mod 一个都不用改。
	declaration.features.insert(ModFeature::Gameplay, true);
	declaration.features.insert(ModFeature::Items, true);
	declaration.features.insert(ModFeature::Levels, true);
	declaration.features.insert(ModFeature::Ui, true);
	declaration.features.insert(ModFeature::Hooks, true);
	//modes（模组注册的"全新模式"）走了另一条路：它不是一个 mod.json 能力位，
	//而是运行时 register_mode + 三个钩子，由 has_feature("mode.register") 判定。
	//所以这里保持 false —— 这个位留给"要把整个模式引擎都接管"的那一类（OWN_ENGINE，未实现）。
	declaration.features.insert(ModFeature::Modes, false);

	declaration.history.append(qMakePair(QStringLiteral("0.1.0"),
		QStringLiteral("初版：gameplay / items / levels 三类纯数据覆盖，ui（界面令牌覆盖 + 追加样式表），"
					   "hooks（原生模组 C ABI：事件订阅 / 只读查询 / 暂停 / 触发已有道具效果）；"
					   "modes（全新模式）尚未开放")));
	declaration.history.append(qMakePair(QStringLiteral("0.1.1"),
		QStringLiteral("纯加法式：C ABI 末尾追加 bombs_snapshot / level_index / phase / step_index / "
					   "player_count 与 tile_at 的爆炸格；事件补 ENEMY_DAMAGED、ITEM_USED，"
					   "掉落与关卡事件带上道具名与关卡号。声明 0.1.0 的老 mod 继续可用")));
	declaration.history.append(qMakePair(kModApiVersion,
		QStringLiteral("L3：把 ABI 里预留的槽位填成真函数（register_mode + on_generate_map / "
					   "on_enemy_think / on_check_win / on_mode_start / on_mode_stop），末尾追加 "
					   "set_tile / set_player_start；写请求（生成/移除/伤害实体、改数值、时间倍率）"
					   "全部真实生效。于是模组可以在「开始游戏 → 选择模式」里注册自己的模式，"
					   "并接管地图、敌人 AI 与胜负。声明 0.1.0 / 0.1.1 的老 mod 继续可用")));

	return declaration;
}

ModApiDeclaration ModApiDeclaration::fromJson(const QJsonObject& obj, QString* error)
{
	ModApiDeclaration declaration = builtin();

	if (error) { error->clear(); }

	const QJsonValue engineVersion = obj.value(QStringLiteral("engineApiVersion"));
	if (engineVersion.isString()) { declaration.engineApiVersion = engineVersion.toString(); }

	const QJsonValue versions = obj.value(QStringLiteral("supportedModApiVersions"));
	if (versions.isArray())
	{
		declaration.supportedModApiVersions.clear();
		for (const QJsonValue& value : versions.toArray())
		{
			const QString version = value.toString().trimmed();
			if (!version.isEmpty()) { declaration.supportedModApiVersions.append(version); }
		}
	}

	const QJsonValue features = obj.value(QStringLiteral("features"));
	if (features.isObject())
	{
		declaration.features.clear();
		const QJsonObject featureObject = features.toObject();
		for (auto it = featureObject.constBegin(); it != featureObject.constEnd(); ++it)
		{
			declaration.features.insert(it.key(), it.value().toBool(false));
		}
	}

	const QJsonValue history = obj.value(QStringLiteral("history"));
	if (history.isArray())
	{
		declaration.history.clear();
		for (const QJsonValue& value : history.toArray())
		{
			const QJsonObject entry = value.toObject();
			declaration.history.append(qMakePair(
				entry.value(QStringLiteral("apiVersion")).toString(),
				entry.value(QStringLiteral("note")).toString()));
		}
	}

	//名单为空等于"什么 mod 都不认"，那一定是文件写坏了：
	//保留内置的兜底名单，并明确报一条问题（宁可加载、也不要因为声明文件写错就全禁）
	if (declaration.supportedModApiVersions.isEmpty())
	{
		declaration.supportedModApiVersions = builtin().supportedModApiVersions;
		if (error) { *error = QStringLiteral("supportedModApiVersions 为空，已回退到内置名单"); }
	}

	return declaration;
}

bool ModApiDeclaration::supportsVersion(const QString& apiVersion) const
{
	return supportedModApiVersions.contains(apiVersion);
}

bool ModApiDeclaration::supportsFeature(const QString& feature) const
{
	return features.value(feature, false);
}

QString ModApiDeclaration::supportedVersionsText() const
{
	return supportedModApiVersions.isEmpty() ? QStringLiteral("（无）")
											 : supportedModApiVersions.join(QStringLiteral("、"));
}

//把 mod 的声明与"本构建支持什么"比一遍
//返回空串 = 通过；否则返回一句能直接显示给人看的原因
QString ModApiDeclaration::check(const ModManifest& manifest) const
{
	if (manifest.apiVersion.isEmpty())
	{
		return QStringLiteral("缺少 apiVersion（不知道这份 mod 是按哪一版接口写的）");
	}

	if (!supportsVersion(manifest.apiVersion))
	{
		return QStringLiteral("本版本不支持 mod 接口 v%1（本版本支持：%2）")
			.arg(manifest.apiVersion)
			.arg(supportedVersionsText());
	}

	//mod 声明的每一项能力都必须可用
	for (const QString& feature : manifest.features)
	{
		if (supportsFeature(feature)) { continue; }

		//"未知能力名"和"已知但未开放"给不同提示，方便作者分辨是自己写错了还是引擎还没做
		const bool known = allModFeatures().contains(feature);
		return known
			? QStringLiteral("需要「%1」能力，本版本尚未开放").arg(feature)
			: QStringLiteral("声明了未知能力「%1」").arg(feature);
	}

	return QString();
}

} // namespace bp
