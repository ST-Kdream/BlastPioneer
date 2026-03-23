#include "RulesWindow.h"
#include "MainWindow.h"
#include "SettingsManager.h"

//构造函数
RulesWindow::RulesWindow(MainWindow* mainWin, QWidget* parent) :QWidget(parent),mainWin(mainWin)
{
	setWindowFlags(Qt::Window);
	setupUI();
}

//UI设置（包括一个信号槽连接）
void RulesWindow::setupUI()
{
    setWindowTitle("游戏规则");

    QByteArray savedGeo = SettingsManager::instance()->loadWindowGeometry();
    if (!savedGeo.isEmpty())
        restoreGeometry(savedGeo);
    else {
        resize(800, 600);
        move(100, 100);
    }

    backBtn = new QPushButton("返回", this);
    Qrules = new QTextEdit(this);

    backBtn->setObjectName("backBtn");
    backBtn->setStyleSheet(R"(
        QPushButton#backBtn {
            font-size: 18px;
            padding: 10px 20px;
            min-width: 120px;
            border: none;
            border-radius: 8px;
            background-color: #4CAF50;
            color: white;
        }
        QPushButton#backBtn:hover { background-color: #45a049; }
        QPushButton#backBtn:pressed { background-color: #3d8b40; }
    )");

    connect(backBtn, &QPushButton::clicked, this, &RulesWindow::goBack);
    getRules();

    //布局
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 40, 20, 50);
    mainLayout->setSpacing(15);
    mainLayout->addWidget(Qrules);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    backBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    btnLayout->addWidget(backBtn);
    mainLayout->addLayout(btnLayout);

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
	QFile file(":/Data/gamerules.txt");
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
	SettingsManager::instance()->saveWindowGeometry(saveGeometry());

	if (mainWin) {
		mainWin->show();
		mainWin->raise();
		mainWin->activateWindow();
	}

	close();
}

//重写关闭函数
void RulesWindow::closeEvent(QCloseEvent* event)
{
	SettingsManager::instance()->saveWindowGeometry(saveGeometry());
	QWidget::closeEvent(event);
}