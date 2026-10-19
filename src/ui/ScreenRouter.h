#pragma once
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QList>
#include <QHash>
#include <QPointer>
#include <QVariant>
#include <QWidget>
#include <functional>

class QStackedWidget;
class ScreenBase;

//屏幕 id 常量：注册与跳转都用这里，避免各处手写字符串写错
//用 inline 常量（C++17）保证所有翻译单元共享同一个对象
namespace ScreenId
{
	inline const QString Splash = "Splash";
	inline const QString Home = "Home";
	inline const QString Welcome = "Welcome";
	inline const QString Player = "Player";
	inline const QString Bag = "Bag";
	inline const QString Shop = "Shop";
	//模式选择：主页 →「开始游戏」→ 这里（单人 / 联机 / 将来的扩展模式）
	inline const QString ModeSelect = "ModeSelect";
	//模组：主页上的独立入口（原来塞在设置页最下面）
	inline const QString Mods = "Mods";
	inline const QString LevelSelect = "LevelSelect";
	inline const QString Rules = "Rules";
	inline const QString Settings = "Settings";
	inline const QString Lobby = "Lobby";
	inline const QString SingleGame = "SingleGame";
	inline const QString MultiGame = "MultiGame";
}

//窗口栈单例：所有界面切换的唯一入口
//界面工厂用 std::function 表示，这样跳转方不需要知道目标界面的类型，也就不需要 include 它
class ScreenRouter : public QObject
{
	Q_OBJECT

public:
	using Factory = std::function<ScreenBase*(QWidget* parent)>;

	static ScreenRouter& instance();

	//绑定宿主窗口里的 QStackedWidget（AppHost 构造时调用一次）
	void setHost(QStackedWidget* stack);

	//注册界面工厂
	void registerScreen(const QString& id, Factory factory);
	//是否已注册
	bool hasScreen(const QString& id) const { return m_factories.contains(id); }

	//把所有已注册界面预先创建好（启动收尾时调用）
	//好处：第一次进入某界面时不再"边创建边显示"，切换更顺滑；
	//界面此时不会进入窗口栈，也不会被显示
	void precreateAll();

	//已注册的界面数量（界面自检用）
	int registeredCount() const { return m_factories.size(); }

	//清空窗口栈并显示指定界面（用于启动完成后进入主页/欢迎页）
	void reset(const QString& id, const QVariantMap& args = QVariantMap());
	//入栈并显示
	void push(const QString& id, const QVariantMap& args = QVariantMap());
	//替换栈顶（例如"关卡选择 -> 游戏"、"大厅 -> 对战"，返回时仍回到原来那一层）
	void replace(const QString& id, const QVariantMap& args = QVariantMap());
	//出栈；栈底时发出 quitRequested
	bool pop();

	//当前界面 id / 栈深度
	QString currentId() const;
	int depth() const { return m_stack.size(); }
	//当前界面指针（可能为空）
	ScreenBase* currentScreen() const;

signals:
	//界面切换完成，AppHost 用它更新窗口标题
	void screenChanged(const QString& id, const QString& title);
	//已经退无可退，请求退出程序
	void quitRequested();

private:
	ScreenRouter() = default;
	ScreenRouter(const ScreenRouter&) = delete;
	ScreenRouter& operator=(const ScreenRouter&) = delete;

	//按工厂创建界面（或复用常驻界面）
	ScreenBase* createScreen(const QString& id, bool* reused);
	//把界面设为当前页（resume=true 时补一次 onEnter）
	void activate(ScreenBase* screen, const QVariantMap& args, bool resume);
	//离开栈顶（onExit + 按需销毁）
	void leaveTop();
	//让栈顶转入后台但保留在栈里
	void suspendTop();
	//清空整栈
	void clearStack();

	QPointer<QStackedWidget> m_host;
	QHash<QString, Factory> m_factories;
	QHash<QString, ScreenBase*> m_cache;   //常驻界面缓存
	QList<ScreenBase*> m_stack;
};
