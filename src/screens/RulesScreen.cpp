#include "RulesScreen.h"
#include "DirectoryLayout.h"
#include "FileManager.h"
#include "SideTabWidget.h"

#include <QTextBrowser>
#include <QLabel>
#include <QVBoxLayout>

RulesScreen::RulesScreen(QWidget* parent) : ScaffoldScreen(parent)
{
	setupUI();
}

void RulesScreen::setupUI()
{
	setPageTitle(QStringLiteral("游戏规则"), QStringLiteral("左侧选一节，右侧看内容"));

	//左侧导航（文字横排、滚轮滚动列表、没有上下箭头）—— 不用 QTabWidget::West，理由见 SideTabWidget.h
	m_tabs = new SideTabWidget(this);

	contentLayout()->addWidget(m_tabs, 1);

	addFooterStretch();
	addBackButton();
}

void RulesScreen::onEnter(const QVariantMap& args)
{
	Q_UNUSED(args);
	rebuildTabs();
}

//读取规则文本并按【小节】切成选项卡
//文本格式约定：以【xxx】单独成行的作为小节标题；
//   - 第一个【】之前的内容作为一个"概览"页（没有就不建）
//   - 每个【】后的内容归到该页，标题里保留方括号（读起来更清楚）
void RulesScreen::rebuildTabs()
{
	m_tabs->clearPages();

	const QString path = DirectoryLayout::instance().dataDir() + "/gamerules.txt";
	const QString content = FileManager::instance().readText(path, QStringLiteral(":/Data/gamerules.txt"));

	if (content.isEmpty())
	{
		FileManager::instance().log("rules", QStringLiteral("规则文件读取失败: %1").arg(path));

		QTextBrowser* fallback = new QTextBrowser(this);
		fallback->setObjectName("rulesView");
		fallback->setText(QStringLiteral("规则内容读取失败。请确认游戏文件完整，或重新安装一次。"));
		m_tabs->addPage(fallback, QStringLiteral("规则"));
		return;
	}

	//切分
	QStringList titles;
	QStringList bodies;
	QString currentBody;

	const QStringList lines = content.split(QLatin1Char('\n'));
	for (const QString& line : lines)
	{
		const QString trimmed = line.trimmed();

		const bool isSection = trimmed.startsWith(QStringLiteral("【"))
			&& trimmed.endsWith(QStringLiteral("】"))
			&& trimmed.size() > 2;

		if (!isSection)
		{
			currentBody += line + QLatin1Char('\n');
			continue;
		}

		//遇到新小节：把上一段收尾
		if (titles.isEmpty() && !currentBody.trimmed().isEmpty())
		{
			titles.append(QStringLiteral("概览"));
			bodies.append(currentBody);
		}
		else if (!titles.isEmpty())
		{
			bodies.append(currentBody);
		}
		else
		{
			//开头就是小节标题：前面只有空白，丢掉
			currentBody.clear();
		}

		titles.append(trimmed);
		currentBody.clear();
	}

	//最后一段
	if (!titles.isEmpty())
	{
		bodies.append(currentBody);
	}
	else
	{
		//整篇没有小节标题：退化成单页
		titles.append(QStringLiteral("规则"));
		bodies.append(content);
	}

	for (int i = 0; i < titles.size(); ++i)
	{
		QTextBrowser* view = new QTextBrowser(this);
		view->setObjectName("rulesView");
		view->setOpenExternalLinks(false);

		//小节标题在正文里再写一遍：左侧导航的标题可能被省略号截断，正文里要有完整标题
		const QString body = bodies.value(i).trimmed();
		view->setText(body.isEmpty() ? body
									 : QStringLiteral("%1\n\n%2").arg(titles.at(i), body));

		m_tabs->addPage(view, titles.at(i));
	}
}
