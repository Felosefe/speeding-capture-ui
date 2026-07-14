#pragma once

#include <QDateTime>
#include <QString>

enum class DeviceConnectionState {
    Offline,
    Online,
    Fault
};

struct DeviceStatus {
    DeviceConnectionState connectionState = DeviceConnectionState::Offline;
    QString runningState = QStringLiteral("未连接");
    QString firmwareVersion = QStringLiteral("v1.0.0");
    QDateTime lastHeartbeat;
    int captureCount = 0;
};

inline QString connectionStateText(DeviceConnectionState state)
{
    switch (state) {
    case DeviceConnectionState::Online:
        return QStringLiteral("在线");
    case DeviceConnectionState::Fault:
        return QStringLiteral("故障");
    case DeviceConnectionState::Offline:
    default:
        return QStringLiteral("离线");
    }
}
