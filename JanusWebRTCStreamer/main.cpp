#include <QApplication>
#include <QDebug>
#include <QLoggingCategory>
#include "mainwindow.h"
#include "cameramanager.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    QLoggingCategory::setFilterRules(
        "qt.qpa.*=false\n"           // Disable QPA (platform) logs
        "qt.text.*=false\n"          // Disable text/font logs
        "qt.gui.*=false\n"           // Disable GUI logs
        "qt.widgets.*=false\n"       // Disable widget logs
        "qt.network.*=false\n"       // Disable network logs (optional)
        "qt.webengine.*=false\n"     // Disable WebEngine logs (optional)
        "default=true"               // Keep your app's debug output
        );

    // Show main window
    MainWindow w;
    w.show();

    // Create and configure camera manager
    CameraManager cameraManager;

    // Optional: Set custom Janus URL
    // cameraManager.setJanusUrl("http://your-janus-server:8088/janus");

    // Connect signals for monitoring
    QObject::connect(&cameraManager, &CameraManager::serviceStarted, []() {
        qDebug() << "Camera streaming service started successfully!";
    });

    QObject::connect(&cameraManager, &CameraManager::streamingStarted,
                     [](const QString &cameraUUID) {
                         qDebug() << "Streaming started for camera:" << cameraUUID;
                     });

    QObject::connect(&cameraManager, &CameraManager::streamingStopped,
                     [](const QString &cameraUUID) {
                         qDebug() << "Streaming stopped for camera:" << cameraUUID;
                     });

    QObject::connect(&cameraManager, &CameraManager::errorOccurred,
                     [](const QString &error) {
                         qWarning() << "Camera Manager Error:" << error;
                     });

    // Start the service
    if (!cameraManager.startService(8080)) {
        qCritical() << "Failed to start camera streaming service!";
        return -1;
    }

    return a.exec();
}
