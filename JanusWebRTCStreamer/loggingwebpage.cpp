#include <QWebEnginePage>
#include <QDebug>

class LoggingWebPage : public QWebEnginePage {
    Q_OBJECT
public:
    explicit LoggingWebPage(QObject *parent = nullptr)
        : QWebEnginePage(parent) {}

protected:
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level,
                                  const QString &message,
                                  int lineNumber,
                                  const QString &sourceID) override {
        QString levelStr;
        switch (level) {
        case InfoMessageLevel:    levelStr = "INFO"; break;
        case WarningMessageLevel: levelStr = "WARN"; break;
        case ErrorMessageLevel:   levelStr = "ERROR"; break;
        }
        qDebug().noquote() << "[JS" << levelStr << "]"
                           << message
                           << "(Line:" << lineNumber
                           << ", Source:" << sourceID << ")";
    }
};
