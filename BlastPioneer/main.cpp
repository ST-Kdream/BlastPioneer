#include <QApplication>
#include "MainWindow.h"
#include "StartupEffectWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName("ST-Kdream");
    app.setApplicationVersion("1.0.0");

    StartupEffectWindow startWin;
    MainWindow mainWin;

    QObject::connect(&startWin, &StartupEffectWindow::finished, &mainWin, &MainWindow::show);
    startWin.showup(2000);
    return app.exec();
}
