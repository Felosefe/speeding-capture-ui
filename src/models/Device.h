#pragma once

#include "DeviceConfig.h"
#include "DeviceStatus.h"

#include <QString>

struct Device {
    QString id;
    QString name;
    QString ipAddress;
    int port = 8000;
    DeviceConfig config;
    DeviceStatus status;
};
