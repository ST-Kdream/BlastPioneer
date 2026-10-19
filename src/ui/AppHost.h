#pragma once
#include <QWidget>
#include <QString>

class QStackedWidget;
class QCloseEvent;
class QKeyEvent;
class QEvent;
class QLabel;

//应用宿主窗口：整个程序唯一的顶层窗口
//所有界面都是它内部 QStackedWidget 的一页，因此只有一个任务栏图标、一个 ×、一套样式
class AppHost : public QWidget
{
	Q_OBJECT

public:
	explicit AppHost(QWidget* parent = nullptr);

	//窗口栈容器（ScreenRouter 需要）
	QStackedWidget* stack() const { return m_stack; }

	//连接窗口栈信号（main 里在注册界面之前调用）
	void initialize();

	//请求退出程序：先确认，再存档，最后退出
	void requestExit();

private slots:
	//界面切换后更新窗口标题
	void onScreenChanged(const QString& id, const QString& title);

protected:
	//点 × 时走同一套退出流程
	void closeEvent(QCloseEvent* event) override;
	//全局键盘分发：Qt 的按键只送给有焦点的子控件，这里统一转给当前界面
	bool eventFilter(QObject* watched, QEvent* event) override;
	//Esc 兜底（焦点在本窗口自身时）
	void keyPressEvent(QKeyEvent* event) override;

private:
	//保存玩家数据与窗口位置
	void saveAll();
	//退出确认：返回是否可以继续退出
	bool confirmExit();
	//安全模式横幅：本次不加载原生模组时，在窗口最顶上横一条（契约 §3.3）
	void setupSafeModeBanner();

	QStackedWidget* m_stack = nullptr;
	QString m_currentId;
	QLabel* m_safeBanner = nullptr;
};
