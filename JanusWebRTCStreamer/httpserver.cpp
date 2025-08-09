#include "httpserver.h"

HttpServer::HttpServer(QObject *parent)
    : QObject(parent)
    , m_tcpServer(new QTcpServer(this))
{
    connect(m_tcpServer, &QTcpServer::newConnection,
            this, &HttpServer::handleNewConnection);
}

HttpServer::~HttpServer()
{
    stopServer();
}

bool HttpServer::startServer(quint16 port)
{
    if (m_tcpServer->isListening()) {
        qWarning() << "Server is already listening";
        return true;
    }

    if (!m_tcpServer->listen(QHostAddress::Any, port)) {
        QString errorMsg = QString("Failed to start HTTP server on port %1: %2")
        .arg(port).arg(m_tcpServer->errorString());
        qDebug() << errorMsg;
        emit serverError(errorMsg);
        return false;
    }

    qDebug() << "HTTP server started on port:" << m_tcpServer->serverPort();
    return true;
}

void HttpServer::stopServer()
{
    if (m_tcpServer->isListening()) {
        m_tcpServer->close();
        qDebug() << "HTTP server stopped";
    }
}

bool HttpServer::isListening() const
{
    return m_tcpServer->isListening();
}

quint16 HttpServer::serverPort() const
{
    return m_tcpServer->serverPort();
}

void HttpServer::handleNewConnection()
{
    while (m_tcpServer->hasPendingConnections()) {
        QTcpSocket *socket = m_tcpServer->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead,
                this, &HttpServer::handleClientRequest);
        connect(socket, &QTcpSocket::disconnected,
                socket, &QTcpSocket::deleteLater);
    }
}

void HttpServer::handleClientRequest()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    QByteArray requestData = socket->readAll();
    QString request = QString::fromUtf8(requestData);

    qDebug() << "Received HTTP request:" << request.left(200) << "...";

    // Parse HTTP request
    QStringList lines = request.split("\r\n");
    if (lines.isEmpty()) {
        socket->disconnectFromHost();
        return;
    }

    QString requestLine = lines[0];
    QStringList requestParts = requestLine.split(" ");

    if (requestParts.size() < 3) {
        sendHttpResponse(socket, 400, "Bad Request",
                         "{\"error\":\"Invalid request format\"}");
        return;
    }

    QString method = requestParts[0];
    QString path = requestParts[1];

    // Handle POST /camera/{uuid}
    if (method == "POST" && path.startsWith("/camera/")) {
        QString uuid = path.mid(8); // Remove "/camera/"

        if (uuid.isEmpty()) {
            sendHttpResponse(socket, 400, "Bad Request",
                             "{\"error\":\"Missing camera UUID\"}");
            return;
        }

        CameraParams params = parsePostRequest(request, uuid);

        if (!params.isValid()) {
            sendHttpResponse(socket, 400, "Bad Request",
                             "{\"error\":\"Invalid camera parameters\"}");
            return;
        }

        // Emit signal with parsed parameters
        emit cameraParametersReceived(params);

        // Send success response
        QJsonObject response;
        response["status"] = "success";
        response["message"] = "Camera streaming request received";
        response["camera_uuid"] = uuid;

        sendHttpResponse(socket, 200, "OK", QJsonDocument(response).toJson());
    } else {
        sendHttpResponse(socket, 404, "Not Found",
                         "{\"error\":\"Endpoint not found\"}");
    }
}

CameraParams HttpServer::parsePostRequest(const QString &request, const QString &uuid)
{
    CameraParams params;

    // Find JSON body
    int bodyStart = request.indexOf("\r\n\r\n");
    if (bodyStart == -1) {
        qWarning() << "No body found in request";
        return params;
    }

    QString jsonBody = request.mid(bodyStart + 4);

    // Parse JSON
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonBody.toUtf8(), &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "JSON parse error:" << parseError.errorString();
        return params;
    }

    QJsonObject jsonObj = doc.object();

    // Extract parameters
    params.cameraUUID = uuid;
    params.customerName = jsonObj["customer_name"].toString();
    params.applianceName = jsonObj["appliance_name"].toString();
    params.cameraId = jsonObj["camera_id"].toString();
    params.roomName = jsonObj["room_name"].toString();
    params.ipPort = jsonObj["ip_port"].toString();

    // Optional RTSP credentials
    if (jsonObj.contains("rtsp_user")) {
        params.rtspUser = jsonObj["rtsp_user"].toString();
    }
    if (jsonObj.contains("rtsp_password")) {
        params.rtspPassword = jsonObj["rtsp_password"].toString();
    }

    // Construct RTSP URL
    if (!params.ipPort.isEmpty()) {

        params.rtspUrl = QString("rtsp://%1/main").arg(params.ipPort);
        // QStringList parts = params.ipPort.split(":");
        // if (parts.size() == 2) {
        //     QString ip = parts[0];
        //     QString port = parts[1];
        //     params.rtspUrl = QString("rtsp://%1:%2@%3:%4/main")
        //                          .arg(params.rtspUser, params.rtspPassword, ip, port);
        // }
    }

    return params;
}

void HttpServer::sendHttpResponse(QTcpSocket *socket, int statusCode,
                                  const QString &statusText, const QByteArray &body)
{
    QString response = QString(
                           "HTTP/1.1 %1 %2\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: %3\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           ).arg(statusCode).arg(statusText).arg(body.size());

    socket->write(response.toUtf8());
    socket->write(body);
    socket->disconnectFromHost();
}
