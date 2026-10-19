#pragma once
#include "Item.h"
#include <QString>
#include <QList>

//道具数据库单例：全项目唯一解析 itemInfo.json 的地方
//磁盘上的 Data/itemInfo.json 优先，缺失时回落到资源内置副本 ":/Data/itemInfo.json"
class ItemDatabase
{
private:
	ItemDatabase() = default;

	QList<Item> m_items;
	bool m_loaded = false;

public:
	//单例访问
	static ItemDatabase& instance();

	//加载（幂等）：已加载或已成功过则直接返回 true；失败时保留旧数据
	bool load();

	//强制重新加载（清掉"已加载"标记再读一遍）
	//用途：设置界面里开关了某个 mod 之后，道具覆盖要立刻反映出来
	bool reload();

	//全部道具
	QList<Item> all() const { return m_items; }

	//按名字查找，找不到返回 nullptr（返回指针指向内部缓存，不要保存）
	Item* find(const QString& name);

	//按名字取图标相对路径，找不到返回空串
	QString iconPath(const QString& name) const;

	//按名字取效果ID：优先用 itemInfo.json 里的 effect 字段，
	//没写时回落到 ItemEffects.cpp 的内置映射（兼容旧数据）
	QString effectForItem(const QString& name) const;

	//掉落权重表（只含 dropWeight > 0 的道具），可直接灌给 GameEngine::setDropTable
	QList<QPair<QString, double>> dropTable() const;

	//全部道具名（顺序与 JSON 一致）
	QStringList itemNames() const;

	//图标相对路径 -> 绝对路径（供 QPixmap 加载）
	static QString absoluteIconPath(const QString& relativePath);

	//是否已加载成功
	bool isLoaded() const { return m_loaded; }
};
