#include "cameramanager.h"
#include "janusconnector.h"

CameraManager::CameraManager(QObject *parent)
    : QObject(parent)
    , m_httpServer(new HttpServer(this))
    , m_janusUrl("http://10.10.205.65:8088/janus")
    //, m_janusConnector(new JanusConnector(this))
{
    // Connect HTTP server signals
    connect(m_httpServer, &HttpServer::cameraParametersReceived,
            this, &CameraManager::onCameraParametersReceived);
    connect(m_httpServer, &HttpServer::serverError,
            this, &CameraManager::onHttpServerError);

    // Connect Janus connector signals
    // connect(m_janusConnector, &JanusConnector::streamingStarted,
    //         this, &CameraManager::onStreamingStarted);
    // connect(m_janusConnector, &JanusConnector::streamingStopped,
    //         this, &CameraManager::onStreamingStopped);
    // connect(m_janusConnector, &JanusConnector::errorOccurred,
    //         this, &CameraManager::onJanusError);
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

    for (auto it = m_janusConnectors.begin(); it != m_janusConnectors.end(); ++it) {
        it.value()->disconnect();
        it.value()->deleteLater();
    }
    m_janusConnectors.clear();

    emit serviceStopped();
    qDebug() << "Camera streaming service stopped";


    // m_httpServer->stopServer();
    // m_janusConnector->disconnect();

    // emit serviceStopped();
    // qDebug() << "Camera streaming service stopped";
}

void CameraManager::setJanusUrl(const QString &url)
{
    m_janusUrl = url;
    //m_janusConnector->setJanusUrl(url);
}

void CameraManager::onCameraParametersReceived(const CameraParams &params)
{
    qDebug() << "Received camera parameters for UUID:" << params.cameraUUID;

    // Check if connector already exists**
    if (m_janusConnectors.contains(params.cameraUUID)) {
        // Stop existing stream
        m_janusConnectors[params.cameraUUID]->disconnect();
        m_janusConnectors[params.cameraUUID]->deleteLater();
        m_janusConnectors.remove(params.cameraUUID);
    }

    // Create new connector for this camera
    JanusConnector *connector = new JanusConnector(this);
    connector->setJanusUrl(m_janusUrl);

    // Connect signals with camera UUID tracking
    connect(connector, &JanusConnector::streamingStarted,
            this, &CameraManager::onStreamingStarted);
    connect(connector, &JanusConnector::streamingStopped,
            this, &CameraManager::onStreamingStopped);
    connect(connector, &JanusConnector::errorOccurred,
            this, &CameraManager::onJanusError);
    connect(connector, &QObject::destroyed,
            this, &CameraManager::onConnectorDestroyed);

    // Store connector in map
    m_janusConnectors[params.cameraUUID] = connector;

    // Start connection
    connector->connectToJanus(params);

    // m_currentCameraUUID = params.cameraUUID;

    // // Stop current streaming if any
    // if (m_janusConnector->isConnected()) {
    //     m_janusConnector->disconnect();
    // }

    // // Start new connection
    // m_janusConnector->connectToJanus(params);
}

void CameraManager::onStreamingStarted()
{

    // Find which connector sent the signal
    JanusConnector *connector = qobject_cast<JanusConnector*>(sender());
    if (!connector) return;

    // Find the camera UUID for this connector
    for (auto it = m_janusConnectors.begin(); it != m_janusConnectors.end(); ++it) {
        if (it.value() == connector) {
            qDebug() << "Streaming started for camera:" << it.key();
            emit streamingStarted(it.key());
            break;
        }
    }
    // qDebug() << "Streaming started for camera:" << m_currentCameraUUID;
    // emit streamingStarted(m_currentCameraUUID);
}

void CameraManager::onStreamingStopped()
{

    // Find which connector sent the signal**
    JanusConnector *connector = qobject_cast<JanusConnector*>(sender());
    if (!connector) return;

    // Find the camera UUID for this connector
    for (auto it = m_janusConnectors.begin(); it != m_janusConnectors.end(); ++it) {
        if (it.value() == connector) {
            qDebug() << "Streaming stopped for camera:" << it.key();
            emit streamingStopped(it.key());
            break;
        }
    }

    // qDebug() << "Streaming stopped for camera:" << m_currentCameraUUID;
    // emit streamingStopped(m_currentCameraUUID);
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

// Cleanup when connector is destroyed
void CameraManager::onConnectorDestroyed()
{
    // Remove destroyed connector from map
    for (auto it = m_janusConnectors.begin(); it != m_janusConnectors.end(); ++it) {
        if (it.value() == sender()) {
            m_janusConnectors.erase(it);
            break;
        }
    }
}
