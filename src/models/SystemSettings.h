#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QString>
#include <QStringList>
#include <QTime>

struct UiSettings {
    bool startMaximized = false;
    bool autoListenDeviceData = true;
    bool autoConnectOnStart = false;
    QString fontFamily = QStringLiteral("Microsoft YaHei");
    int fontPointSize = 10;
    int captureListMaxRows = 1000;
    QStringList captureListFields = {
        QStringLiteral("time"),
        QStringLiteral("plate"),
        QStringLiteral("plateColor"),
        QStringLiteral("eventType"),
        QStringLiteral("deviceId"),
        QStringLiteral("direction"),
        QStringLiteral("coordinate"),
        QStringLiteral("remark"),
    };
    int previewFrameRate = 12;
    bool showOnlyVehicleFrames = false;
    bool overlaySpeed = true;
    bool showCalibrationLines = true;
    QString plateImagePosition = QStringLiteral("right");
    bool multiVideoSplit = true;
    bool stopVideoQueryWhenMinimized = true;
    bool showSuccessPopup = false;
};

struct StorageSettings {
    QString rootPath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("captures"));
    QString normalImageTemplate = QStringLiteral("{yyyyMMdd}/{deviceId}_{plate}_{type}_{index}.jpg");
    QString overspeedImageTemplate = QStringLiteral("{yyyyMMdd}/overspeed/{deviceId}_{plate}_{index}.jpg");
    QString watchedVehicleImageTemplate = QStringLiteral("{yyyyMMdd}/watched/{deviceId}_{plate}_{index}.jpg");
    QString violationVideoTemplate = QStringLiteral("{yyyyMMdd}/video/{deviceId}_{plate}_{index}.mp4");
    QString plateCloseupTemplate = QStringLiteral("{yyyyMMdd}/plate/{deviceId}_{plate}_{index}.jpg");
    QString testDeviceAssetTemplate = QStringLiteral("{yyyyMMdd}/test/{deviceId}_{index}.dat");
    QString regularVideoTemplate = QStringLiteral("{yyyyMMdd}/regular/{deviceId}_{index}.mp4");
    int indexDigits = 6;
    bool syncTextInfoFile = true;
    bool savePlateCloseup = true;
    bool mergePanoramaAndPlate = false;
    bool autoRecord = false;
    bool vehiclePassRecord = false;
    int maxVideoSegmentMb = 512;
};

struct MaintenanceSettings {
    bool startWithSystem = false;
    bool enableDailyDeviceTimeSync = false;
    QTime dailySyncTime = QTime(3, 0);
    bool enableScheduledShutdown = false;
    QTime shutdownTime = QTime(23, 30);
    bool enableDiskMaintenance = true;
    int expireCaptureDays = 30;
    int expireVideoDays = 30;
    int minFreeSpaceGb = 5;
    bool deleteOldestWhenLowSpace = true;
};

struct SystemSettings {
    UiSettings ui;
    StorageSettings storage;
    MaintenanceSettings maintenance;

    static SystemSettings defaults()
    {
        return {};
    }
};
