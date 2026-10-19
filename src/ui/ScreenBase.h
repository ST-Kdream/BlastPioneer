#pragma once
#include "KeyDispatch.h"
#include <QWidget>
#include <QVariantMap>

//界面基类：所有页面都继承它，用来替代原先"每个界面都是一个独立顶层窗口"的写法
//职责约定：
//  1. 界面之间不互相持有指针、不互相 include；跳转统一通过 ScreenRouter
//  2. 进入/离开的实现写在 onEnter/onExit（替代原来的 showEvent/hide 跳转）
//  3. 返回按钮不要自己找上一个窗口，统一 emit requestBack()
class ScreenBase : public QWidget, public IKeyHandler
{
	Q_OBJECT

public:
	explicit ScreenBase(QWidget* parent = nullptr) : QWidget(parent)
	{
		//为什么必须设最小尺寸：界面没有 layout 时，QWidget 的 minimumSizeHint 默认是 (100,30)，
		//而 QStackedWidget 会把所有子页的最小尺寸取最大值 —— 于是窗口会被拖到 100x30 起步，
		//游戏界面就会用这个尺寸算出 cellSize≈2.7，所有实体挤在左上角几像素内。
		setMinimumSize(800, 600);
	}
	virtual ~ScreenBase() = default;

	//屏幕标识（用于日志、注册表），必须与注册时使用的 id 一致
	virtual QString screenId() const = 0;
	//窗口标题（AppHost 会拼成"爆破先锋 - xxx"）
	//注意：不能命名为 title()，QWidget 已通过 moc 属性占用了这个名字，会报"重定义"
	virtual QString displayTitle() const = 0;

	//是否常驻：true 表示离开后仍留在窗口栈中复用；false 表示离开即销毁，下次进入重新创建
	virtual bool persistent() const { return true; }

	//是否可以返回（栈底界面返回 false，返回按钮应隐藏或禁用）
	virtual bool canGoBack() const { return true; }

	//处理返回键（Esc）：返回 true 表示自己已经处理完，Router 不再出栈
	//默认只有游戏界面需要拦截
	virtual bool handleBack() override { return false; }

	//进入界面（args 为跳转时携带的参数）
	//约定：每次界面变得可见都会被调用（含从上层界面返回），需要刷新的数据都放这里
	virtual void onEnter(const QVariantMap& args) { Q_UNUSED(args); }

	//离开界面：停止计时器、断开网络、保存临时状态
	virtual void onExit() {}

	//键盘输入（由 AppHost 统一分发，不依赖控件焦点）
	//为什么不用自己的 keyPressEvent：Qt 的按键只送给有焦点的子控件，
	//父窗口收不到，容易出现"按键时灵时不灵"
	virtual void handleKeyPress(QKeyEvent* event) override { Q_UNUSED(event); }
	virtual void handleKeyRelease(QKeyEvent* event) override { Q_UNUSED(event); }

signals:
	//请求返回上一界面
	void requestBack();
	//请求退出程序（交给 AppHost 处理确认框与存档）
	void requestQuit();
};
