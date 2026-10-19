#include "UiTokens.h"

#include <QJsonValue>

namespace UiTokens
{

//内置默认：与 BlastPioneer/ui/tokens.json 保持一致。
//两处都写是刻意的 —— 文件是"设计该待的地方"，代码这份是"程序不能裸奔"的兜底
QHash<QString, QString> builtin()
{
	QHash<QString, QString> tokens;

	//颜色
	tokens.insert(QStringLiteral("primary"),   QStringLiteral("#2F8BEF"));
	tokens.insert(QStringLiteral("primaryHi"), QStringLiteral("#4BA0FF"));
	tokens.insert(QStringLiteral("primaryLo"), QStringLiteral("#1F6FCC"));
	tokens.insert(QStringLiteral("accent"),    QStringLiteral("#E0651A"));
	tokens.insert(QStringLiteral("accentHi"),  QStringLiteral("#F5792E"));
	tokens.insert(QStringLiteral("accentLo"),  QStringLiteral("#B94F12"));
	tokens.insert(QStringLiteral("success"),   QStringLiteral("#43A047"));
	tokens.insert(QStringLiteral("danger"),    QStringLiteral("#E5484D"));
	tokens.insert(QStringLiteral("bg"),        QStringLiteral("#F5F6F8"));
	tokens.insert(QStringLiteral("panel"),     QStringLiteral("#FFFFFF"));
	tokens.insert(QStringLiteral("line"),      QStringLiteral("#D8DCE2"));
	tokens.insert(QStringLiteral("ink"),       QStringLiteral("#22262B"));
	tokens.insert(QStringLiteral("inkWeak"),   QStringLiteral("#5C636D"));
	tokens.insert(QStringLiteral("onPrimary"), QStringLiteral("#FFFFFF"));
	tokens.insert(QStringLiteral("gold"),      QStringLiteral("#B8860B"));   //金色重点（金币、标题）

	//游戏画面（深色，与主题无关：画布颜色是玩法的一部分）
	tokens.insert(QStringLiteral("hudBg"),     QStringLiteral("#23262D"));
	tokens.insert(QStringLiteral("hudLine"),   QStringLiteral("#3A404B"));
	tokens.insert(QStringLiteral("hudInk"),    QStringLiteral("#FFFFFF"));
	tokens.insert(QStringLiteral("hudInkWeak"),QStringLiteral("#C9CFD8"));
	tokens.insert(QStringLiteral("scrim"),     QStringLiteral("rgba(10, 12, 16, 180)"));

	//主页主视觉卡片（深色底 + 亮色文字：在浅色页面里形成主次，也让主页不显空）
	tokens.insert(QStringLiteral("heroBg"),      QStringLiteral("#232A38"));
	tokens.insert(QStringLiteral("heroLine"),    QStringLiteral("#2F3949"));
	tokens.insert(QStringLiteral("heroInk"),     QStringLiteral("#FFFFFF"));
	tokens.insert(QStringLiteral("heroInkWeak"), QStringLiteral("#AEB8C7"));
	tokens.insert(QStringLiteral("heroAccent"),  QStringLiteral("#7FD3FF"));

	//形状与排版
	tokens.insert(QStringLiteral("radius"),    QStringLiteral("8px"));
	tokens.insert(QStringLiteral("radiusSm"),  QStringLiteral("4px"));
	tokens.insert(QStringLiteral("gap"),       QStringLiteral("14px"));
	tokens.insert(QStringLiteral("gapLg"),     QStringLiteral("24px"));
	tokens.insert(QStringLiteral("fontBase"),  QStringLiteral("14px"));
	tokens.insert(QStringLiteral("fontTitle"), QStringLiteral("22px"));
	tokens.insert(QStringLiteral("fontSmall"), QStringLiteral("12px"));

	return tokens;
}

void merge(QHash<QString, QString>& tokens, const QJsonObject& obj,
		   const QString& source, QStringList* problems)
{
	for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
	{
		const QString key = it.key();
		if (key.startsWith(QLatin1Char('_'))) { continue; }   //"_comment" 之类的注释键

		const QJsonValue value = it.value();
		if (!value.isString())
		{
			if (problems)
			{
				problems->append(QStringLiteral("%1: 令牌「%2」不是字符串，已忽略").arg(source, key));
			}
			continue;
		}

		tokens.insert(key, value.toString());
	}
}

QString substitute(const QString& qss, const QHash<QString, QString>& tokens, QStringList* missing)
{
	QString out;
	out.reserve(qss.size());

	//手写一遍扫描：QSS 里 '@' 只可能用来写令牌，遇到不认识的名字原样保留
	for (int i = 0; i < qss.size(); ++i)
	{
		const QChar ch = qss.at(i);
		if (ch != QLatin1Char('@'))
		{
			out.append(ch);
			continue;
		}

		//收集 @ 后面的名字
		//**只接受 ASCII 字母/数字/下划线**：QSS 里中文只可能出现在注释或说明里，
		//如果按"Unicode 字母"算，注释里的 @令牌 就会被当成未定义令牌报警（踩过一次）
		int end = i + 1;
		while (end < qss.size())
		{
			const QChar c = qss.at(end);
			const bool asciiName = (c.unicode() < 128)
				&& (c.isLetterOrNumber() || c == QLatin1Char('_'));
			if (asciiName) { ++end; }
			else { break; }
		}

		const QString name = qss.mid(i + 1, end - i - 1);
		if (name.isEmpty())
		{
			out.append(ch);   //孤立的 '@'：原样写出
			continue;
		}

		if (tokens.contains(name))
		{
			out.append(tokens.value(name));
		}
		else
		{
			out.append(ch);
			out.append(name);
			if (missing && !missing->contains(name)) { missing->append(name); }
		}

		i = end - 1;
	}

	return out;
}

QStringList names(const QHash<QString, QString>& tokens)
{
	QStringList list = tokens.keys();
	list.sort();
	return list;
}

} // namespace UiTokens
