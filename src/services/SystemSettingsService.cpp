#include "SystemSettingsService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>

SystemSettingsService::SystemSettingsService(QObject* parent)
    : SystemSettingsService(defaultSettingsPath(), parent)
{
#ifdef CAMERA_MANAGER_SOURCE_DIR
    const QString legacyPath = QDir(QString::fromUtf8(CAMERA_MANAGER_SOURCE_DIR))
                                   .filePath(QStringLiteral("data/config/system.ini"));
    if (!QFileInfo::exists(settingsPath_) && QFileInfo::exists(legacyPath)) {
        QDir().mkpath(QFileInfo(settingsPath_).absolutePath());
        QFile::copy(legacyPath, settingsPath_);
    }
#endif
}

SystemSettingsService::SystemSettingsService(const QString& settingsPath, QObject* parent)
    : QObject(parent)
    , settingsPath_(settingsPath)
    , settings_(SystemSettings::defaults())
{
}

SystemSettings SystemSettingsService::load()
{
    SystemSettings loaded = SystemSettings::defaults();
    QSettings store(settingsPath_, QSettings::IniFormat);

    readUiSettings(store, loaded.ui);
    readStorageSettings(store, loaded.storage);
    readMaintenanceSettings(store, loaded.maintenance);

    settings_ = normalized(loaded);
    return settings_;
}

bool SystemSettingsService::save(const SystemSettings& settings)
{
    settings_ = normalized(settings);
    QDir().mkpath(QFileInfo(settingsPath_).absolutePath());

    QSettings store(settingsPath_, QSettings::IniFormat);
    writeUiSettings(store, settings_.ui);
    writeStorageSettings(store, settings_.storage);
    writeMaintenanceSettings(store, settings_.maintenance);
    store.sync();

    if (store.status() != QSettings::NoError) {
        setLastError(QStringLiteral("Failed to write system settings"));
        return false;
    }

    emit settingsChanged(settings_);
    return true;
}

SystemSettings SystemSettingsService::settings() const
{
    return settings_;
}

QString SystemSettingsService::lastError() const
{
    return lastError_;
}

QString SystemSettingsService::settingsPath() const
{
    return settingsPath_;
}

QString SystemSettingsService::defaultSettingsPath() const
{
    QDir root(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    return root.filePath(QStringLiteral("data/config/system.ini"));
}

SystemSettings SystemSettingsService::normalized(SystemSettings settings) const
{
    settings.ui.fontPointSize = qBound(6, settings.ui.fontPointSize, 48);
    settings.ui.captureListMaxRows = qBound(1, settings.ui.captureListMaxRows, 100000);
    settings.ui.previewFrameRate = qBound(1, settings.ui.previewFrameRate, 60);

    if (settings.ui.captureListFields.isEmpty()) {
        settings.ui.captureListFields = SystemSettings::defaults().ui.captureListFields;
    }
    if (settings.ui.plateImagePosition != QStringLiteral("right")
        && settings.ui.plateImagePosition != QStringLiteral("bottom")
        && settings.ui.plateImagePosition != QStringLiteral("hidden")) {
        settings.ui.plateImagePosition = QStringLiteral("right");
    }

    settings.storage.indexDigits = qBound(2, settings.storage.indexDigits, 12);
    settings.storage.maxVideoSegmentMb = qBound(1, settings.storage.maxVideoSegmentMb, 8192);
    if (settings.storage.rootPath.trimmed().isEmpty()) {
        settings.storage.rootPath = SystemSettings::defaults().storage.rootPath;
    }

    settings.maintenance.expireCaptureDays = qBound(1, settings.maintenance.expireCaptureDays, 3650);
    settings.maintenance.expireVideoDays = qBound(1, settings.maintenance.expireVideoDays, 3650);
    settings.maintenance.minFreeSpaceGb = qBound(1, settings.maintenance.minFreeSpaceGb, 1024);

    return settings;
}

void SystemSettingsService::readUiSettings(QSettings& store, UiSettings& settings) const
{
    store.beginGroup(QStringLiteral("ui"));
    settings.startMaximized = store.value(QStringLiteral("startMaximized"), settings.startMaximized).toBool();
    settings.autoListenDeviceData = store.value(QStringLiteral("autoListenDeviceData"), settings.autoListenDeviceData).toBool();
    settings.autoConnectOnStart = store.value(QStringLiteral("autoConnectOnStart"), settings.autoConnectOnStart).toBool();
    settings.lastSelectedVideoDeviceId = store.value(QStringLiteral("lastSelectedVideoDeviceId"),
                                                      settings.lastSelectedVideoDeviceId).toString();
    settings.fontFamily = store.value(QStringLiteral("fontFamily"), settings.fontFamily).toString();
    settings.fontPointSize = store.value(QStringLiteral("fontPointSize"), settings.fontPointSize).toInt();
    settings.captureListMaxRows = store.value(QStringLiteral("captureListMaxRows"), settings.captureListMaxRows).toInt();
    settings.captureListFields = store.value(QStringLiteral("captureListFields"), settings.captureListFields).toStringList();
    settings.previewFrameRate = store.value(QStringLiteral("previewFrameRate"), settings.previewFrameRate).toInt();
    settings.showOnlyVehicleFrames = store.value(QStringLiteral("showOnlyVehicleFrames"), settings.showOnlyVehicleFrames).toBool();
    settings.overlaySpeed = store.value(QStringLiteral("overlaySpeed"), settings.overlaySpeed).toBool();
    settings.showCalibrationLines = store.value(QStringLiteral("showCalibrationLines"), settings.showCalibrationLines).toBool();
    settings.plateImagePosition = store.value(QStringLiteral("plateImagePosition"), settings.plateImagePosition).toString();
    settings.multiVideoSplit = store.value(QStringLiteral("multiVideoSplit"), settings.multiVideoSplit).toBool();
    settings.stopVideoQueryWhenMinimized = store.value(QStringLiteral("stopVideoQueryWhenMinimized"), settings.stopVideoQueryWhenMinimized).toBool();
    settings.showSuccessPopup = store.value(QStringLiteral("showSuccessPopup"), settings.showSuccessPopup).toBool();
    store.endGroup();
}

void SystemSettingsService::readStorageSettings(QSettings& store, StorageSettings& settings) const
{
    store.beginGroup(QStringLiteral("storage"));
    settings.rootPath = store.value(QStringLiteral("rootPath"), settings.rootPath).toString();
    settings.normalImageTemplate = store.value(QStringLiteral("normalImageTemplate"), settings.normalImageTemplate).toString();
    settings.overspeedImageTemplate = store.value(QStringLiteral("overspeedImageTemplate"), settings.overspeedImageTemplate).toString();
    settings.watchedVehicleImageTemplate = store.value(QStringLiteral("watchedVehicleImageTemplate"), settings.watchedVehicleImageTemplate).toString();
    settings.violationVideoTemplate = store.value(QStringLiteral("violationVideoTemplate"), settings.violationVideoTemplate).toString();
    settings.plateCloseupTemplate = store.value(QStringLiteral("plateCloseupTemplate"), settings.plateCloseupTemplate).toString();
    settings.testDeviceAssetTemplate = store.value(QStringLiteral("testDeviceAssetTemplate"), settings.testDeviceAssetTemplate).toString();
    settings.regularVideoTemplate = store.value(QStringLiteral("regularVideoTemplate"), settings.regularVideoTemplate).toString();
    settings.indexDigits = store.value(QStringLiteral("indexDigits"), settings.indexDigits).toInt();
    settings.syncTextInfoFile = store.value(QStringLiteral("syncTextInfoFile"), settings.syncTextInfoFile).toBool();
    settings.savePlateCloseup = store.value(QStringLiteral("savePlateCloseup"), settings.savePlateCloseup).toBool();
    settings.mergePanoramaAndPlate = store.value(QStringLiteral("mergePanoramaAndPlate"), settings.mergePanoramaAndPlate).toBool();
    settings.autoRecord = store.value(QStringLiteral("autoRecord"), settings.autoRecord).toBool();
    settings.vehiclePassRecord = store.value(QStringLiteral("vehiclePassRecord"), settings.vehiclePassRecord).toBool();
    settings.maxVideoSegmentMb = store.value(QStringLiteral("maxVideoSegmentMb"), settings.maxVideoSegmentMb).toInt();
    store.endGroup();
}

void SystemSettingsService::readMaintenanceSettings(QSettings& store, MaintenanceSettings& settings) const
{
    store.beginGroup(QStringLiteral("maintenance"));
    settings.startWithSystem = store.value(QStringLiteral("startWithSystem"), settings.startWithSystem).toBool();
    settings.enableDailyDeviceTimeSync = store.value(QStringLiteral("enableDailyDeviceTimeSync"), settings.enableDailyDeviceTimeSync).toBool();
    settings.dailySyncTime = store.value(QStringLiteral("dailySyncTime"), settings.dailySyncTime).toTime();
    settings.enableScheduledShutdown = store.value(QStringLiteral("enableScheduledShutdown"), settings.enableScheduledShutdown).toBool();
    settings.shutdownTime = store.value(QStringLiteral("shutdownTime"), settings.shutdownTime).toTime();
    settings.enableDiskMaintenance = store.value(QStringLiteral("enableDiskMaintenance"), settings.enableDiskMaintenance).toBool();
    settings.expireCaptureDays = store.value(QStringLiteral("expireCaptureDays"), settings.expireCaptureDays).toInt();
    settings.expireVideoDays = store.value(QStringLiteral("expireVideoDays"), settings.expireVideoDays).toInt();
    settings.minFreeSpaceGb = store.value(QStringLiteral("minFreeSpaceGb"), settings.minFreeSpaceGb).toInt();
    settings.deleteOldestWhenLowSpace = store.value(QStringLiteral("deleteOldestWhenLowSpace"), settings.deleteOldestWhenLowSpace).toBool();
    store.endGroup();
}

void SystemSettingsService::writeUiSettings(QSettings& store, const UiSettings& settings) const
{
    store.beginGroup(QStringLiteral("ui"));
    store.setValue(QStringLiteral("startMaximized"), settings.startMaximized);
    store.setValue(QStringLiteral("autoListenDeviceData"), settings.autoListenDeviceData);
    store.setValue(QStringLiteral("autoConnectOnStart"), settings.autoConnectOnStart);
    store.setValue(QStringLiteral("lastSelectedVideoDeviceId"), settings.lastSelectedVideoDeviceId);
    store.setValue(QStringLiteral("fontFamily"), settings.fontFamily);
    store.setValue(QStringLiteral("fontPointSize"), settings.fontPointSize);
    store.setValue(QStringLiteral("captureListMaxRows"), settings.captureListMaxRows);
    store.setValue(QStringLiteral("captureListFields"), settings.captureListFields);
    store.setValue(QStringLiteral("previewFrameRate"), settings.previewFrameRate);
    store.setValue(QStringLiteral("showOnlyVehicleFrames"), settings.showOnlyVehicleFrames);
    store.setValue(QStringLiteral("overlaySpeed"), settings.overlaySpeed);
    store.setValue(QStringLiteral("showCalibrationLines"), settings.showCalibrationLines);
    store.setValue(QStringLiteral("plateImagePosition"), settings.plateImagePosition);
    store.setValue(QStringLiteral("multiVideoSplit"), settings.multiVideoSplit);
    store.setValue(QStringLiteral("stopVideoQueryWhenMinimized"), settings.stopVideoQueryWhenMinimized);
    store.setValue(QStringLiteral("showSuccessPopup"), settings.showSuccessPopup);
    store.endGroup();
}

void SystemSettingsService::writeStorageSettings(QSettings& store, const StorageSettings& settings) const
{
    store.beginGroup(QStringLiteral("storage"));
    store.setValue(QStringLiteral("rootPath"), settings.rootPath);
    store.setValue(QStringLiteral("normalImageTemplate"), settings.normalImageTemplate);
    store.setValue(QStringLiteral("overspeedImageTemplate"), settings.overspeedImageTemplate);
    store.setValue(QStringLiteral("watchedVehicleImageTemplate"), settings.watchedVehicleImageTemplate);
    store.setValue(QStringLiteral("violationVideoTemplate"), settings.violationVideoTemplate);
    store.setValue(QStringLiteral("plateCloseupTemplate"), settings.plateCloseupTemplate);
    store.setValue(QStringLiteral("testDeviceAssetTemplate"), settings.testDeviceAssetTemplate);
    store.setValue(QStringLiteral("regularVideoTemplate"), settings.regularVideoTemplate);
    store.setValue(QStringLiteral("indexDigits"), settings.indexDigits);
    store.setValue(QStringLiteral("syncTextInfoFile"), settings.syncTextInfoFile);
    store.setValue(QStringLiteral("savePlateCloseup"), settings.savePlateCloseup);
    store.setValue(QStringLiteral("mergePanoramaAndPlate"), settings.mergePanoramaAndPlate);
    store.setValue(QStringLiteral("autoRecord"), settings.autoRecord);
    store.setValue(QStringLiteral("vehiclePassRecord"), settings.vehiclePassRecord);
    store.setValue(QStringLiteral("maxVideoSegmentMb"), settings.maxVideoSegmentMb);
    store.endGroup();
}

void SystemSettingsService::writeMaintenanceSettings(QSettings& store, const MaintenanceSettings& settings) const
{
    store.beginGroup(QStringLiteral("maintenance"));
    store.setValue(QStringLiteral("startWithSystem"), settings.startWithSystem);
    store.setValue(QStringLiteral("enableDailyDeviceTimeSync"), settings.enableDailyDeviceTimeSync);
    store.setValue(QStringLiteral("dailySyncTime"), settings.dailySyncTime);
    store.setValue(QStringLiteral("enableScheduledShutdown"), settings.enableScheduledShutdown);
    store.setValue(QStringLiteral("shutdownTime"), settings.shutdownTime);
    store.setValue(QStringLiteral("enableDiskMaintenance"), settings.enableDiskMaintenance);
    store.setValue(QStringLiteral("expireCaptureDays"), settings.expireCaptureDays);
    store.setValue(QStringLiteral("expireVideoDays"), settings.expireVideoDays);
    store.setValue(QStringLiteral("minFreeSpaceGb"), settings.minFreeSpaceGb);
    store.setValue(QStringLiteral("deleteOldestWhenLowSpace"), settings.deleteOldestWhenLowSpace);
    store.endGroup();
}

void SystemSettingsService::setLastError(const QString& message) const
{
    lastError_ = message;
}
