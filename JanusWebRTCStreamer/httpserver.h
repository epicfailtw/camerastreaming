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
    CameraParams parsePostRequest(const QString &request, const QString &uuid);

    QTcpServer *m_tcpServer;
};

#endif // HTTPSERVER_H
