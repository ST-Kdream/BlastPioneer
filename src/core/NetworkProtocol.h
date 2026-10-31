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
    MSG_GAME_OVER = 7,    // 游戏结束消息
    MSG_GAME_PAUSE = 8,   // 主机暂停/继续（客户端据此显示同一块暂停遮罩）
    MSG_ROOM_CLOSED = 9    // 房主解散房间（大厅里还在等待的客户端回到空闲状态）
};

//MSG_GAME_OVER 的一个特殊 winnerId：客户端主动离开对局。
//主机收到后把"留下的那个玩家"判为胜者并结束本局 —— 否则客户端走了、主机还在傻等。
constexpr int kOpponentLeftWinnerId = -2;

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

//游戏状态快照（主机广播给客户端）
//
// 关于坐标：这里的 QPointF 是"连续格子坐标"，即 x = 列、y = 行（可以是 3.75 这样的值）。
// 之所以不用像素：引擎只认逻辑格子，像素换算是各端视图自己的事。
// 主机会在发之前把 GridPosF 折成 (col, row)，客户端收到后直接还原成 GridPosF。
//
// 关于玩家：playerNames / playerRanges / playerMaxBombs 是"一次对局里不变"的信息，
// 每帧都捎带一份（数据量极小），换来客户端 HUD 不用等额外消息就能显示名字与数值。
struct GameStateSnapshot
{
    //游戏信息
    bool gameRunning = true;
    int winnerId = -1; // -1表示未结束

    //玩家信息
    QList<int> playerIds;
    QList<QPointF> playerPositions;   // (列, 行) 连续格子坐标
    QList<int> playerLives;
    QList<int> playerMaxLives;        // 上限也会被道具改（"眼睛"等），HUD 要一起同步
    QList<QString> playerNames;
    QList<int> playerRanges;
    QList<int> playerMaxBombs;

    //炸弹信息
    QList<QPointF> bombPositions;     // (列, 行) 连续格子坐标
    QList<int> bombOwners;
    QList<float> bombTimers;          //剩余引信（秒）
    QList<float> bombFuseTotals;      //总引信（秒）：客户端据此画"剩余圆弧"的进度
    QList<int> bombRanges;

    //掉落物（砖块被炸开后掉的）
    QList<QPointF> dropPositions;
    QList<QString> dropNames;
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
        << snap.playerMaxLives
        << snap.playerNames << snap.playerRanges << snap.playerMaxBombs
        << snap.bombPositions << snap.bombTimers << snap.bombFuseTotals << snap.bombRanges
        << snap.bombOwners << snap.dropPositions << snap.dropNames
        << snap.gameRunning << snap.winnerId;
    return out;
}

//游戏状态反序列化
inline QDataStream& operator>>(QDataStream& in, GameStateSnapshot& snap)
{
    in >> snap.playerIds >> snap.playerPositions >> snap.playerLives
        >> snap.playerMaxLives
        >> snap.playerNames >> snap.playerRanges >> snap.playerMaxBombs
        >> snap.bombPositions >> snap.bombTimers >> snap.bombFuseTotals >> snap.bombRanges
        >> snap.bombOwners >> snap.dropPositions >> snap.dropNames
        >> snap.gameRunning >> snap.winnerId;
    return in;
}