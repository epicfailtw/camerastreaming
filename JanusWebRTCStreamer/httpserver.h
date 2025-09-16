#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <QDebug>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include "cameraparams.h"

class HttpServer : public QObject
{
    Q_OBJECT

public:
    explicit HttpServer(QObject *parent = nullptr);
    ~HttpServer();

    bool startServer(quint16 port = 8080);
    void stopServer();
    bool isListening() const;
    quint16 serverPort() const;

    void registerStream(const QString &cameraUUID, const CameraParams &params, int mountpointId, const QString &janusUrl);
    void unregisterStream(const QString &cameraUUID);

    void setCredentials(const QString &username, const QString &password);
    bool isValidCredentials(const QString &username, const QString &password) const;
    QMap<QString, QString> parseHttpHeaders(const QString &request);

signals:
    void cameraParametersReceived(const CameraParams &params);
    void serverError(const QString &error);

private slots:
    void handleNewConnection();
    void handleClientRequest();

private:
    void sendHttpResponse(QTcpSocket *socket,
                          int statusCode,
                          const QString &statusText,
                          const QByteArray &body);

    void sendHtmlResponse(QTcpSocket *socket, const QString &htmlContent);
    CameraParams parsePostRequest(const QString &request);
    void handleGetRequest(QTcpSocket *socket, const QString &path, const QMap<QString, QString> &headers);

    bool checkBasicAuth(const QString &authHeader) const;
    void sendAuthRequired(QTcpSocket *socket);

    QTcpServer *m_tcpServer;

    struct StreamInfo {
        CameraParams params;
        int mountpointId;
        QString janusUrl;
    };
    QMap<QString, StreamInfo> m_activeStreams;
    QString m_username;
    QString m_password;
    bool m_authEnabled;
};

#endif // HTTPSERVER_H
