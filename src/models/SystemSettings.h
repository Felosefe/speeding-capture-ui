#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QString>
#include <QStringList>
#include <QStandardPaths>
#include <QTime>

struct UiSettings {
    bool startMaximized = false;
    bool autoListenDeviceData = true;
    bool autoConnectOnStart = false;
    QString lastSelectedVideoDeviceId;
    // 实时预览默认码流："main" = 主码流 /live/0（出厂默认），"sub" = 辅码流 /live/1。
    // 用户在预览页切换码流后会被记住，下次启动继续用同一路。
    QString rtspStreamRole = QStringLiteral("main");
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
    QString rootPath = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                           .filePath(QStringLiteral("captures"));
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

struct MaintenanceSettings {    bool startWithSystem = false;
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

// 「检测结果自动保存到文件夹」设置（用户第 4 项需求）。
// 目标文件夹独立于证据缓存目录：缓存目录是软件内部用的，这个是给用户直接翻的。
struct DetectionSyncSettings {
    bool enabled = true;
    // 空表示"跟着存储根目录走"（<存储根目录>/检测结果）。
    // 注意必须留空：SystemSettingsService::normalized() 在读完 storage.rootPath 之后才
    // 把空值解析成具体路径。若在这里就填上默认路径，用户改了存储根目录后不会跟着走。
    QString folder;
    bool includeSnapshot = false;
};

struct SystemSettings {
    UiSettings ui;
    StorageSettings storage;
    DetectionSyncSettings detectionSync;
    MaintenanceSettings maintenance;

    static SystemSettings defaults()
    {
        return {};
    }

    // 默认目录：存储根目录下的「检测结果」子目录。
    static QString defaultDetectionFolder(const QString& storageRootPath)
    {
        const QString root = storageRootPath.trimmed().isEmpty()
                                 ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                                       .filePath(QStringLiteral("captures"))
                                 : storageRootPath;
        return QDir(root).filePath(QStringLiteral("检测结果"));
    }

    QString detectionFolder() const
    {
        return detectionSync.folder.trimmed().isEmpty()
                   ? defaultDetectionFolder(storage.rootPath)
                   : detectionSync.folder;
    }
};
