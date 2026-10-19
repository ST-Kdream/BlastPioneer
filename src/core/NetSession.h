#pragma once
#include <QObject>
#include <QHostAddress>
#include <QMap>
#include "NetworkProtocol.h"

class QUdpSocket;

//联机会话单例：持有一次联机过程中的共享状态
//大厅与对战界面共用同一个 UDP socket，避免"大厅隐藏后仍和游戏抢 readyRead"的问题
//大厅负责 bind/close，对战界面只负责收发
class NetSession : public QObject
{
	Q_OBJECT

public:
	//单例访问
	static NetSession& instance();

	//获取（首次调用时创建）UDP socket
	QUdpSocket* socket();

	//是否已绑定
	bool isOpen() const;
	//绑定到本机任意地址（port 传 0 表示由系统分配）
	bool bind(quint16 port);
	//关闭并释放 socket（不删除，保证指针长期有效）
	void close();

	//房间状态（大厅填充，对战界面读取）
	bool isHost() const { return m_isHost; }
	void setIsHost(bool isHost) { m_isHost = isHost; }

	int myPlayerId() const { return m_myPlayerId; }
	void setMyPlayerId(int id) { m_myPlayerId = id; }

	QHostAddress hostAddress() const { return m_hostAddress; }
	quint16 hostPort() const { return m_hostPort; }
	void setHostEndpoint(const QHostAddress& address, quint16 port)
	{
		m_hostAddress = address;
		m_hostPort = port;
	}

	QMap<int, QString> playerNames() const { return m_playerNames; }
	void setPlayerNames(const QMap<int, QString>& names) { m_playerNames = names; }

	QMap<int, ClientEndpoint> clientEndpoints() const { return m_clientEndpoints; }
	void setClientEndpoints(const QMap<int, ClientEndpoint>& endpoints) { m_clientEndpoints = endpoints; }

	//对局种子：由主机在"开始游戏"时决定并广播，客户端存下来
	//为什么必须共享：地图是两端各自按种子生成的，种子不同就会出现"我看得见你、你看不见我"
	quint32 matchSeed() const { return m_matchSeed; }
	void setMatchSeed(quint32 seed) { m_matchSeed = seed; }

	//清空一次联机的全部状态（返回大厅或退出联机时调用）
	void reset();

private:
	explicit NetSession(QObject* parent = nullptr);

	QUdpSocket* m_socket = nullptr;

	bool m_isHost = false;
	int m_myPlayerId = 0;
	QHostAddress m_hostAddress;
	quint16 m_hostPort = 0;
	QMap<int, QString> m_playerNames;
	QMap<int, ClientEndpoint> m_clientEndpoints;
	quint32 m_matchSeed = 0;
};
