#include "BootstrapTask.h"
#include "DirectoryLayout.h"
#include "FileManager.h"
#include "FigManager.h"
#include "ItemDatabase.h"
#include "LevelDatabase.h"
#include "ModManager.h"
#include "NativeModHost.h"
#include "LaunchGuard.h"
#include "PlayerProfile.h"
#include "SettingsManager.h"
#include <QDir>
#include <QFileInfo>

BootstrapTask::BootstrapTask(QObject* parent) : QObject(parent) {}

//执行一步：发进度、写日志、累计错误数
void BootstrapTask::reportStep(int index, int total, const StepResult& result)
{
	int percent = qBound(0, (index + 1) * 100 / total, 100);

	if (!result.success) { ++m_errorCount; }

	emit progress(percent, result.name);

	QString text = result.detail.isEmpty() ? result.name : result.name + " - " + result.detail;
	FileManager::instance().log("boot", QString("[%1/%2] %3 %4")
		.arg(index + 1).arg(total).arg(result.success ? "OK" : "FAIL", text));
}

//主流程
void BootstrapTask::run()
{
	//打点：启动管线开始（还没碰原生模组）。
	//这一行与 run() 里的加载打点是"崩在哪一段"的分界线：崩在 bootstrap 说明还没轮到模组
	LaunchGuard::instance().markStage(QStringLiteral("bootstrap"));

	//顺序执行的步骤表
	const int total = 7;

	reportStep(0, total, stepEnsureDirs());
	reportStep(1, total, stepLoadSettings());
	reportStep(2, total, stepLoadProfile());
	reportStep(3, total, stepLoadMods());
	reportStep(4, total, stepLoadItems());
	reportStep(5, total, stepLoadLevels());
	reportStep(6, total, stepPreloadFigs());

	BootstrapResult result;
	result.playerDataFound = m_playerDataFound;
	result.modCount = m_modCount;
	result.errorCount = m_errorCount;

	FileManager::instance().log("boot", QString("初始化完成：存档=%1 mod=%2 错误=%3")
		.arg(m_playerDataFound ? "有" : "无").arg(m_modCount).arg(m_errorCount));

	emit finished(result);
}

//步骤1：确保运行时目录存在
BootstrapTask::StepResult BootstrapTask::stepEnsureDirs()
{
	StepResult result;
	result.name = "检查运行目录";

	DirectoryLayout& layout = DirectoryLayout::instance();
	layout.ensureRuntimeDirs();

	result.success = QDir(layout.dataDir()).exists() && QDir(layout.assetsDir()).exists();
	result.detail = result.success ? layout.root() : "缺少 Data/ 或 Assets/ 目录";
	return result;
}

//步骤2：加载设置（缺失或损坏时由 QSettings 自行生成默认值）
BootstrapTask::StepResult BootstrapTask::stepLoadSettings()
{
	StepResult result;
	result.name = "加载设置";

	QString path = DirectoryLayout::instance().settingsPath();
	bool existed = FileManager::instance().exists(path);

	//触发一次读写，让 QSettings 在文件不存在时生成默认配置
	SettingsManager::instance();

	result.success = true;
	result.detail = existed ? path : "未找到，将使用默认设置";
	return result;
}

//步骤3：加载玩家存档
BootstrapTask::StepResult BootstrapTask::stepLoadProfile()
{
	StepResult result;
	result.name = "加载玩家数据";

	m_playerDataFound = PlayerProfile::instance().load();
	result.success = true;   //没有存档不是错误，而是首次启动的正常分支
	result.detail = m_playerDataFound
		? QString("玩家 %1").arg(PlayerProfile::instance().info().getUserName())
		: "无存档，进入注册流程";
	return result;
}

//步骤4：加载模组（Data/mod_api.json + mods/*）
//必须排在道具/关卡前面：L1 的覆盖是在那两个库加载时并入的
//"读不到 mods 目录"是正常情况，不是错误
BootstrapTask::StepResult BootstrapTask::stepLoadMods()
{
	StepResult result;
	result.name = "加载模组";

	ModManager& mods = ModManager::instance();
	mods.load();

	//原生模组（C ABI）在这一步之后加载：它依赖 mods 的扫描结果与玩家的总开关。
	//顺序也重要 —— 原生模组的 init 可能想读关卡/道具数据，所以放在它们之前不合适；
	//但反过来，它的数据覆盖又必须和 L1 一样在道具/关卡加载前并入……
	//取舍：**原生模组的 init 只允许订阅与初始化，不保证能读到关卡数据**（契约 §8 写明了）。
	//崩溃保护由 LaunchGuard 负责（见 docs/原生模组ABI契约.md §3.3）
	QStringList nativeProblems;
	NativeModHost::instance().loadAll(&nativeProblems);
	for (const QString& problem : nativeProblems)
	{
		mods.addProblem(problem);
	}

	m_modCount = mods.mods().size();

	const int active = mods.activeMods().size();
	const int problems = mods.problems().size();

	result.success = true;
	if (m_modCount == 0)
	{
		result.detail = "未安装模组";
	}
	else if (problems == 0)
	{
		result.detail = QString("%1 个，生效 %2 个").arg(m_modCount).arg(active);
	}
	else
	{
		result.detail = QString("%1 个，生效 %2 个（%3 条问题）").arg(m_modCount).arg(active).arg(problems);
	}
	return result;
}

//步骤5：加载道具数据
BootstrapTask::StepResult BootstrapTask::stepLoadItems()
{
	StepResult result;
	result.name = "加载道具数据";

	result.success = ItemDatabase::instance().load();
	result.detail = QString("%1 条").arg(ItemDatabase::instance().all().size());
	return result;
}

//步骤6：加载关卡数据（Data/levels/*.json）
//注意：读不到关卡不是错误 —— 引擎会退回"程序化随机关卡"，
//所以这里 success 只看"有没有出问题"，不因为目录缺失就报失败
BootstrapTask::StepResult BootstrapTask::stepLoadLevels()
{
	StepResult result;
	result.name = "加载关卡数据";

	LevelDatabase& levels = LevelDatabase::instance();
	const bool anyLevel = levels.load();

	result.success = true;
	if (!anyLevel)
	{
		result.detail = "未找到 Data/levels，使用程序化随机关卡";
	}
	else
	{
		const QStringList problems = levels.problems();
		result.detail = problems.isEmpty()
			? QString("%1 关").arg(levels.count())
			: QString("%1 关（%2 条问题）").arg(levels.count()).arg(problems.size());
	}
	return result;
}

//步骤7：预加载图片资源（只生成 QImage，线程安全）
//注意顺序：本步骤跑在"加载道具数据"之后，所以这里能顺带把道具图标一起读进来
BootstrapTask::StepResult BootstrapTask::stepPreloadFigs()
{
	StepResult result;
	result.name = "预加载图片资源";

	FigManager::instance().preloadAll();
	FigManager::instance().preloadItemImages();

	result.success = true;
	result.detail = "完成";
	return result;
}
