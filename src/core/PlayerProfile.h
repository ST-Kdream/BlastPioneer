#pragma once
#include "PlayerInfo.h"

//玩家资料单例：全项目唯一的 PlayerInfo 持有者
//取代原先"MainWindow 持有副本、PlayerWindow 再写一份"的两套存档逻辑
class PlayerProfile
{
private:
	PlayerProfile() = default;

	PlayerInfo m_info;
	bool m_loadedFromFile = false;

public:
	//单例访问
	static PlayerProfile& instance();

	//从 Data/playerData.json 加载
	//返回 false 表示文件不存在或解析失败（此时内部为默认值，调用方应进入注册/欢迎流程）
	bool load();

	//保存到 Data/playerData.json
	bool save();

	//数据访问
	PlayerInfo& info() { return m_info; }
	const PlayerInfo& info() const { return m_info; }

	//是否来自磁盘上的存档文件
	bool loadedFromFile() const { return m_loadedFromFile; }
};
