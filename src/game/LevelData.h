#pragma once
#include "GameTypes.h"
#include "MapGenerator.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace bp
{

// ============================================================
// 关卡定义：一关的全部"数据"
//
// 它存在的理由：以前"第几关"只意味着一组公式（砖块数 = 20 + 5*level，
// 敌人 = level 个……），想加一个手工设计的关卡就得改 C++ 并重新编译。
// 现在一关 = Data/levels 下的一个 JSON 文件，引擎只认下面这个结构，
// 不认文件名、不认路径（于是 mod 想加关卡，只要往目录里丢文件）。
//
// 所有字段都是**可选**的：没写的项由 LevelSpec（也就是 GameConfig 的公式）兜底。
// 这条很重要——"只写一半的 JSON"和"老数据缺字段"都不会让游戏打不开。
// ============================================================
struct LevelDefinition
{
	//关卡号（1 起，也是"第几个文件"的排序依据）
	int     id = 0;
	QString name;

	//数值型字段：负数表示"没写，用公式"
	int     brickCount = -1;
	int     enemyCount = -1;
	int     enemyHp = -1;
	int     coins = -1;
	int     ep = -1;

	//0 表示"用外面传进来的种子"（联机时用大厅广播的种子，单机用关卡种子）
	quint32 seed = 0;

	GridPos playerStart = GridPos(1, 1);

	//手写地图（可空）：一个字符一格
	//  '#' / 'X' 外墙      'B' / 'b' 砖块
	//  'P' 玩家出生点      'E' 敌人       '.' / ' ' 空地
	QStringList layout;

	//从 JSON 解析。缺字段不算错；行列不一致、出现未知字符才算结构性错误
	//出错时把原因写进 error（不抛异常、不弹窗），返回的仍是一个可用的结构
	static LevelDefinition fromJson(const QJsonObject& obj, QString* error = nullptr);

	bool hasLayout() const { return !layout.isEmpty(); }

	//数值兜底：把"没写的字段"用 spec 补齐
	LevelSpec toSpec(const LevelSpec& spec) const;
};

// ============================================================
// JSON 关卡生成器：把 LevelDefinition 铺成棋盘
//
// 两种模式：
//   1. 有 layout  -> 按字符铺（手工设计关卡）
//   2. 没有 layout -> 转交 RandomMapGenerator（只改数值的"程序化关卡"）
//
// 任何一处不合法都会**退回随机生成**并写一条日志，而不是让这一关打不开。
// "坏数据不能让游戏进不去"这条从阶段五 mods 起就是硬要求，这里先立住。
// ============================================================
class JsonMapGenerator : public IMapGenerator
{
public:
	explicit JsonMapGenerator(const LevelDefinition& definition) : m_definition(definition) {}

	GeneratedMap generate(GameBoard& board, const LevelSpec& spec) override;
	QString name() const override { return QStringLiteral("JsonMapGenerator"); }

	const LevelDefinition& definition() const { return m_definition; }

	//字符含义（关卡文件与工具脚本共用同一份定义，避免各处硬编码）
	static bool isWallChar(QChar c);
	static bool isBrickChar(QChar c);
	static bool isEmptyChar(QChar c);
	static bool isPlayerChar(QChar c);
	static bool isEnemyChar(QChar c);
	static bool isKnownChar(QChar c);

private:
	//按 layout 铺设；成功返回 true（结果写进 out），失败时把原因写进 error
	bool buildFromLayout(GameBoard& board, const LevelSpec& spec, GeneratedMap& out, QString* error) const;

	LevelDefinition m_definition;
	//退回随机生成时用它（保持"同种子同地图"的语义）
	RandomMapGenerator m_fallback;
};

} // namespace bp
