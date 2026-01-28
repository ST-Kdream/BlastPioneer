#include "RulesWindow.h"

//构造函数
RulesWindow::RulesWindow(MainWindow* mainWin, QWidget* parent) :QWidget(parent),mainWin(mainWin)
{
	// 设置默认大小，如果QSettings中没有保存的值则使用默认值
	int defaultWidth = 600;
	int defaultHeight = 400;
	int width = settings->value("windows/width", defaultWidth).toInt();
	int height = settings->value("windows/height", defaultHeight).toInt();
	resize(width, height);

	// 恢复窗口位置
	QPoint defaultPos(100, 100);  // 默认位置
	QPoint savedPos = settings->value("RulesWindow/position", defaultPos).toPoint();
	move(savedPos);

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
	QVBoxLayout* mainLayout = new QVBoxLayout(this);
	mainLayout->addWidget(Qrules);
	mainLayout->addWidget(backBtn);
	setLayout(mainLayout);
}

//析构函数
RulesWindow::~RulesWindow()
{
	//保存窗口设置
	settings->setValue("windows/width", width());
	settings->setValue("windows/height", height());
	settings->setValue("windows/position", pos());
}

//读取规则函数
void RulesWindow::getRules()
{
	std::string ruleLine;
	std::stringstream ssrules;

	std::ifstream rulesText("gamerules.txt");
	if (rulesText.is_open())
	{
		while (getline(rulesText, ruleLine))
		{
			ssrules << ruleLine << std::endl;
		}
		rulesText.close();
	}
	else
	{
		ssrules << "wrong!"<<std::endl;
		QMessageBox::warning(nullptr, "游戏规则文件读取错误","无法打开游戏规则文件，请保证其与可执行程序在同一个文件夹下！");
	}

	Qrules->setText(QString::fromStdString(ssrules.str()));
}

//返回函数
void RulesWindow::goBack()
{
	// 保存窗口设置
	settings->setValue("RulesWindow/width", width());
	settings->setValue("RulesWindow/height", height());
	settings->setValue("RulesWindow/position", pos());

	// 显示主窗口，关闭规则窗口
	mainWin->show();
	this->close();
}