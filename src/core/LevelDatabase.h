#pragma once
#include "LevelData.h"

#include <QList>
#include <QString>
#include <QStringList>

// ============================================================
// 关卡数据库单例：全项目唯一解析 Data/levels/*.json 的地方
//
// 设计取向与 ItemDatabase 一致：
//   - 磁盘优先（mod / 玩家可以直接改文件，甚至覆盖同名关卡）
//   - 加载失败**不弹窗、不阻塞**：记进 problems()，游戏照常能玩
//     （目录缺失时引擎会退回"程序化关卡"，也就是阶段三之前的行为）
//   - 幂等：重复调用 load() 只在第一次真正读盘
//
// 为什么不用 qrc 兜底：一个"文件目录"没法映射成单个 qrc 路径。
// 保证 Data/levels 存在的机制是构建后的资源同步（见 vcxproj 的 CopyContentToOutDir），
// 而不是运行时的资源回落。
// ============================================================
class LevelDatabase
{
private:
	LevelDatabase() = default;

	QList<bp::LevelDefinition> m_levels;
	QStringList m_problems;
	bool m_loaded = false;

public:
	//单例访问
	static LevelDatabase& instance();

	//加载（幂等）：返回是否至少读到一关
	bool load();

	//强制重新加载（清掉"已加载"标记再读一遍）
	//用途：设置界面里开关了某个 mod 之后，关卡覆盖/新增要立刻可见
	bool reload();

	//是否已经加载过（不代表关卡数为 0）
	bool isLoaded() const { return m_loaded; }

	//关卡个数
	int count() const { return m_levels.size(); }

	//全部关卡（按关卡号升序）
	QList<bp::LevelDefinition> all() const { return m_levels; }

	//按关卡号查找；找不到返回 nullptr（指针指向内部缓存，不要保存）
	const bp::LevelDefinition* find(int level) const;

	//关卡名（找不到返回空串）
	QString nameOf(int level) const;

	//加载期间发现的问题（给设置界面/日志用，界面据此显示"⚠ N 个关卡未加载"）
	QStringList problems() const { return m_problems; }
};
