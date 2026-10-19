#pragma once
// ============================================================
// 输入分发测试（并入 bp_tests，只依赖 QtCore）
//
// 为什么单独做这一组：Esc 反复出问题（先是弹退出确认、后是"没反应"），
// 而"按键有没有送到界面、界面有没有消费它"是纯逻辑 ——
// 应该自动验证，而不是靠手测推理事件链路。
//
// 真实链路：AppHost 全局事件过滤器 -> bp::dispatchKey(当前界面, 事件)
// 这里用假的 IKeyHandler 验证 dispatchKey 的语义。
//
// 本文件靠调用方（test_game_core.cpp）提供的 CHECK / CHECK_EQ_INT 宏与计数变量。
// ============================================================
#include "KeyDispatch.h"
#include <QKeyEvent>

namespace keydispatch_test
{

//假处理器：记录收到的调用
class FakeHandler : public IKeyHandler
{
public:
	bool backResult = true;      //handleBack 的返回值（true = 我处理了）
	int  backCalls = 0;
	int  pressCalls = 0;
	int  releaseCalls = 0;
	int  lastKey = 0;

	bool handleBack() override { ++backCalls; return backResult; }
	void handleKeyPress(QKeyEvent* event) override { ++pressCalls; lastKey = event->key(); }
	void handleKeyRelease(QKeyEvent* event) override { ++releaseCalls; lastKey = event->key(); }
};

} // namespace keydispatch_test

//由 test_game_core.cpp 在 main 里调用
static void testKeyDispatch()
{
	using keydispatch_test::FakeHandler;

	// ---------- 1. Esc 必须走到 handleBack ----------
	{
		FakeHandler handler;
		QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);

		const bool consumed = dispatchKey(&handler, &esc);

		CHECK_EQ_INT(handler.backCalls, 1, "Esc calls handleBack exactly once");
		CHECK(consumed, "Esc is consumed when the handler handles it");
		CHECK_EQ_INT(handler.pressCalls, 0, "Esc must not be delivered as a normal key press");
	}

	// ---------- 2. 界面拒绝处理时返回 false（调用方据此决定是否继续传播） ----------
	{
		FakeHandler handler;
		handler.backResult = false;
		QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);

		const bool consumed = dispatchKey(&handler, &esc);

		CHECK_EQ_INT(handler.backCalls, 1, "Esc still reaches handleBack");
		CHECK(!consumed, "unhandled Esc is reported as not consumed");
	}

	// ---------- 3. 普通按键走 handleKeyPress ----------
	{
		FakeHandler handler;
		QKeyEvent w(QEvent::KeyPress, Qt::Key_W, Qt::NoModifier);

		const bool consumed = dispatchKey(&handler, &w);

		CHECK(consumed, "normal key press is consumed");
		CHECK_EQ_INT(handler.pressCalls, 1, "normal key press delivered once");
		CHECK_EQ_INT(handler.backCalls, 0, "normal key press must not trigger handleBack");
		CHECK_EQ_INT(handler.lastKey, Qt::Key_W, "delivered key equals the pressed key");
	}

	// ---------- 4. 抬键走 handleKeyRelease ----------
	{
		FakeHandler handler;
		QKeyEvent w(QEvent::KeyRelease, Qt::Key_W, Qt::NoModifier);

		dispatchKey(&handler, &w);

		CHECK_EQ_INT(handler.releaseCalls, 1, "key release delivered once");
		CHECK_EQ_INT(handler.pressCalls, 0, "key release must not be delivered as a press");
		CHECK_EQ_INT(handler.backCalls, 0, "key release must not trigger handleBack");
	}

	// ---------- 5. 空格（放炸弹）也要能到达 ----------
	{
		FakeHandler handler;
		QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);

		dispatchKey(&handler, &space);

		CHECK_EQ_INT(handler.pressCalls, 1, "Space is delivered");
		CHECK_EQ_INT(handler.lastKey, Qt::Key_Space, "Space key code preserved");
	}

	// ---------- 6. 空指针不能崩 ----------
	{
		QKeyEvent w(QEvent::KeyPress, Qt::Key_W, Qt::NoModifier);
		CHECK(!dispatchKey(nullptr, &w), "null handler is safe");
		FakeHandler handler;
		CHECK(!dispatchKey(&handler, nullptr), "null event is safe");
	}
}
