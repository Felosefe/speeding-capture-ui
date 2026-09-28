#pragma once

#include "DeviceConfig.h"
#include "DeviceStatus.h"

#include <QString>
#include <QStringList>
#include <QDateTime>

struct Device {
    QString id;
    QString name;
    QString ipAddress;
    int port = 8000;
    QString apiUrl;
    QStringList capabilities;
    QDateTime lastOnline;
    QString lastError;
    DeviceConfig config;
    DeviceStatus status;
};
