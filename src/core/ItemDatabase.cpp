#include "ItemDatabase.h"
#include "DirectoryLayout.h"
#include "FileManager.h"
#include "ItemEffects.h"
#include "ModManager.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QStringList>

//单例访问
ItemDatabase& ItemDatabase::instance()
{
	static ItemDatabase s_instance;
	return s_instance;
}

//把 L1 模组的道具覆盖并进道具表
//
// 语义（与关卡覆盖一致）：**按字段**覆盖，只写差异。
//   { "生命药水": { "price": 10 } }  -> 只改价格，描述/图标/权重保持原值
// 补丁里的名字在原表里不存在时**新增**一个道具：这样 mod 加道具不用改引擎
// （它有没有效果取决于引擎的效果表认不认识那个 effect —— 不认识就是没效果，不会崩）
//
// 实现上不引入 setter：Item 是只读数据，改字段就"读旧值 + 覆盖指定字段 + 重新构造"，
// 于是"Item 纯数据、谁也别想悄悄改它"这条约定仍然成立
static void applyModItemPatches(QList<Item>& items)
{
	const QHash<QString, QJsonObject>& patches = ModManager::instance().itemPatches();
	if (patches.isEmpty()) { return; }

	for (auto it = patches.constBegin(); it != patches.constEnd(); ++it)
	{
		const QString name = it.key();
		const QJsonObject patch = it.value();

		Item* target = nullptr;
		for (Item& item : items)
		{
			if (item.getName() == name) { target = &item; break; }
		}

		if (!target)
		{
			//新增道具
			items.append(Item(name,
				patch.value(QStringLiteral("description")).toString(),
				patch.value(QStringLiteral("price")).toInt(),
				patch.value(QStringLiteral("iconPath")).toString(),
				patch.value(QStringLiteral("effect")).toString(),
				patch.value(QStringLiteral("dropWeight")).toDouble()));
			FileManager::instance().log("item", QStringLiteral("模组新增道具: %1").arg(name));
			continue;
		}

		//已有道具：只覆盖补丁里出现的字段
		auto fieldOr = [&patch](const char* key, const QString& fallback)
		{
			return patch.contains(QLatin1String(key)) ? patch.value(QLatin1String(key)).toString() : fallback;
		};

		const QString description = fieldOr("description", target->getDescription());
		const int price = patch.contains(QStringLiteral("price"))
			? patch.value(QStringLiteral("price")).toInt()
			: target->getPrice();
		const QString iconPath = fieldOr("iconPath", target->getIconPath());
		const QString effect = fieldOr("effect", target->getEffect());
		const double dropWeight = patch.contains(QStringLiteral("dropWeight"))
			? patch.value(QStringLiteral("dropWeight")).toDouble()
			: target->getDropWeight();

		*target = Item(name, description, price, iconPath, effect, dropWeight);
	}
}

//加载道具列表
bool ItemDatabase::load()
{
	if (m_loaded) { return true; }

	QString path = DirectoryLayout::instance().itemInfoPath();
	bool ok = false;
	QJsonArray array = FileManager::instance().readJsonArray(path, ":/Data/itemInfo.json", &ok);
	if (!ok)
	{
		FileManager::instance().log("item", "道具数据加载失败，沿用已有数据");
		return false;
	}

	QList<Item> items;
	for (const QJsonValue& value : array)
	{
		QJsonObject obj = value.toObject();
		QString name = obj["name"].toString();
		if (name.isEmpty()) { continue; }

		items.append(Item(name,
			obj["description"].toString(),
			obj["price"].toInt(),
			obj["iconPath"].toString(),
			obj["effect"].toString(),
			obj["dropWeight"].toDouble()));
	}

	//模组覆盖：必须在"读完磁盘"之后、"定稿"之前
	applyModItemPatches(items);

	m_items = items;
	m_loaded = true;
	FileManager::instance().log("item", QString("道具数据加载完成，共 %1 条").arg(m_items.size()));
	return true;
}

//强制重新加载（设置界面开关 mod 之后用）
bool ItemDatabase::reload()
{
	m_loaded = false;
	return load();
}

//按名字查找
Item* ItemDatabase::find(const QString& name)
{
	for (Item& item : m_items)
	{
		if (item.getName() == name) { return &item; }
	}
	return nullptr;
}

//按名字取图标相对路径
QString ItemDatabase::iconPath(const QString& name) const
{
	for (const Item& item : m_items)
	{
		if (item.getName() == name) { return item.getIconPath(); }
	}
	return QString();
}

//相对路径转绝对路径
QString ItemDatabase::absoluteIconPath(const QString& relativePath)
{
	if (relativePath.isEmpty()) { return QString(); }
	return DirectoryLayout::instance().root() + "/" + relativePath;
}

//按名字取效果ID
//优先级：JSON 的 effect 字段 > 内置静态映射。
//为什么保留内置映射：老存档 / 老 Data/itemInfo.json 里没有 effect 字段，
//直接删掉静态表会让这些数据"效果消失"，所以做成"数据优先、代码兜底"
QString ItemDatabase::effectForItem(const QString& name) const
{
	for (const Item& item : m_items)
	{
		if (item.getName() != name) { continue; }

		const QString effect = item.getEffect();
		if (!effect.isEmpty()) { return effect; }
		break;
	}

	//没加载成功（m_items 为空）或该道具没写 effect：回落内置映射
	return bp::effectIdForItemName(name);
}

//掉落权重表：只收 dropWeight > 0 的道具
QList<QPair<QString, double>> ItemDatabase::dropTable() const
{
	QList<QPair<QString, double>> table;
	for (const Item& item : m_items)
	{
		if (item.getDropWeight() > 0.0)
		{
			table.append(qMakePair(item.getName(), item.getDropWeight()));
		}
	}
	return table;
}

//全部道具名
QStringList ItemDatabase::itemNames() const
{
	QStringList names;
	for (const Item& item : m_items)
	{
		names.append(item.getName());
	}
	return names;
}
