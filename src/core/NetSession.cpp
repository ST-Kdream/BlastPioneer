#include "NetSession.h"
#include <QUdpSocket>

NetSession::NetSession(QObject* parent) : QObject(parent) {}

//单例访问
NetSession& NetSession::instance()
{
	static NetSession s_instance;
	return s_instance;
}

//获取 socket（首次调用时创建，之后一直复用）
QUdpSocket* NetSession::socket()
{
	if (!m_socket)
	{
		m_socket = new QUdpSocket(this);
	}
	return m_socket;
}

//是否已绑定
bool NetSession::isOpen() const
{
	return m_socket && m_socket->state() == QAbstractSocket::BoundState;
}

//绑定端口
bool NetSession::bind(quint16 port)
{
	return socket()->bind(QHostAddress::Any, port);
}

//关闭（保留对象，方便下次直接复用）
void NetSession::close()
{
	if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState)
	{
		m_socket->close();
	}
}

	//清空状态
void NetSession::reset()
{
	close();
	m_isHost = false;
	m_myPlayerId = 0;
	m_hostAddress = QHostAddress();
	m_hostPort = 0;
	m_matchSeed = 0;
	m_playerNames.clear();
	m_clientEndpoints.clear();
}
