#pragma once
#include <QEvent>
#include <QKeyEvent>

// ============================================================
// 按键处理接口 + 统一分发函数
//
// 为什么单独抽出来：
//   键盘分发的正确性（尤其 Esc 能不能到达界面、会不会被重复处理）
//   属于"纯逻辑"，不该只能在真机上靠手测确认。
//   这里只依赖 QtCore，所以测试程序可以直接构造一个假处理器来验证，
//   不需要 QApplication、不需要窗口。
//
// 真实链路：AppHost 的全局事件过滤器 -> bp::dispatchKey(当前界面, 事件)
// ============================================================

//界面需要实现的三个动作（ScreenBase 会继承它）
class IKeyHandler
{
public:
	virtual ~IKeyHandler() = default;

	//返回 true 表示自己处理完了（窗口栈不再出栈）
	virtual bool handleBack() = 0;
	virtual void handleKeyPress(QKeyEvent* event) = 0;
	virtual void handleKeyRelease(QKeyEvent* event) = 0;
};

//把一次按键交给处理器：
//  Esc  -> handleBack()（返回值就是它是否处理了）
//  其它 -> handleKeyPress / handleKeyRelease，并返回 true（表示已消费）
// 返回 false 表示"没有任何人处理这个按键"
inline bool dispatchKey(IKeyHandler* handler, QKeyEvent* event)
{
	if (!handler || !event) { return false; }

	if (event->type() == QEvent::KeyPress && event->key() == Qt::Key_Escape)
	{
		return handler->handleBack();
	}

	if (event->type() == QEvent::KeyPress)
	{
		handler->handleKeyPress(event);
		return true;
	}

	if (event->type() == QEvent::KeyRelease)
	{
		handler->handleKeyRelease(event);
		return true;
	}

	return false;
}
