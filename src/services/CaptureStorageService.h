#pragma once

#include "../models/CaptureRecord.h"
#include "../models/SystemSettings.h"

#include <QString>

enum class CaptureAssetKind {
    NormalImage,
    OverspeedImage,
    WatchedVehicleImage,
    ViolationVideo,
    PlateCloseup,
    TestDeviceAsset,
    RegularVideo
};

struct CaptureStorageResult {
    bool ok = false;
    QString primaryPath;
    QString textInfoPath;
    QString errorMessage;
};

class CaptureStorageService final
{
public:
    explicit CaptureStorageService(const StorageSettings& settings = SystemSettings::defaults().storage);

    void setSettings(const StorageSettings& settings);
    StorageSettings settings() const;
    CaptureStorageResult saveCaptureAssets(const CaptureRecord& record, CaptureAssetKind kind = CaptureAssetKind::NormalImage);
    QString buildAssetPath(const CaptureRecord& record, CaptureAssetKind kind, int index) const;
    int deleteOldestFiles(int maxFilesToDelete, qint64 minBytesToFree);
    int deleteFilesOlderThan(int maxAgeDays, const QDateTime& now = QDateTime::currentDateTime());

private:
    QString templateForKind(CaptureAssetKind kind) const;
    QString captureTypeToken(CaptureType type) const;
    QString sanitized(QString value) const;
    int allocateIndex();
    bool writeTextInfo(const QString& assetPath, const CaptureRecord& record, QString* textInfoPath, QString* errorMessage) const;

    StorageSettings settings_;
    int nextIndex_ = 1;
};
