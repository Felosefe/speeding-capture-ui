#pragma once

#include <QString>

struct DeviceConfig {
    // 点位信息在真机模式下不在本机配置，而是从板端读取（设备配置 › 展示配置）。
    // 这里保持空值，避免"没读到板端值"被误显示成一条看起来真实的假配置。
    QString location;
    QString direction;
    QString laneName;
    int speedLimitKmh = 60;
    bool savePlateImage = true;
    bool enableOverspeedAlert = true;

    bool ntpEnabled = true;
    QString cameraModel = QStringLiteral("LPR-Radar-X1");
    QString streamResolution = QStringLiteral("1920x1080");
    int previewFrameRate = 25;

    QString adminUser = QStringLiteral("admin");
    bool remoteAuthEnabled = true;
    QString ipWhitelist = QStringLiteral("192.168.1.0/24");

    QString radarModel = QStringLiteral("RS-24G");
    double speedCalibration = 1.0;
    int lowSpeedFilterKmh = 10;
    bool flashEnabled = true;

    int recognitionMarginLeft = 120;
    int recognitionMarginTop = 160;
    int recognitionMarginRight = 120;
    int recognitionMarginBottom = 160;
    bool saveUnknownPlate = true;
    bool enableBlacklist = true;
    QString overspeedViolationName = QStringLiteral("超速行驶");
    QString overspeedViolationCode = QStringLiteral("1303");

    int preRecordSeconds = 3;
    int postRecordSeconds = 5;
    int jpegQuality = 90;
    bool rtspEnabled = true;
    bool uploadEnabled = false;
    QString uploadServer = QStringLiteral("192.168.1.200");
    int uploadPort = 9000;

    // 点位信息（地点/通道/方向/限速）是否完全未设置。真机模式下这些值由板端提供，
    // 本机结构体保持空值，界面据此显示"未下发"而不是假配置。
    bool siteInfoUnset() const
    {
        return location.trimmed().isEmpty()
            && laneName.trimmed().isEmpty()
            && direction.trimmed().isEmpty();
    }
};
