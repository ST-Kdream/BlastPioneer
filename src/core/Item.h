#pragma once
#include <QString>

// ============================================================
// 道具：纯数据（名字 / 描述 / 价格 / 图标路径 / 效果ID / 掉落权重）
//
// 阶段三的重要变化：这里**不再保存"效果实现"**
//   旧设计：Item 里存 std::function<bool(GameWindow*)>，由数据层调用界面方法
//          —— 数据层反向依赖界面层，加一个道具要改 C++ 并重新编译
//   新设计：Item 只存"效果ID"（itemInfo.json 里的 effect 字段），
//          真正的效果由 bp::GameEngine::applyEffect(effectId) 实现
//
// 阶段二追加：
//   effect      —— 效果ID，让"加一个道具"彻底变成改 JSON
//   dropWeight  —— 掉落权重，> 0 才会出现在掉落表里（0 = 只能靠彩蛋获得）
// ============================================================
class Item
{
private:
	QString name;
	QString description;
	QString iconPath;
	int price = 0;
	QString effect;
	double dropWeight = 0.0;

public:
	//2个构造函数
	Item() = default;
	Item(const QString& name, const QString& description, int price, const QString& iconPath = "",
		 const QString& effect = QString(), double dropWeight = 0.0);

	//接口函数
	QString getName() const { return name; }
	QString getDescription() const { return description; }
	QString getIconPath() const { return iconPath; }
	int getPrice() const { return price; }
	//效果ID（itemInfo.json 的 effect）；为空表示"还没登记效果"
	QString getEffect() const { return effect; }
	//掉落权重（itemInfo.json 的 dropWeight）；<= 0 表示不会随机掉落
	double getDropWeight() const { return dropWeight; }
};
