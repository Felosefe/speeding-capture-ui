#pragma once

#include <QString>

struct DeviceConfig {
    QString location = QStringLiteral("测试路口");
    QString direction = QStringLiteral("由北向南");
    QString laneName = QStringLiteral("一车道");
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
};
