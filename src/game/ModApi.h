#pragma once
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace bp
{

// ============================================================
// mod 接口契约（引擎侧）
//
// 为什么放在引擎层而不是 core：这是"引擎能提供什么"的契约，
// 比对逻辑必须能脱机测试（tests/ 里就是这么测的）。
//
// 兼容性怎么判定（重要，别改错）：
//   本构建实现哪个版本的接口 -> kModApiVersion（代码常量）
//   本构建**接受**哪些版本的 mod -> Data/mod_api.json 里的 supportedModApiVersions（数据）
//   判定 = 用 mod.json 声明的 apiVersion / features 去查这份声明。
//
// 为什么"接受哪些版本"要放数据而不是写成 "支持 N 与 N-1" 的代码：
//   1. 破坏性改动时只需要从名单里删掉老版本号，不用改判断逻辑（也不用重新编译引擎）；
//   2. 可以表达"部分兼容"：老版本的 mod 只要它用到的能力还在，就继续能加载；
//   3. 声明文件本身就是给 mod 作者看的文档（history 里写着每版改了什么）。
// ============================================================

//本构建实现的 mod 接口版本（语义化字符串）
//
// 0.x 阶段的进位规则（和语义化版本一致，只是"破坏性"落在次版本号上）：
//   0.1.0 -> 0.1.1   **加法式**改动：新增能力、新增可覆盖的键、新增令牌/objectName、
//                     **C ABI 末尾追加函数与字段**（老 mod 不用重编）
//   0.1.1 -> 0.1.2   **加法式**：把 ABI 里预留的槽位填成真函数（模式注册 + 三个钩子），
//                     再末尾追加 set_tile / set_player_start（布局与偏移一个字节都没变）
//   0.1.2 -> 0.2.0   **破坏性**改动：删能力、改字段含义、改键名、重命名既有令牌或 objectName
//   1.0.0            留给"接口稳定、可以承诺不再变"的那一天
//
// 为什么用字符串而不是整数：程序版本是 0.1.0，接口版本却写着 1，两处对不上会让人困惑；
// 而且 0.x 阶段需要"次版本号 = 破坏性"这种表达，整数 1/2/3 表达不了。
inline const QString kModApiVersion = QStringLiteral("0.1.2");

//能力名：mod.json 的 features 里写这些字符串
//开放的能力是"加"出来的，未开放的写成 false，mod 声明了就会被拒绝（而不是行为诡异）
namespace ModFeature
{
	inline const QString Gameplay = QStringLiteral("gameplay"); //改玩法数值
	inline const QString Items    = QStringLiteral("items");    //改道具数据
	inline const QString Levels   = QStringLiteral("levels");   //改/加关卡
	inline const QString Ui       = QStringLiteral("ui");       //改界面外观（令牌覆盖 + 追加样式表）
	inline const QString Hooks    = QStringLiteral("hooks");    //原生模组（C ABI，L2 已开放）
	inline const QString Modes    = QStringLiteral("modes");    //全新模式（未开放）
}

//全部已知能力名：用来区分"未知能力名（作者拼错）"与"已知但本版本未开放"
inline QStringList allModFeatures()
{
	return QStringList() << ModFeature::Gameplay << ModFeature::Items
						 << ModFeature::Levels << ModFeature::Ui
						 << ModFeature::Hooks << ModFeature::Modes;
}

//一个 mod 的自我声明（mod.json）
struct ModManifest
{
	QString     id;                         //目录名建议一致；为空时用目录名兜底
	QString     name;
	QString     version;
	QString     author;
	QString     description;
	int         priority = 0;               //同键冲突时：priority 大的后应用（覆盖小的）
	bool        enabledByDefault = true;    //作者建议的默认开关；玩家可以在设置里改
	QString     apiVersion;                 //按哪一版接口写的（空 = 没写，会被拒绝）
	QStringList features;                   //用到哪些能力；留空表示"按用了哪些文件自动判断"
	//原生模组（C ABI）：native = true 时必须显式开启"允许原生模组"才会加载
	bool        native = false;
	QString     nativeLib;                  //DLL 文件名（不含扩展名；留空用 id）

	//从 JSON 解析。缺字段不算错（会兜底或留空），返回的 error 只表示"读不出这个对象"
	static ModManifest fromJson(const QJsonObject& obj, QString* error = nullptr);
};

//版本与能力声明（Data/mod_api.json）；文件缺失时用 builtin() 兜底
struct ModApiDeclaration
{
	QString                    engineApiVersion = kModApiVersion;
	QStringList                supportedModApiVersions;   //本构建接受的 mod apiVersion 名单
	QHash<QString, bool>       features;                  //能力名 -> 是否可用
	QList<QPair<QString, QString>> history;               //每版接口的说明（给作者看的文档）

	//代码内置的兜底声明：与仓库里的 Data/mod_api.json 保持一致
	static ModApiDeclaration builtin();
	//从 JSON 读；结构不对时保留能读到的部分，并写一条 error
	static ModApiDeclaration fromJson(const QJsonObject& obj, QString* error = nullptr);

	bool supportsVersion(const QString& apiVersion) const;
	bool supportsFeature(const QString& feature) const;

	//比对：返回空串表示"可以加载"；否则返回**给人看的原因**（会显示在设置界面的模组页）
	QString check(const ModManifest& manifest) const;
	bool isAcceptable(const ModManifest& manifest) const { return check(manifest).isEmpty(); }

	//支持版本名单的显示形式（"0.1.0、0.1.1"）
	QString supportedVersionsText() const;
};

} // namespace bp
