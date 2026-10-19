#pragma once
#include <QObject>
#include <QString>

//启动初始化结果
struct BootstrapResult
{
	bool playerDataFound = false;   //是否成功读到存档（false 时进入注册/欢迎流程）
	int  modCount = 0;              //mods 目录下识别到的模组数量
	int  errorCount = 0;            //出错步骤数量（不阻塞启动，只提示与记日志）
};

Q_DECLARE_METATYPE(BootstrapResult)

//启动初始化任务：在工作线程里执行全部"打开游戏之前"要做的事
//只做磁盘/数据/资源相关的操作，绝不触碰任何界面控件（结果通过信号回主线程）
//步骤：检查目录 -> 加载设置 -> 加载存档 -> 加载模组 -> 加载道具 -> 加载关卡 -> 预加载图片
//注意"模组"必须排在道具/关卡前面：L1 的覆盖是在它们加载时并入的
class BootstrapTask : public QObject
{
	Q_OBJECT

public:
	explicit BootstrapTask(QObject* parent = nullptr);

	//单步结果
	struct StepResult
	{
		QString name;          //步骤名（显示在启动界面上）
		QString detail;        //细节（写日志用）
		bool    success = true;
	};

public slots:
	//由工作线程调用；结束时发出 finished
	void run();

signals:
	//进度（percent 0~100）
	void progress(int percent, const QString& stepText);
	//全部完成
	void finished(const BootstrapResult& result);

private:
	//各步骤
	StepResult stepEnsureDirs();
	StepResult stepLoadSettings();
	StepResult stepLoadProfile();
	StepResult stepLoadItems();
	StepResult stepLoadLevels();
	StepResult stepPreloadFigs();
	StepResult stepLoadMods();

	//执行一步：发进度、写日志、累计错误数
	void reportStep(int index, int total, const StepResult& result);

	//统计 mods 目录下带 mod.json 的模组数量
	int countMods() const;

	bool m_playerDataFound = false;
	int  m_modCount = 0;
	int  m_errorCount = 0;
};
