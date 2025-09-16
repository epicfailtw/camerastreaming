#include "janusconnector.h"

int JanusConnector::s_nextMountpointId = 1;

JanusConnector::JanusConnector(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_webView(new QWebEngineView())
    , m_webChannel(new QWebChannel(this))
    , m_keepAliveTimer(new QTimer(this))
    , m_janusUrl("http://10.10.205.65:8088/janus")
    , m_sessionId(0)
    , m_handleId(0)
    , m_state(Idle)
    , m_currentReply(nullptr)
    , m_mountpointId(s_nextMountpointId++)
{
    // Setup network manager
    m_networkManager->setTransferTimeout(10000);// 10 seconds

    // Setup keep-alive timer
    m_keepAliveTimer->setInterval(30000); // 30 seconds
    connect(m_keepAliveTimer, &QTimer::timeout, this, &JanusConnector::sendKeepAlive);

    setupWebEngineView();
}

JanusConnector::~JanusConnector()
{
    cleanup();
    delete m_webView;
}

void JanusConnector::setJanusUrl(const QString &url)
{
    if (m_state != Idle) {
        qWarning() << "Cannot change Janus URL while connected";
        return;
    }
    m_janusUrl = url;
}

QString JanusConnector::janusUrl() const
{
    return m_janusUrl;
}

void JanusConnector::connectToJanus(const CameraParams &params)
{
    if (!params.isValid()) {
        emit errorOccurred("Invalid camera parameters");
        return;
    }

    if (m_state != Idle) {
        qWarning() << "Already connecting/connected, current state:" << m_state;
        return;
    }

    m_currentParams = params;
    qDebug() << "Connecting to Janus for camera:" << params.cameraUUID;
    qDebug() << "RTSP URL:" << params.rtspUrl;
    qDebug() << "Using mountpoint ID:" << m_mountpointId;

    createJanusSession();
}

void JanusConnector::disconnect()
{
    cleanup();
    m_state = Idle;
    emit connectionStateChanged(false);
}

bool JanusConnector::isConnected() const
{
    return m_state == Ready || m_state == Streaming;
}

qint64 JanusConnector::sessionId() const
{
    return m_sessionId;
}

qint64 JanusConnector::handleId() const
{
    return m_handleId;
}

void JanusConnector::startStreaming()
{
    if (m_state != Ready) {
        qWarning() << "Cannot start streaming, not ready. Current state:" << m_state;
        return;
    }
    startWebRTCStreaming();
}

void JanusConnector::stopStreaming()
{
    if (m_state == Streaming) {
        m_webView->hide();
        m_state = Ready;
        emit streamingStopped();
    }
}

void JanusConnector::createJanusSession()
{
    m_state = CreatingSession;

    QJsonObject sessionRequest;
    sessionRequest["janus"] = "create";
    sessionRequest["transaction"] = "tx-create-session";

    QJsonDocument doc(sessionRequest);
    QNetworkRequest request((QUrl(m_janusUrl)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    m_currentReply = m_networkManager->post(request, doc.toJson());
    connect(m_currentReply, &QNetworkReply::finished,
            this, &JanusConnector::onSessionCreated);
    connect(m_currentReply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::errorOccurred),
            this, &JanusConnector::onNetworkError);
}

void JanusConnector::onSessionCreated()
{
    if (!m_currentReply) return;

    QByteArray response = m_currentReply->readAll();
    m_currentReply->deleteLater();
    m_currentReply = nullptr;

    qDebug() << "Session Create Response:" << response;

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(response, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        emit errorOccurred("Failed to parse session response");
        m_state = Idle;
        return;
    }

    QJsonObject obj = doc.object();
    if (obj["janus"].toString() != "success") {
        emit errorOccurred("Failed to create Janus session");
        m_state = Idle;
        return;
    }

    m_sessionId = obj["data"].toObject()["id"].toVariant().toLongLong();
    qDebug() << "Session ID:" << m_sessionId;

    // Start keep-alive timer
    m_keepAliveTimer->start();

    // Proceed to attach plugin
    attachToStreamingPlugin();
}

void JanusConnector::attachToStreamingPlugin()
{
    m_state = AttachingPlugin;

    QJsonObject attachRequest;
    attachRequest["janus"] = "attach";
    attachRequest["plugin"] = "janus.plugin.streaming";
    attachRequest["transaction"] = "tx-attach-plugin";

    QJsonDocument doc(attachRequest);
    QString url = QString("%1/%2").arg(m_janusUrl).arg(m_sessionId);
    QNetworkRequest request((QUrl(url)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    m_currentReply = m_networkManager->post(request, doc.toJson());
    connect(m_currentReply, &QNetworkReply::finished,
            this, &JanusConnector::onPluginAttached);
    connect(m_currentReply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::errorOccurred),
            this, &JanusConnector::onNetworkError);
}

void JanusConnector::onPluginAttached()
{
    if (!m_currentReply) return;

    QByteArray response = m_currentReply->readAll();
    m_currentReply->deleteLater();
    m_currentReply = nullptr;

    qDebug() << "Plugin Attach Response:" << response;

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(response, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        emit errorOccurred("Failed to parse attach response");
        m_state = Idle;
        return;
    }

    QJsonObject obj = doc.object();
    if (obj["janus"].toString() != "success") {
        emit errorOccurred("Failed to attach to streaming plugin");
        m_state = Idle;
        return;
    }

    m_handleId = obj["data"].toObject()["id"].toVariant().toLongLong();
    qDebug() << "Handle ID:" << m_handleId;

    // Proceed to create mountpoint
    createRTSPMountpoint();
}

void JanusConnector::createRTSPMountpoint()
{
    m_state = CreatingMountpoint;

    QJsonObject body;
    body["request"] = "create";
    body["type"] = "rtsp";
    body["id"] = m_mountpointId;;
    body["name"] = m_currentParams.roomName;
    body["description"] = QString("%1 - %2 Live Stream")
                              .arg(m_currentParams.customerName, m_currentParams.applianceName);
    body["audio"] = true;
    body["video"] = true;
    body["permanent"] = false;
    body["url"] = m_currentParams.rtspUrl;
    body["metadata"] = QString("Camera: %1, Room: %2, School: %3")
                           .arg(m_currentParams.cameraId, m_currentParams.roomName,
                                m_currentParams.applianceName);

    // RTSP parameters
    body["rtsp_user"] = m_currentParams.rtspUser;
    body["rtsp_pwd"] = m_currentParams.rtspPassword;
    body["rtsp_reconnect_delay"] = 5;
    body["rtsp_session_timeout"] = 0;
    body["rtsp_timeout"] = 10;
    body["rtsp_conn_timeout"] = 5;

    QJsonObject mountpointRequest;
    mountpointRequest["janus"] = "message";
    mountpointRequest["transaction"] = "tx-create-mountpoint";
    mountpointRequest["session_id"] = m_sessionId;
    mountpointRequest["handle_id"] = m_handleId;
    mountpointRequest["body"] = body;

    QJsonDocument doc(mountpointRequest);
    QString url = QString("%1/%2/%3").arg(m_janusUrl).arg(m_sessionId).arg(m_handleId);
    QNetworkRequest request((QUrl(url)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    m_currentReply = m_networkManager->post(request, doc.toJson());
    connect(m_currentReply, &QNetworkReply::finished,
            this, &JanusConnector::onMountpointCreated);
    connect(m_currentReply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::errorOccurred),
            this, &JanusConnector::onNetworkError);
}

void JanusConnector::onMountpointCreated()
{
    if (!m_currentReply) return;

    QByteArray response = m_currentReply->readAll();
    m_currentReply->deleteLater();
    m_currentReply = nullptr;

    qDebug() << "Mountpoint Create Response:" << response;

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(response, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        emit errorOccurred("Failed to parse mountpoint response");
        m_state = Idle;
        return;
    }

    QJsonObject obj = doc.object();
    if (obj["janus"].toString() != "success") {
        emit errorOccurred("Failed to create RTSP mountpoint");
        m_state = Idle;
        return;
    }

    m_state = Ready;
    qDebug() << "RTSP mountpoint created successfully";

    emit sessionReady(m_sessionId, m_handleId);
    emit connectionStateChanged(true);

    // Auto-start streaming after a short delay
    //QTimer::singleShot(1000, this, &JanusConnector::startStreaming);
}

void JanusConnector::startWebRTCStreaming()
{
    if (m_state != Ready) {
        qWarning() << "Cannot start WebRTC streaming, not ready";
        return;
    }

    qDebug() << "Starting WebRTC streaming";

    // Load janus.js from resources**
    QFile janusFile(":/scripts/janus.js");
    QString janusJsContent;
    if (janusFile.open(QIODevice::ReadOnly)) {
        janusJsContent = QString::fromUtf8(janusFile.readAll());
        janusFile.close();
    } else {
        qWarning() << "Failed to load janus.js from resources";
        emit errorOccurred("Failed to load janus.js from resources");
        return;
    }

    // Use template loader to generate HTML content**
    QString htmlContent = templateloader::loadStreamTemplate(
        m_currentParams,
        m_janusUrl,
        m_mountpointId,
        janusJsContent
        );

    if (htmlContent.isEmpty()) {
        emit errorOccurred("Failed to load stream template");
        return;
    }

    m_webView->setHtml(htmlContent);
    m_webView->show();

    m_state = Streaming;
    emit streamingStarted();
}

void JanusConnector::setupWebEngineView()
{
    m_webView->resize(1280, 720);
    m_webView->setWindowTitle("Janus WebRTC Stream");

    QWebEngineSettings *settings = m_webView->page()->settings();
    settings->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    settings->setAttribute(QWebEngineSettings::AllowRunningInsecureContent, true);

    m_webView->setContextMenuPolicy(Qt::DefaultContextMenu);

    m_webView->page()->setWebChannel(m_webChannel);
    m_webChannel->registerObject("qtConnector", this);
}

void JanusConnector::sendKeepAlive()
{
    if (m_sessionId > 0) {
        QJsonObject keepAlive;
        keepAlive["janus"] = "keepalive";
        keepAlive["session_id"] = m_sessionId;
        keepAlive["transaction"] = QString("tx-keepalive-%1")
                                       .arg(QDateTime::currentMSecsSinceEpoch());

        QJsonDocument doc(keepAlive);
        QNetworkRequest request(QUrl(QString("%1/%2").arg(m_janusUrl).arg(m_sessionId)));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QNetworkReply *reply = m_networkManager->post(request, doc.toJson());
        connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
    }
}

void JanusConnector::cleanup()
{
    m_keepAliveTimer->stop();

    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }

    if (m_webView) {
        m_webView->hide();
    }

    m_sessionId = 0;
    m_handleId = 0;
}

void JanusConnector::onNetworkError(QNetworkReply::NetworkError error)
{
    qDebug() << "Network error:" << error;

    if (m_currentReply) {
        qDebug() << "Error string:" << m_currentReply->errorString();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }

    emit errorOccurred(QString("Network error: %1").arg(static_cast<int>(error)));
    m_state = Idle;
    emit connectionStateChanged(false);
}
