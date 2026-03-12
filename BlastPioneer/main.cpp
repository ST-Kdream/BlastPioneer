#include <QApplication>
#include <QLoggingCategory>
#include "MainWindow.h"
#include "SettingsManager.h"
#include "StartupEffectWindow.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// 从资源文件加载物品列表（防止初始化失败）
QList<Item> initLoad()
{
    QList<Item> items;
    QFile itemFile(":/Data/itemInfo.json");
    if (itemFile.open(QIODevice::ReadOnly)) 
    {
        QByteArray data = itemFile.readAll();
        itemFile.close();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isArray()) 
        {
            for (auto value : doc.array()) 
            {
                QJsonObject obj = value.toObject();
                items.append(Item(obj["name"].toString(),
                    obj["description"].toString(),
                    obj["price"].toInt(),
                    obj["iconPath"].toString()));
            }
        }
    }
    return items;
}

int main(int argc, char* argv[])
{
    // 环境设置（保持不变）
    qputenv("QT_LOGGING_RULES", "qt.qpa.gl=true\nqt.qpa.qwindows=true\nqt.qpa.*=true");
    QApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    QApplication::setAttribute(Qt::AA_DisableShaderDiskCache);
    qputenv("QT_OPENGL", "angle");
    QApplication::setAttribute(Qt::AA_UseOpenGLES);
    qputenv("QT_OPENGL", "software");
    qputenv("QSG_RENDERER_LOOP", "basic");
    qputenv("QT_QUICK_BACKEND", "software");

    QApplication app(argc, argv);
    app.setOrganizationName("ST-Kdream");
    app.setApplicationVersion("1.0.0");

    // 如果设置中开启了启动特效
    if (SettingsManager::instance()->showStartupEffect())
    {
        StartupEffectWindow startWin;
        MainWindow* mainWin = nullptr;

        // 数据加载完成时创建主窗口
        QObject::connect(&startWin, &StartupEffectWindow::dataLoaded,
            [&](const PlayerInfo& playerInfo, const QList<Item>& items) {
                mainWin = new MainWindow(playerInfo, items);
            });

        // 特效窗口关闭时（无论数据是否加载成功）显示主窗口
        QObject::connect(&startWin, &StartupEffectWindow::finished, [&]() {
            if (!mainWin) {
                // 超时或加载失败，使用默认数据
                PlayerInfo defaultInfo;
                defaultInfo.setDefaults();
                QList<Item> items = initLoad();
                mainWin = new MainWindow(defaultInfo, items);
            }
            mainWin->show();
            });

        // 启动特效窗口（开始加载数据）
        startWin.start();
        return app.exec();
    }
    else
    {
        // 未开启特效：直接加载数据并显示主窗口
        PlayerInfo playerInfo;
        QString filePath = QCoreApplication::applicationDirPath() + "/Data/playerData.json";
        QFile playerFile(filePath);
        if (playerFile.exists() && playerFile.open(QIODevice::ReadOnly)) {
            QByteArray data = playerFile.readAll();
            playerFile.close();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (doc.isObject()) {
                playerInfo.loadFromJson(doc.object());
            }
            else {
                playerInfo.setDefaults();
            }
        }
        else {
            playerInfo.setDefaults();
        }

        QList<Item> items = initLoad();

        MainWindow mainWin(playerInfo, items);
        mainWin.show();
        return app.exec();
    }
}