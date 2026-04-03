#pragma once
#include <QWidget>
#include <QUdpSocket>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QHostAddress>

class MainWindow;
class MultiGameWindow;

class LobbyWindow :public QWidget
{
	Q_OBJECT

private:
	MainWindow* mainWin;
	QUdpSocket* udpSocket;
	bool isHost;
	quint16 serverPort;
	QHostAddress serverAddress;

	//UI控件
	QLineEdit* ipEdit;
	QLineEdit* portEdit;
	QPushButton* createHostBtn;
	QPushButton* joinBtn;
	QPushButton* startGameBtn;
	QPushButton* backBtn;
	QListWidget* playerListWidget;
	QLabel statusLabel;

	//主机专用：客户端地址映射
	QMap<QHostAddress, int> clientAddrToId;
	QMap<int, QString> playerNames;
	int nextPlayerId;

	void setupUI();
	void connectSignals();

public:
	explicit LobbyWindow(MainWindow* mainWin, QWidget* parent = nullptr);
	~LobbyWindow();

private slots:
	void createHost();
	void joinIn();
	void startGame();
	void goBack();
	void readPendingDatagrams();
	void processJoinRequest(const QHostAddress& sender, quint16 senderPort, const QByteArray& Data);
	void broadcastPlayerList();
	void sendStartGameToAll();
};