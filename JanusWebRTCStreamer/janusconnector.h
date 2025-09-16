#ifndef JANUSCONNECTOR_H
#define JANUSCONNECTOR_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QWebEngineView>
#include <QWebChannel>
#include <QDebug>
#include <QDateTime>
#include <QFile>
#include "cameraparams.h"
#include <QWebEngineSettings>
#include <QWebEnginePage>
#include <QDir>
#include <QThread>
#include "templateloader.h"

class JanusConnector : public QObject
{
    Q_OBJECT

public:
    explicit JanusConnector(QObject *parent = nullptr);
    ~JanusConnector();

    // Set Janus server URL
    void setJanusUrl(const QString &url);
    QString janusUrl() const;

    // Connect to Janus with camera parameters
    void connectToJanus(const CameraParams &params);

    // Disconnect from current session
    void disconnect();

    // Current state
    bool isConnected() const;
    qint64 sessionId() const;
    qint64 handleId() const;

    int mountpointId() const { return m_mountpointId; }
    CameraParams currentParams() const { return m_currentParams; }

public slots:
    void startStreaming();
    void stopStreaming();

signals:
    void sessionReady(qint64 sessionId, qint64 handleId);
    void streamingStarted();
    void streamingStopped();
    void errorOccurred(const QString &error);
    void connectionStateChanged(bool connected);

private slots:
    void onSessionCreated();
    void onPluginAttached();
    void onMountpointCreated();
    void onNetworkError(QNetworkReply::NetworkError error);
    void sendKeepAlive();
private:
    enum State {
        Idle,
        CreatingSession,
        AttachingPlugin,
        CreatingMountpoint,
        Ready,
        Streaming
    };

    void createJanusSession();
    void attachToStreamingPlugin();
    void createRTSPMountpoint();
    void setupWebEngineView();
    void startWebRTCStreaming();
    void cleanup();

    // Network and UI components
    QNetworkAccessManager *m_networkManager;
    QWebEngineView *m_webView;
    QWebChannel *m_webChannel;
    QTimer *m_keepAliveTimer;

    // Janus connection state
    QString m_janusUrl;
    qint64 m_sessionId;
    qint64 m_handleId;
    State m_state;

    // Current camera parameters
    CameraParams m_currentParams;

    // Request tracking
    QNetworkReply *m_currentReply;

    // ADDED for unique mountpoint IDs
    static int s_nextMountpointId;
    int m_mountpointId;
};

#endif // JANUSCONNECTOR_H
