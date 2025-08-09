#include "cameramanager.h"
#include "janusconnector.h"

CameraManager::CameraManager(QObject *parent)
    : QObject(parent)
    , m_httpServer(new HttpServer(this))
    , m_janusConnector(new JanusConnector(this))
{
    // Connect HTTP server signals
    connect(m_httpServer, &HttpServer::cameraParametersReceived,
            this, &CameraManager::onCameraParametersReceived);
    connect(m_httpServer, &HttpServer::serverError,
            this, &CameraManager::onHttpServerError);

    // Connect Janus connector signals
    connect(m_janusConnector, &JanusConnector::streamingStarted,
            this, &CameraManager::onStreamingStarted);
    connect(m_janusConnector, &JanusConnector::streamingStopped,
            this, &CameraManager::onStreamingStopped);
    connect(m_janusConnector, &JanusConnector::errorOccurred,
            this, &CameraManager::onJanusError);
}

CameraManager::~CameraManager()
{
    stopService();
}

bool CameraManager::startService(quint16 httpPort)
{
    if (!m_httpServer->startServer(httpPort)) {
        return false;
    }

    qDebug() << "Camera streaming service started on port:" << httpPort;
    qDebug() << "Send POST requests to: http://localhost:" << httpPort << "/camera/{uuid}";

    emit serviceStarted();
    return true;
}

void CameraManager::stopService()
{
    m_httpServer->stopServer();
    m_janusConnector->disconnect();

    emit serviceStopped();
    qDebug() << "Camera streaming service stopped";
}

void CameraManager::setJanusUrl(const QString &url)
{
    m_janusConnector->setJanusUrl(url);
}

void CameraManager::onCameraParametersReceived(const CameraParams &params)
{
    qDebug() << "Received camera parameters for UUID:" << params.cameraUUID;

    m_currentCameraUUID = params.cameraUUID;

    // Stop current streaming if any
    if (m_janusConnector->isConnected()) {
        m_janusConnector->disconnect();
    }

    // Start new connection
    m_janusConnector->connectToJanus(params);
}

void CameraManager::onStreamingStarted()
{
    qDebug() << "Streaming started for camera:" << m_currentCameraUUID;
    emit streamingStarted(m_currentCameraUUID);
}

void CameraManager::onStreamingStopped()
{
    qDebug() << "Streaming stopped for camera:" << m_currentCameraUUID;
    emit streamingStopped(m_currentCameraUUID);
}

void CameraManager::onJanusError(const QString &error)
{
    qWarning() << "Janus error:" << error;
    emit errorOccurred(QString("Janus error: %1").arg(error));
}

void CameraManager::onHttpServerError(const QString &error)
{
    qWarning() << "HTTP server error:" << error;
    emit errorOccurred(QString("HTTP server error: %1").arg(error));
}
