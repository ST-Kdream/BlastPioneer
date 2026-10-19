#pragma once
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

// ============================================================
// 界面令牌表（美化体系的 L0 层）
//
// 为什么需要它：QSS 没有变量。颜色直接写进 qss 就等于"改一个主色要全局搜索替换"，
// 而且 mod 想换肤只能整份替换样式表（于是主题一升级，mod 就过期）。
// 这里做一层最小的模板替换：qss 里写 @primary，应用前替换成实际值。
//
// 为什么放在 core 且只依赖 QtCore：
//   替换与合并都是纯字符串/JSON 逻辑，于是它能在控制台测试里被直接验证
//   （不需要 QApplication，也就不需要把界面拖进测试工程）。
// ============================================================
namespace UiTokens
{
	//内置默认令牌（与 ui/tokens.json 保持一致；文件缺失时用它兜底）
	QHash<QString, QString> builtin();

	//把一份 JSON 对象并进令牌表（只接受字符串值）
	//非字符串值会被忽略并往 problems 追加一句人话说明（不静默）
	void merge(QHash<QString, QString>& tokens, const QJsonObject& obj,
			   const QString& source, QStringList* problems);

	//把 qss 里的 @名字 替换成令牌值
	//令牌名只认 ASCII 字母/数字/下划线（中文只可能出现在注释里，不该被当成令牌名）
	//未定义的令牌**保持原样**，并把名字记进 missing —— 不崩、也不静默消失
	QString substitute(const QString& qss, const QHash<QString, QString>& tokens,
					   QStringList* missing = nullptr);

	//令牌名清单（排序后；日志/设置界面展示用）
	QStringList names(const QHash<QString, QString>& tokens);
}
