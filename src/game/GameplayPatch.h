#pragma once
#include "GameConfig.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace bp
{

// ============================================================
// 玩法数值覆盖（L1 数据 mod 的第一类）
//
// 形态：mods/<id>/config/gameplay.json 里写
//     { "playerSpeed": 4.2, "dropChance": 0.5, "multiSelfDamage": true }
// 只写"差异"——没写的键保持引擎默认值，于是引擎升级后新增的数值会自动跟上。
//
// 为什么要有白名单：
//     mod 打错一个键名（比如 "playerSpeeed"）如果不吭声，作者会以为生效了，
//     这种"静默失败"比报错难查得多。所以名单之外的键一律忽略 + 记一条问题。
//
// 为什么放引擎层：这张表是"引擎允许被改什么"的契约，必须能脱机测试。
// ============================================================
struct GameplayPatch
{
	QString key;
	double  value = 0.0;
	QString source;   //来源描述（例如 "example_numbers/config/gameplay.json"），报问题用

	GameplayPatch() = default;
	GameplayPatch(const QString& k, double v, const QString& s = QString())
		: key(k), value(v), source(s) {}
};

//允许被 mod 覆盖的键清单（顺序即文档顺序）
QStringList gameplayPatchKeys();

//这个键是不是布尔开关（true/false），而不是数值
bool isGameplayFlagKey(const QString& key);

//这个键是否存在
bool isGameplayPatchKey(const QString& key);

//应用一批覆盖：按列表顺序依次写入（后面的覆盖前面的）
//返回成功应用的条数；忽略的键会往 problems 里追加一句人话说明
int applyGameplayPatches(GameConfig& config, const QList<GameplayPatch>& patches, QStringList* problems);

//应用单个键；键不存在返回 false（不抛异常、不改任何字段）
bool applyGameplayPatch(GameConfig& config, const QString& key, double value);

} // namespace bp
