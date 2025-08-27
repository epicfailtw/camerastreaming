#include "templateloader.h"
#include <QFile>
#include <QDebug>

QString templateloader::loadStreamTemplate(const CameraParams &params,
                                           const QString &janusUrl,
                                           int mountpointId,
                                           const QString &janusJsContent)
{
    // Load HTML template
    QString htmlTemplate = loadTemplate(":/templates/streaming.html");
    if (htmlTemplate.isEmpty()) {
        qWarning() << "Failed to load HTML template";
        return QString();
    }

    // Load JavaScript template
    QString jsTemplate = loadTemplate(":/scripts/web-rtc.js");
    if (jsTemplate.isEmpty()) {
        qWarning() << "Failed to load JavaScript template";
        return QString();
    }

    // Prepare variables for replacement
    QMap<QString, QString> variables;
    variables["ROOM_NAME"] = params.roomName;
    variables["CUSTOMER_NAME"] = params.customerName;
    variables["APPLIANCE_NAME"] = params.applianceName;
    variables["JANUS_URL"] = janusUrl;
    variables["MOUNTPOINT_ID"] = QString::number(mountpointId);
    variables["JANUS_JS_CONTENT"] = janusJsContent;

    // Process JavaScript template first
    QString processedJs = processTemplate(jsTemplate, variables);
    variables["WEBRTC_SCRIPT"] = processedJs;

    // Process HTML template
    return processTemplate(htmlTemplate, variables);
}

QString templateloader::loadTemplate(const QString &templatePath)
{
    QFile file(templatePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Cannot open template file:" << templatePath;
        return QString();
    }

    return QString::fromUtf8(file.readAll());
}

QString templateloader::processTemplate(const QString &templateContent,
                                        const QMap<QString, QString> &variables)
{
    QString result = templateContent;

    // Replace all variables in format {{VARIABLE_NAME}}
    for (auto it = variables.begin(); it != variables.end(); ++it) {
        QString placeholder = QString("{{%1}}").arg(it.key());
        result.replace(placeholder, it.value());
    }

    return result;
}
