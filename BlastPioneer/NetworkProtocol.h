#pragma once
#include <QDataStream>
#include <QHostAddress>

//网络消息枚举类型
enum NetMsgType
{
	MSG_JOIN_REQ = 1,  //加入房间请求
	MSG_JOIN_ACK,  //加入响应
	MSG_PLAYER_LIST,  //同步玩家列表
	MSG_START_GAME,  //开始游戏
	MSG_GAME_STATE,  //游戏状态快照
	MSG_PLAYER_INPUT,  //玩家输入
	MSG_GAME_OVER  //游戏结束
};

//玩家输入数据结构
struct PlayerInput
{
	int playerId;
	bool moveUp;
	bool moveDown;
	bool moveLeft;
	bool moveRight;
	bool placeBomb;
};

//游戏状态数据结构
struct GameStateSnapshot
{
	//玩家位置和状态
	QList<int> playerIds;
	QList<QPointF> playerPositons;
	QList<bool> playerAlive;
	QList<int> playerLives;

	//炸弹列表
	QList<QPointF> bombPositions;
	QList<float> bombTimers;
	QList<int> bombRanges;
	QList<int> bombOwners;

	// 游戏是否进行中
	bool gameRunning;
	int winnerId;
};

//二进制流序列化和反序列化运算符重载
inline QDataStream& operator<<(QDataStream& out, const PlayerInput& input)
{
	out << input.playerId << input.moveUp << input.moveDown
		<< input.moveLeft << input.moveRight << input.placeBomb;
	return out;
}

inline QDataStream& operator>>(QDataStream& in, PlayerInput& input)
{
	in >> input.playerId >> input.moveUp >> input.moveDown
		>> input.moveLeft >> input.moveRight >> input.placeBomb;
	return in;
}