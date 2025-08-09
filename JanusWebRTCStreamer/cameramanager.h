#ifndef CAMERAMANAGER_H
#define CAMERAMANAGER_H


#include <QObject>
#include <QDebug>
#include "httpserver.h"
#include "janusconnector.h"
#include "cameraparams.h"

class CameraManager : public QObject
{
    Q_OBJECT

public:
    explicit CameraManager(QObject *parent = nullptr);
    ~CameraManager();

    bool startService(quint16 httpPort = 8080);
    void stopService();

    // Configuration
    void setJanusUrl(const QString &url);

signals:
    void serviceStarted();
    void serviceStopped();
    void streamingStarted(const QString &cameraUUID);
    void streamingStopped(const QString &cameraUUID);
    void errorOccurred(const QString &error);

private slots:
    void onCameraParametersReceived(const CameraParams &params);
    void onStreamingStarted();
    void onStreamingStopped();
    void onJanusError(const QString &error);
    void onHttpServerError(const QString &error);

private:
    HttpServer *m_httpServer;
    JanusConnector *m_janusConnector;
    QString m_currentCameraUUID;
};

#endif // CAMERAMANAGER_H
