#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName("ST-Kdream");
    app.setApplicationVersion("1.0.0");
    return app.exec();
}
