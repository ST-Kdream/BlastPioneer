#pragma once
#include "ModApi.h"
#include "GameConfig.h"
#include "GameplayPatch.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

// ============================================================
// 模组管理器（L1：纯数据 mod）
//
// 目录约定（都在 exe 旁边，也就是 x64\<Config>\ 下）：
//   Data/mod_api.json                 本构建的"mod 兼容性声明"（接受哪些版本/能力）
//   mods/<id>/mod.json                模组的自我声明
//   mods/<id>/config/gameplay.json    玩法数值覆盖（只写差异）
//   mods/<id>/config/items.json       道具字段覆盖（键 = 道具名）
//   mods/<id>/config/levels.json      关卡覆盖/新增（键 = 关卡号）
//
// 四条底线（对应"坏 mod 不能让游戏打不开"）：
//   1. 任何一步失败只影响这一个 mod：跳过它、记原因，其它照常，游戏本体照常启动；
//   2. 声明不支持就不加载，而不是"加载了但行为诡异"；
//   3. 未知的键/字段不静默：记一条警告，作者在设置界面的模组页能看见；
//   4. 玩家的启用开关写在 settings.ini 里，**不写回 mod.json**（那是作者的文件）。
//
// 叠加顺序：priority 小的先应用、大的后应用（于是大的覆盖小的）；
// 同 priority 按 id 排，保证结果可复现。内容只写差异 ——
// 引擎升级后，mod 没覆盖到的部分自动跟上新版本。
// ============================================================
class ModManager
{
public:
	//一个模组自己带的三份覆盖（读盘后的原始 JSON）
	struct ModConfig
	{
		bool        hasGameplay = false;
		bool        hasItems = false;
		bool        hasLevels = false;
		QJsonObject gameplay;
		QJsonObject items;
		QJsonObject levels;
	};

	//界面外观覆盖（美化体系的 L0/L1 入口，见 docs/界面体系与美化接口.md 第 4 节）
	//  ① config/ui.json    —— 覆盖界面令牌（换主色/圆角/间距，不用碰布局）
	//  ② ui/override.qss   —— 在主题之后追加样式表（只写要改的控件）
	struct UiOverride
	{
		QString     modId;
		bool        hasTokens = false;
		QJsonObject tokens;
		bool        hasQss = false;
		QString     qssPath;
		QString     qss;
	};

	//一个模组在本次运行中的完整状态
	struct ModEntry
	{
		bp::ModManifest manifest;
		QString     directory;          //绝对路径
		QStringList files;              //实际读到的覆盖文件（相对模组目录）
		QStringList featuresUsed;       //实际用到的能力（含按文件推断出来的）
		ModConfig   config;
		UiOverride  ui;                 //界面外观覆盖（可选）
		bool        enabled = false;    //玩家开关（已解析默认值）
		bool        active = false;     //通过校验 + 已启用 = 真的生效
		bool        blocked = false;    //被兼容性声明拒绝（这类连内容都不读）
		QString     blockedReason;      //不能加载的原因（空 = 没问题）
		QStringList warnings;           //能继续但需要知道的问题（未知键、坏字段……）
		int         patchesApplied = 0; //成功并入的覆盖条数（玩法 + 道具 + 关卡 + 界面）
	};

	static ModManager& instance();

	//扫描 + 校验 + 叠加（幂等；重复调用只在第一次真正读盘）
	//改完 mods 目录或开关后想立刻生效，调用 reload()
	bool load();
	void reload();
	bool isLoaded() const { return m_loaded; }

	//全部模组（含未启用/未通过的），按 priority 升序
	QList<ModEntry> mods() const { return m_entries; }
	//真正生效的模组
	QList<bp::ModManifest> activeMods() const;
	//汇总问题（给设置界面显示"⚠ N 个模组未加载"；空 = 一切正常）
	QStringList problems() const { return m_problems; }
	//外部模块（原生模组宿主）追加一条问题：让它和别的问题一起出现在模组页与日志里
	void addProblem(const QString& problem) { m_problems.append(problem); }

	//本构建的兼容性声明（Data/mod_api.json；文件缺失时用内置兜底）
	const bp::ModApiDeclaration& declaration() const { return m_declaration; }
	//声明文件本身的问题（例如写坏了），空 = 没问题
	QString declarationProblem() const { return m_declarationProblem; }

	// ---------- 叠加结果（开局时用这些，而不是直接 bp::GameConfig()） ----------
	bp::GameConfig gameConfig() const;
	//道具覆盖：道具名 -> 覆盖字段
	const QHash<QString, QJsonObject>& itemPatches() const { return m_itemPatches; }
	//关卡覆盖/新增：关卡号 -> 覆盖字段
	const QHash<int, QJsonObject>& levelPatches() const { return m_levelPatches; }

	//界面外观覆盖（按 priority 顺序；ThemeManager 用它做令牌覆盖与样式表追加）
	const QList<UiOverride>& uiOverrides() const { return m_uiOverrides; }

	// ---------- 玩家开关（写 settings.ini） ----------
	//改完开关需要再调 reload() 才会立刻生效
	void setEnabled(const QString& modId, bool enabled);
	QStringList enabledIds() const;

private:
	ModManager() = default;
	ModManager(const ModManager&) = delete;
	ModManager& operator=(const ModManager&) = delete;

	//声明：读 Data/mod_api.json（失败就用内置兜底）
	void loadDeclaration();
	//扫描目录：读 mod.json、比对声明、读 config、解析启用状态
	void scanMods();
	//校验某个 mod 的三份覆盖并统计条数；merge=true 时才真的并进总表
	//（未启用的 mod 也会走一遍校验：这样在设置界面里能看到"它改了哪些东西"，
	//  但它的键不会影响游戏 —— 看得到、不影响，是这一层最合适的取舍）
	void applyEntry(ModEntry& entry, bool merge);

	//某模组当前是否启用（玩家没动过开关时用作者建议值）
	bool resolveEnabled(const bp::ModManifest& manifest) const;

	bp::ModApiDeclaration m_declaration;
	QString m_declarationProblem;

	QList<ModEntry> m_entries;
	QHash<QString, QJsonObject> m_itemPatches;
	QHash<int, QJsonObject> m_levelPatches;
	QList<bp::GameplayPatch> m_gameplayPatches;
	QList<UiOverride> m_uiOverrides;
	QStringList m_problems;
	bool m_loaded = false;
};
