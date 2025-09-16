#ifndef TEMPLATELOADER_H
#define TEMPLATELOADER_H

#include <QString>
#include <QMap>
#include "cameraparams.h"

class templateloader
{
public:
    static QString loadStreamTemplate(const CameraParams &params,
                                      const QString &janusUrl,
                                      int mountpointId,
                                      const QString &janusJsContent);
    static QString loadSimpleStreamTemplate(const CameraParams &params,
                                            const QString &janusUrl,
                                            int mountpointId,
                                            const QString &janusJsContent);

private:
    static QString loadTemplate(const QString &templatePath);
    static QString processTemplate(const QString &templateContent,
                                   const QMap<QString, QString> &variables);

};

#endif
