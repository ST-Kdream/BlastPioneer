#pragma once
#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QGridLayout>
#include <QMessageBox>
#include <QShowEvent>

class MainWindow;
class GameWindow;
class PlayerInfo;

class LevelSelectWindow :public QWidget
{
	Q_OBJECT

private:
	PlayerInfo& playerInfo;

	MainWindow* mainWin;
	GameWindow* gameWin;

	QLabel* title;
	QPushButton* backBtn;
	QPushButton* levelBtns[6];

	void setupUI();
	void setupLevelBtn();
	void showEvent(QShowEvent* event) override;

public:
	explicit LevelSelectWindow(PlayerInfo& playerInfo, QWidget* parent = nullptr);

private slots:
	void goBack();
	void goGameWindow(int level);
};
