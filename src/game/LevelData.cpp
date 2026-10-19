#include "LevelData.h"
#include "GameBoard.h"

#include <QJsonArray>
#include <QJsonValue>

namespace bp
{

// ---------- 字符表 ----------
// 关卡文件里的字符含义只在这里定义一次，别的代码（含工具脚本）都引用它

bool JsonMapGenerator::isWallChar(QChar c)   { return c == QLatin1Char('#') || c == QLatin1Char('X'); }
bool JsonMapGenerator::isBrickChar(QChar c)  { return c == QLatin1Char('B') || c == QLatin1Char('b'); }
bool JsonMapGenerator::isEmptyChar(QChar c)  { return c == QLatin1Char('.') || c == QLatin1Char(' '); }
bool JsonMapGenerator::isPlayerChar(QChar c) { return c == QLatin1Char('P'); }
bool JsonMapGenerator::isEnemyChar(QChar c)  { return c == QLatin1Char('E'); }

bool JsonMapGenerator::isKnownChar(QChar c)
{
	return isWallChar(c) || isBrickChar(c) || isEmptyChar(c) || isPlayerChar(c) || isEnemyChar(c);
}

// ---------- 解析 ----------

//从 JSON 对象读出一个关卡定义
//设计要点：所有键都可选；"读不懂"用 error 表达（调用方记日志并跳过），绝不抛异常
LevelDefinition LevelDefinition::fromJson(const QJsonObject& obj, QString* error)
{
	LevelDefinition def;

	//数值字段用 isDouble() 判断"到底写没写"，这样 0 也是合法值（例如 enemyCount = 0）
	auto readInt = [&obj](const char* key, int fallback)
	{
		const QJsonValue value = obj.value(QLatin1String(key));
		return value.isDouble() ? value.toInt() : fallback;
	};

	def.id = readInt("id", 0);
	def.name = obj.value(QStringLiteral("name")).toString();
	def.brickCount = readInt("bricks", -1);
	def.enemyCount = readInt("enemyCount", -1);
	def.enemyHp = readInt("enemyHp", -1);
	def.coins = readInt("coins", -1);
	def.ep = readInt("ep", -1);
	def.seed = static_cast<quint32>(readInt("seed", 0));

	//出生点：[行, 列]
	const QJsonValue start = obj.value(QStringLiteral("playerStart"));
	if (start.isArray())
	{
		const QJsonArray pair = start.toArray();
		if (pair.size() >= 2)
		{
			def.playerStart = GridPos(pair.at(0).toInt(1), pair.at(1).toInt(1));
		}
	}

	//手写地图：一行一个字符串
	const QJsonValue layoutValue = obj.value(QStringLiteral("layout"));
	if (layoutValue.isArray())
	{
		const QJsonArray rows = layoutValue.toArray();
		for (const QJsonValue& row : rows)
		{
			def.layout.append(row.toString());
		}

		if (def.layout.size() != kRows)
		{
			if (error) { *error = QStringLiteral("layout 行数应为 %1，实际 %2").arg(kRows).arg(def.layout.size()); }
		}
		else
		{
			for (int r = 0; r < def.layout.size(); ++r)
			{
				const QString& line = def.layout.at(r);
				if (line.size() != kCols)
				{
					if (error)
					{
						*error = QStringLiteral("layout 第 %1 行应为 %2 列，实际 %3")
							.arg(r + 1).arg(kCols).arg(line.size());
					}
					break;
				}

				int badCol = -1;
				QChar badChar;
				for (int c = 0; c < line.size(); ++c)
				{
					if (!JsonMapGenerator::isKnownChar(line.at(c)))
					{
						badCol = c;
						badChar = line.at(c);
						break;
					}
				}

				if (badCol >= 0)
				{
					if (error)
					{
						*error = QStringLiteral("layout 第 %1 行第 %2 列出现未知字符 '%3'")
							.arg(r + 1).arg(badCol + 1).arg(badChar);
					}
					break;
				}
			}
		}
	}

	//出生点越界会让玩家一开局就卡进墙里，直接退回默认值
	if (!GameBoard::inBounds(def.playerStart))
	{
		def.playerStart = GridPos(1, 1);
	}

	return def;
}

//把"没写的字段"用 spec 补齐（JSON 优先、配置公式兜底）
LevelSpec LevelDefinition::toSpec(const LevelSpec& spec) const
{
	LevelSpec out = spec;

	if (brickCount >= 0) { out.brickCount = brickCount; }
	if (enemyCount >= 0) { out.enemyCount = enemyCount; }
	if (enemyHp >= 0) { out.enemyHp = enemyHp; }
	if (seed != 0) { out.seed = seed; }

	out.level = (id > 0) ? id : spec.level;
	out.playerStart = playerStart;
	return out;
}

// ---------- 生成 ----------

GeneratedMap JsonMapGenerator::generate(GameBoard& board, const LevelSpec& spec)
{
	const LevelSpec effective = m_definition.toSpec(spec);

	//没有手写地图：交给随机生成器，行为与"程序化关卡"完全一致
	if (m_definition.hasLayout())
	{
		GeneratedMap result;
		QString error;
		if (buildFromLayout(board, effective, result, &error))
		{
			return result;
		}
		//layout 坏了：退回随机生成，保证"这一关仍然能玩"
	}

	return m_fallback.generate(board, effective);
}

//按 layout 铺棋盘
//规则：
//   1. 先清空并按字符铺（墙 / 砖块 / 空地），出生点与敌人在字符位置记下来
//   2. 最后强制补一圈外墙 —— 手写地图漏画边框不会让玩家走出地图
//   3. 'P' 决定出生点；没写就用 spec.playerStart
bool JsonMapGenerator::buildFromLayout(GameBoard& board, const LevelSpec& spec,
									   GeneratedMap& out, QString* error) const
{
	if (m_definition.layout.size() != kRows)
	{
		if (error) { *error = QStringLiteral("layout 行数不是 %1").arg(kRows); }
		return false;
	}

	board.fill(TileType::Empty);

	QList<EnemySpawn> enemies;
	GridPos playerStart = spec.playerStart;

	for (int r = 0; r < m_definition.layout.size(); ++r)
	{
		const QString& line = m_definition.layout.at(r);
		if (line.size() != kCols)
		{
			if (error) { *error = QStringLiteral("layout 第 %1 行列数不是 %2").arg(r + 1).arg(kCols); }
			return false;
		}

		for (int c = 0; c < line.size(); ++c)
		{
			const QChar ch = line.at(c);
			const GridPos cell(r, c);

			if (isWallChar(ch))        { board.setTile(cell, TileType::Wall); }
			else if (isBrickChar(ch))  { board.setTile(cell, TileType::Brick); }
			else if (isPlayerChar(ch)) { playerStart = cell; }
			else if (isEnemyChar(ch))
			{
				EnemySpawn spawn;
				spawn.pos = cell;
				spawn.hp = spec.enemyHp;
				enemies.append(spawn);
			}
			else if (!isEmptyChar(ch))
			{
				if (error)
				{
					*error = QStringLiteral("layout 第 %1 行第 %2 列出现未知字符 '%3'")
						.arg(r + 1).arg(c + 1).arg(ch);
				}
				return false;
			}
		}
	}

	//边框永远封死：手写地图最外圈一律当墙，避免"玩家走出地图"这类哭笑不得的问题
	board.buildBorderWalls();

	//出生点若被墙或砖块占住（手写地图写错了），清成空地，别让玩家卡在墙里
	const TileType startTile = board.tile(playerStart);
	if (startTile == TileType::Wall || startTile == TileType::Brick)
	{
		board.setTile(playerStart, TileType::Empty);
	}
	board.markEntity(playerStart, EntityKind::Player);

	out.ok = true;
	out.enemies = enemies;
	out.playerStart = playerStart;
	out.hasPlayerStart = true;
	out.usedLayout = true;
	return true;
}

} // namespace bp
