#pragma once

#include <QDateTime>
#include <QString>

enum class CaptureType {
    Normal,
    Overspeed,
    UnknownPlate,
    Blacklist
};

enum class CapturePlateState {
    Valid,
    Unknown
};

struct CaptureRecord {
    QString id;
    QDateTime timestamp;
    QString deviceId;
    QString deviceName;
    QString plateNumber;
    QString plateColor;
    QString eventType = QStringLiteral("抓拍");
    int speedKmh = 0;
    int speedLimitKmh = 0;
    QString direction;
    QString coordinateText;
    QString remark;
    CapturePlateState plateState = CapturePlateState::Valid;
    CaptureType type = CaptureType::Normal;
    QString filePath;
};

inline QString captureTypeText(CaptureType type)
{
    switch (type) {
    case CaptureType::Overspeed:
        return QStringLiteral("超速");
    case CaptureType::UnknownPlate:
        return QStringLiteral("未知车牌");
    case CaptureType::Blacklist:
        return QStringLiteral("黑名单");
    case CaptureType::Normal:
    default:
        return QStringLiteral("正常");
    }
}
