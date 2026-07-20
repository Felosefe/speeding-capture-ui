#pragma once

#include <QDateTime>
#include <QString>

enum class DeviceConnectionState {
    Offline,
    Connecting,
    Online,
    Degraded,
    AuthenticationFailed,
    Disconnecting,
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
    case DeviceConnectionState::Connecting:
        return QStringLiteral("连接中");
    case DeviceConnectionState::Online:
        return QStringLiteral("在线");
    case DeviceConnectionState::Degraded:
        return QStringLiteral("退化");
    case DeviceConnectionState::AuthenticationFailed:
        return QStringLiteral("认证失败");
    case DeviceConnectionState::Disconnecting:
        return QStringLiteral("断开中");
    case DeviceConnectionState::Fault:
        return QStringLiteral("故障");
    case DeviceConnectionState::Offline:
    default:
        return QStringLiteral("离线");
    }
}
