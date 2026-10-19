#pragma once
#include <QString>
#include <QStringList>
#include <QList>
#include <QPair>
#include <cstdint>
#include <functional>
#include <memory>

//为什么这里直接 include 引擎类型：下面要按值存 GridPos（QPair<GridPos,int> 需要完整类型），
//而 GameTypes.h 只依赖 QtCore，拉进来的代价很小
#include "GameTypes.h"

class QElapsedTimer;

struct BpHostApi;
struct BpModApi;
struct BpGameEvent;
struct BpModeDesc;

namespace bp
{
	class GameEngine;
	class GameBoard;
	class IMapGenerator;
	struct GameEvent;
	struct LevelSpec;
	struct GeneratedMap;
	struct Enemy;
	struct FrameContext;
	struct GridPos;
}

// ============================================================
// 原生模组宿主（C ABI 的引擎侧实现）
//
// 职责：加载 DLL → 校验版本与 ABI 指纹 → 调 bp_mod_entry/init →
//       提供宿主函数表 → 分发事件 → 每步调 on_tick → 记账（命令数/耗时）→ 日志。
//
// 契约见 docs/原生模组ABI契约.md。三条设计不变量：
//   1. **模组拿不到引擎对象的指针**：只能通过下面实现的函数表访问，且只读数据是快照拷贝；
//   2. **写操作都是请求**：宿主先校验再应用，校验不过就返回错误码（引擎逻辑不受影响）；
//   3. **不静默失效**：没实现的能力 has_feature 返回 0、函数返回 BP_ERR_UNSUPPORTED，
//      绝不"调了没反应"。
//
// 线程：**全部在游戏线程**。宿主函数表不保证线程安全，也不打算保证。
// ============================================================
class NativeModHost
{
public:
	//一个原生模组的加载结果（给界面与日志用）
	struct LoadedMod
	{
		QString id;
		QString name;
		QString version;
		QString dllPath;
		QString apiVersion;        //DLL 自己声明的 ABI 版本（"0.1.1"）
		bool    active = false;    //真的加载并 init 成功
		QString reason;            //没加载成功的原因（空 = 没问题）
	};

	//一个原生模组的运行统计（契约 §7.3：只报信息、不做决定，判断权交给玩家）
	//"上限之内也记一份"是有意的：模组页只在真的超过软预算时才提示，
	//因为常态是 0.0x ms，把它摆在玩家面前只会变成噪音
	struct ModStats
	{
		QString id;
		int     callbacks = 0;        //被调用的次数（事件回调 + on_tick）
		int     slowCallbacks = 0;    //超过软预算（2ms）的次数
		double  avgCallbackMs = 0.0;
		double  maxCallbackMs = 0.0;
	};

	//一个已加载模组的运行状态（定义在 .cpp；宿主函数表靠它区分调用者）
	struct ModContext;

	static NativeModHost& instance();

	//扫描 ModManager 里的原生模组并加载（幂等：重复调用只加载一次）
	//problems 追加"能继续但要让人知道"的问题
	void loadAll(QStringList* problems = nullptr);
	void unloadAll();                     //清空记录（DLL 本身永不卸载，见契约 §3.2）
	bool isLoaded() const { return m_loaded; }

	//安全模式：本次运行不加载任何原生模组（契约 §3.3）
	void setSafeMode(bool on) { m_safeMode = on; }
	bool safeMode() const { return m_safeMode; }

	const QList<LoadedMod>& mods() const { return m_mods; }
	int activeCount() const;

	//逐模组的耗时统计（模组页显示"该模组平均回调 x ms，可能造成卡顿"）
	QList<ModStats> stats() const;

	// ---------- 模组注册的模式（「开始游戏 → 选择模式」里的卡片） ----------
	struct ModMode
	{
		QString modId;
		QString id;            //模组内唯一；全局标识是 modId/modeId
		QString name;
		QString hint;
		QString description;
		int     kind = 0;
		int     flags = 0;
		int     playerLives = 0;
		int     playerMaxBombs = 0;
		int     playerBombRange = 0;
		double  timeLimitSeconds = 0.0;
		bool    usable = false;    //能不能玩（false 时 reason 说明原因）
		QString reason;

		QString key() const { return modId + QLatin1Char('/') + id; }
	};

	const QList<ModMode>& modes() const { return m_modes; }
	const ModMode* findMode(const QString& modId, const QString& modeId) const;
	const ModMode* activeMode() const;

	//开始一个模组模式：把 mode 声明的钩子接到这个引擎上，并按 flags 关掉引擎的默认判定。
	//返回 false 时 reason 说明原因（模式不可用 / 引擎为空 / 模组已经卸载……）
	bool beginMode(const QString& modId, const QString& modeId, bp::GameEngine* engine, QString* reason);
	//结束当前模式（会调模组的 on_mode_stop；崩溃时不会调）
	void endMode();
	//本模式的地图生成器适配器（模式没声明 TAKE_OVER_MAP 时返回 nullptr）
	std::unique_ptr<bp::IMapGenerator> takeModeMapGenerator();
	//时间倍率的落点：界面把帧循环的 setTimeScale 接进来（没接时 set_time_scale 返回"不支持"）
	void setTimeScaleSink(std::function<void(double)> sink) { m_timeScaleSink = std::move(sink); }
	bool hasTimeScaleSink() const { return static_cast<bool>(m_timeScaleSink); }

	//本局结束的原因（模组胜负担出的一句话；空 = 用界面默认文案）
	QString modeFinishReason() const { return m_modeFinishReason; }

	// ---------- 与引擎的接口 ----------
	//游戏界面在开局时把这一局的引擎"借"给宿主，退出时传 nullptr。
	//宿主不持有引擎的所有权，也永远不会在界面没绑定时做任何事。
	void setActiveEngine(bp::GameEngine* engine);

	//引擎每完成一个逻辑步后调用：先分发这一步的事件，再依次调各模组的 on_tick
	void onEngineStepped(double stepSeconds);

	// ---------- 联机握手 ----------
	//生效的原生模组集合指纹（id + 版本 + ABI 版本，排序后哈希）——大厅校验用
	QString modSetHash() const;
	QStringList activeIds() const;

	// ---------- 给宿主函数表调用的内部接口（不对外用） ----------
	//当前正在被调用的模组（回调期间有效；外部调用时为 nullptr）
	QString currentModId() const;
	void    logForCurrentMod(int level, const QString& text);
	//当前这一局的引擎（宿主函数表的只读查询/写请求都靠它；可能为 nullptr）
	bp::GameEngine* engineForMods() const { return m_engine; }

	// ---------- 宿主函数表的内部实现（ABI 那一侧调用，界面不要用） ----------
	//地图生成期间：set_tile / spawn_enemy 写进"待生成"的缓冲区，而不是直接改引擎
	bool isGeneratingMap() const { return m_genBoard != nullptr; }
	bool registerGeneratedEnemy(int32_t row, int32_t col, int32_t hp);
	bool setGeneratedTile(int32_t row, int32_t col, int32_t tile);
	bool setGeneratedPlayerStart(int32_t row, int32_t col);
	//注册模式（只能在模组的 init 里调；失败返回 ABI 的错误码）
	int32_t registerModeForCurrentMod(const BpModeDesc* desc);
	//时间倍率（宿主函数表拿到它之后转给帧循环）
	void applyTimeScale(double scale);
	//真正的地图生成：把棋盘交给模组的 on_generate_map（takeModeMapGenerator 的适配器调它）
	void generateModeMap(bp::GameBoard& board, const bp::LevelSpec& spec, bp::GeneratedMap* out);

private:
	NativeModHost() = default;
	NativeModHost(const NativeModHost&) = delete;
	NativeModHost& operator=(const NativeModHost&) = delete;

	//回调记账：进入时重置本次调用的状态，离开时统计耗时并在超预算时记日志
	void enterContext(ModContext* context);
	void leaveContext(ModContext* context, QElapsedTimer& timer);
	//事件分发：引擎事件 -> ABI 事件号与载荷；合成事件（关卡开始/胜利/失败）；发给订阅者
	void dispatchEngineEvent(uint32_t type, const bp::GameEvent& event);
	void dispatchSynthetic(uint32_t type, int32_t row, int32_t col);
	void dispatchSyntheticLevel(uint32_t type, int32_t level, int32_t victory);
	void dispatchToSubscribers(const BpGameEvent& event);

	// ---------- 模式 ----------
	ModContext* findContext(const QString& modId) const;
	//把 BpModeDesc 变成一个 ModMode（校验不通过时 usable=false 且填 reason）
	ModMode parseModeDesc(ModContext* context, const void* desc);
	//引擎侧的三个钩子（beginMode 把它们挂到引擎上；返回 0 / -1 = 交回引擎默认）
	int  onModEnemyThink(const bp::Enemy& enemy, const bp::FrameContext& ctx,
						 int& direction, bool& placeBomb);
	int  onModCheckWin(QString* reason);

	QList<LoadedMod> m_mods;
	bool    m_loaded = false;
	bool    m_safeMode = false;
	bp::GameEngine* m_engine = nullptr;
	int     m_lastLevel = -1;
	//上一次看到的阶段（用 int 存，避免头文件依赖 GameTypes.h）：
	//胜利/失败事件是宿主**合成**的（引擎没有这两个事件），靠阶段变化检测
	int     m_lastPhase = -1;

	//模式注册表与当前模式
	QList<ModMode> m_modes;
	QString m_activeModId;
	QString m_activeModeId;
	bp::GameEngine* m_modeEngine = nullptr;
	QString m_modeFinishReason;
	std::function<void(double)> m_timeScaleSink;

	//地图生成期间的落点：此时 set_tile / spawn_enemy 写进这里，而不是直接改引擎
	//（引擎在 generate() 返回之后才建敌人，直接塞进引擎会被它清掉）
	bp::GameBoard* m_genBoard = nullptr;
	QList<QPair<bp::GridPos, int>> m_genSpawns;
	bp::GridPos m_genPlayerStart;
	bool m_genPlayerStartSet = false;

	QList<ModContext*> m_contexts;
};
