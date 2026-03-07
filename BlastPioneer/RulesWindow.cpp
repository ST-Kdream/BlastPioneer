#include "RulesWindow.h"

//构造函数
RulesWindow::RulesWindow(MainWindow* mainWin, QWidget* parent) :QWidget(parent),mainWin(mainWin)
{
	setupUI();
}

//UI设置函数（包括一个信号槽链接）
void RulesWindow::setupUI()
{
	setWindowTitle("游戏规则");

	QByteArray savedGeo = SettingsManager::instance()->loadWindowGeometry();
	if (!savedGeo.isEmpty())
	{
		restoreGeometry(savedGeo);
	}
	else
	{
		resize(800, 600);
		move(100, 100);
	}

	//文本控件和返回按钮控件
	backBtn = new QPushButton("返回", this);
	Qrules = new QTextEdit(this);
	backBtn->setStyleSheet(R"(
								  QPushButton{background-color:#4CAF50;color:white;font-size:20px;padding:10px;border-radius:5px;}
								  QPushButton:hover{background-color:#45a049;}
								  QPushButton:pressed{background-color:#3d8b40;}
							  )");

	connect(backBtn, &QPushButton::clicked, this, &RulesWindow::goBack);
	getRules();

	//布局
	QVBoxLayout* mainLayout = new QVBoxLayout();
	mainLayout->addWidget(Qrules);
	mainLayout->addWidget(backBtn);
	setLayout(mainLayout);
}

//析构函数
RulesWindow::~RulesWindow()
{
	//保存窗口设置
	SettingsManager::instance()->saveWindowGeometry(saveGeometry());
}

//读取规则函数
void RulesWindow::getRules()
{
	QFile file(":/gamerules.txt");
	QString content;
	if (file.open(QIODevice::ReadOnly) | QIODevice::Text)
	{
		content = QString::fromUtf8(file.readAll());
		file.close();
		Qrules->setText(content);
	}
	else
	{
		QMessageBox::critical(this, "错误", "游戏规则读取失败");
	}
}

//返回函数
void RulesWindow::goBack()
{
	// 保存窗口设置
	SettingsManager::instance()->saveWindowGeometry(saveGeometry());

	// 显示主窗口，关闭规则窗口
	mainWin->show();
	this->hide();
}

//重写关闭函数
void RulesWindow::closeEvent(QCloseEvent* event)
{
	SettingsManager::instance()->saveWindowGeometry(saveGeometry());
	QWidget::closeEvent(event);
}