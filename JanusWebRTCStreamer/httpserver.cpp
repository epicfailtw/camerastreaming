#include "httpserver.h"
#include "janusconnector.h"

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

    //qDebug() << "HTTP server started on port:" << m_tcpServer->serverPort();
    qDebug() << "Stream URLs: http://localhost:" << port << "/stream/{uuid}";
    return true;
}

void HttpServer::stopServer()
{
    if (m_tcpServer->isListening()) {
        m_tcpServer->close();
        qDebug() << "HTTP server stopped";
    }

    m_activeStreams.clear();
}

bool HttpServer::isListening() const
{
    return m_tcpServer->isListening();
}

quint16 HttpServer::serverPort() const
{
    return m_tcpServer->serverPort();
}


void HttpServer::registerStream(const QString &cameraUUID, const CameraParams &params, int mountpointId, const QString &janusUrl)
{
    StreamInfo info;
    info.params = params;
    info.mountpointId = mountpointId;
    info.janusUrl = janusUrl;

    m_activeStreams[cameraUUID] = info;
    qDebug() << "Stream registered:" << cameraUUID << "-> mountpoint" << mountpointId;
}

void HttpServer::unregisterStream(const QString &cameraUUID)
{
    if (m_activeStreams.remove(cameraUUID)) {
        qDebug() << "Stream unregistered:" << cameraUUID;
    }
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

    QMap<QString, QString> headers = parseHttpHeaders(request);

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

    if (method == "GET") {
        handleGetRequest(socket, path, headers);
        return;
    }

    // Existing POST handling logic
    if (method != "POST") {
        sendHttpResponse(socket, 405, "Method Not Allowed", "Only POST and GET methods are supported");
        return;
    }

    if (!path.startsWith("/camera/")) {
        sendHttpResponse(socket, 404, "Not Found", "Endpoint not found");
        return;
    }

    QString uuid = path.mid(8); // Remove "/camera/"
    if (uuid.isEmpty()) {
        sendHttpResponse(socket, 400, "Bad Request", "UUID required");
        return;
    }

    CameraParams params = parsePostRequest(request);
    if (params.cameraUUID.isEmpty()) {
        sendHttpResponse(socket, 400, "Bad Request", "Invalid JSON or missing required fields");
        return;
    }

    emit cameraParametersReceived(params);
    sendHttpResponse(socket, 200, "OK", "Camera parameters received successfully");
}


void HttpServer::handleGetRequest(QTcpSocket *socket, const QString &path, const QMap<QString, QString> &headers)
{
    if (path.startsWith("/stream/")) {
        QString cameraUUID = path.mid(8); // Remove "/stream/"
        if (cameraUUID.isEmpty()) {
            sendHttpResponse(socket, 400, "Bad Request", "Camera UUID required");
            return;
        }

        if (!m_activeStreams.contains(cameraUUID)) {
            sendHttpResponse(socket, 404, "Not Found", "Stream not found or not active");
            return;
        }

        if (m_authEnabled) {
            QString authHeader = headers.value("authorization", "");
            if (!checkBasicAuth(authHeader)) {
                sendAuthRequired(socket);
                return;
            }
        }

        const StreamInfo &streamInfo = m_activeStreams[cameraUUID];

        qDebug() << "Loading stream template for camera:" << cameraUUID;

        // Load Janus.js from resources
        QFile janusFile(":/scripts/janus.js");
        QString janusJsContent;
        if (janusFile.open(QIODevice::ReadOnly)) {
            janusJsContent = QString::fromUtf8(janusFile.readAll());
            janusFile.close();
        } else {
            qWarning() << "Failed to load janus.js from resources";
            sendHttpResponse(socket, 500, "Internal Server Error", "Janus script not found");
            return;
        }

        // Use TemplateLoader to generate HTML content
        QString htmlContent = templateloader::loadSimpleStreamTemplate(
            streamInfo.params,
            streamInfo.janusUrl,
            streamInfo.mountpointId,
            janusJsContent
            );


        if (htmlContent.isEmpty()) {
            qWarning() << "Failed to load stream template";
            sendHttpResponse(socket, 500, "Internal Server Error", "Template loading failed");
            return;
        }

        qDebug() << "Stream template loaded successfully for camera:" << cameraUUID;
        sendHtmlResponse(socket, htmlContent);
    } else {
        sendHttpResponse(socket, 404, "Not Found", "Page not found");
    }
}

QMap<QString, QString> HttpServer::parseHttpHeaders(const QString &request)
{
    QMap<QString, QString> headers;
    QStringList lines = request.split("\r\n");

    // Skip the first line (request line)
    for (int i = 1; i < lines.size(); i++) {
        QString line = lines[i].trimmed();
        if (line.isEmpty()) break; // End of headers

        int colonPos = line.indexOf(':');
        if (colonPos > 0) {
            QString key = line.left(colonPos).trimmed().toLower();
            QString value = line.mid(colonPos + 1).trimmed();
            headers[key] = value;
        }
    }

    return headers;
}

CameraParams HttpServer::parsePostRequest(const QString &request)
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
    params.cameraUUID = jsonObj["camera_id"].toString();
    params.customerName = jsonObj["customer_name"].toString();
    params.applianceName = jsonObj["appliance_name"].toString();
    params.cameraId = jsonObj["camera_id"].toString();
    params.roomName = jsonObj["room_name"].toString();
    params.ip = jsonObj["ip"].toString();

    // Optional RTSP credentials
    if (jsonObj.contains("rtsp_user")) {
        params.rtspUser = jsonObj["rtsp_user"].toString();
    }
    if (jsonObj.contains("rtsp_password")) {
        params.rtspPassword = jsonObj["rtsp_password"].toString();
    }

    // Construct RTSP URL
    if (!params.ip.isEmpty()) {
        params.rtspUrl = QString("rtsp://%1/main").arg(params.ip);
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

void HttpServer::sendHtmlResponse(QTcpSocket *socket, const QString &htmlContent)
{
    QByteArray body = htmlContent.toUtf8();

    QString response = QString(
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/html; charset=utf-8\r\n"
                           "Content-Length: %1\r\n"
                           "Connection: close\r\n"
                           "\r\n")
                           .arg(body.size());

    socket->write(response.toUtf8());
    socket->write(body);
    socket->flush(); // make sure it’s sent immediately
    socket->disconnectFromHost();
}

void HttpServer::setCredentials(const QString &username, const QString &password)
{
    m_username = username;
    m_password = password;
    m_authEnabled = !username.isEmpty() && !password.isEmpty();
    qDebug() << "Basic auth" << (m_authEnabled ? "enabled" : "disabled");
}

bool HttpServer::isValidCredentials(const QString &username, const QString &password) const
{
    return (username == m_username && password == m_password);
}

bool HttpServer::checkBasicAuth(const QString &authHeader) const
{
    if (!m_authEnabled) return true;

    if (!authHeader.startsWith("Basic ")) return false;

    QString encoded = authHeader.mid(6); // Remove "Basic "
    QByteArray decoded = QByteArray::fromBase64(encoded.toUtf8());
    QString credentials = QString::fromUtf8(decoded);

    QStringList parts = credentials.split(":");
    if (parts.size() != 2) return false;

    return isValidCredentials(parts[0], parts[1]);
}

void HttpServer::sendAuthRequired(QTcpSocket *socket)
{
    QString response = "HTTP/1.1 401 Unauthorized\r\n";
    response += "WWW-Authenticate: Basic realm=\"Stream Access\"\r\n";
    response += "Content-Type: text/html; charset=utf-8\r\n";
    response += "Content-Length: 47\r\n";
    response += "Connection: close\r\n\r\n";
    response += "<html><body><h1>401 Unauthorized</h1></body></html>";

    socket->write(response.toUtf8());
    socket->flush();
    socket->disconnectFromHost();
}
