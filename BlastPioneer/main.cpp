#include <QApplication>
#include "MainWindow.h"
#include "SettingsManager.h"
#include "StartupEffectWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName("ST-Kdream");
    app.setApplicationVersion("1.0.0");

    if (SettingsManager::instance()->showStartupEffect())
    {
        StartupEffectWindow startWin;
        MainWindow* mainWin = nullptr;

        QObject::connect(&startWin, &StartupEffectWindow::dataLoaded,
            [&](const PlayerInfo& playerInfo, const QList<Item>& items) 
            { 
                mainWin = new MainWindow(playerInfo, items);
                QObject::connect(&startWin, &StartupEffectWindow::finished, [mainWin]() {mainWin->show(); });
            });

        QObject::connect(&startWin, &StartupEffectWindow::finished, [&]() 
            {
                if (!mainWin)
                {
                    mainWin = new MainWindow();
                    mainWin->show();
                }
            });

        startWin.start();
        return app.exec();
    }
    else
    {
        MainWindow mainWin;
        mainWin.show();
        return app.exec();
    }
}
