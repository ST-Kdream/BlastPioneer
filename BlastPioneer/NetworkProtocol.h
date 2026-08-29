#pragma once
#include <QMetaType>
#include <QDataStream>
#include <QHostAddress>
#include <QPointF>
#include <QMap>
#include <QList>

//消息类型枚举
enum MessageType
{
    MSG_JOIN_REQ = 1,   // 客户端请求加入房间
    MSG_JOIN_ACK = 2,   // 主机回复加入请求（也用作广播房间信息）
    MSG_PLAYER_LIST = 3,   // 主机广播当前玩家列表
    MSG_START_GAME = 4,   // 主机通知开始游戏
    MSG_GAME_STATE = 5,   // 主机广播游戏状态快照
    MSG_PLAYER_INPUT = 6,   // 客户端发送玩家输入
    MSG_GAME_OVER = 7    // 游戏结束消息
};

//客户端IP+端口
struct ClientEndpoint
{
    QHostAddress address;
    quint16 port;
};

//玩家输入
struct PlayerInput
{
    int playerId = 0;
    bool is_moveup = false;
    bool is_movedown = false;
    bool is_moveleft = false;
    bool is_moveright = false;
    bool is_placeBomb = false;
};

//游戏状态快照
struct GameStateSnapshot
{
    //游戏信息
    bool gameRunning = true;
    int winnerId = -1; // -1表示未结束

    //玩家信息
    QList<int> playerIds;
    QList<QPointF> playerPositions;
    QList<int> playerLives;

    //炸弹信息
    QList<QPointF> bombPositions;
    QList<int> bombOwners;
    QList<float> bombTimers;
    QList<int> bombRanges;
};

// ====================== 运算符重载 =====================
//玩家输入序列化
inline QDataStream& operator<<(QDataStream& out, const PlayerInput& input)
{
    out << input.playerId << input.is_moveup << input.is_movedown << input.is_moveleft
         << input.is_moveright << input.is_placeBomb;
    return out;
}

//玩家输入反系列化
inline QDataStream& operator>>(QDataStream& data, PlayerInput& input)
{
    data >> input.playerId >> input.is_moveup >> input.is_movedown >> input.is_moveleft
         >> input.is_moveright >> input.is_placeBomb;
    return data;
}

//游戏状态序列化
inline QDataStream& operator<<(QDataStream& out, const GameStateSnapshot& snap)
{
    out << snap.playerIds << snap.playerPositions << snap.playerLives
        << snap.bombPositions << snap.bombTimers << snap.bombRanges
        << snap.bombOwners << snap.gameRunning << snap.winnerId;
    return out;
}

//游戏状态反序列化
inline QDataStream& operator>>(QDataStream& in, GameStateSnapshot& snap)
{
    in >> snap.playerIds >> snap.playerPositions >> snap.playerLives
        >> snap.bombPositions >> snap.bombTimers >> snap.bombRanges
        >> snap.bombOwners >> snap.gameRunning >> snap.winnerId;
    return in;
}