#ifndef CAMERAPARAMS_H
#define CAMERAPARAMS_H

#include <QString>


struct CameraParams {
    QString cameraUUID;
    QString customerName;    // district
    QString applianceName;   // school
    QString cameraId;
    QString roomName;        // camera name
    QString ipPort;
    QString rtspUrl;         // constructed from IP:PORT
    QString rtspUser = "admin";
    QString rtspPassword = "Kloud123";

    bool isValid() const {
        return !cameraUUID.isEmpty() && !ipPort.isEmpty();
    }
};

#endif // CAMERAPARAMS_H
