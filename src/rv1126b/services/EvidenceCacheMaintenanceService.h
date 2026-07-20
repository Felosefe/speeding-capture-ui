#pragma once

#include "../../models/SystemSettings.h"

#include <QObject>
#include <QString>

#include <functional>

namespace rv1126b {

struct EvidenceCacheCleanupPolicy {
    int retentionDays = 30;
    qint64 maxCacheBytes = 0;
    qint64 minFreeSpaceBytes = 0;
    bool deleteOldestWhenLowSpace = true;

    static EvidenceCacheCleanupPolicy fromMaintenanceSettings(const MaintenanceSettings& settings);
};

struct EvidenceCacheCleanupResult {
    int scannedFiles = 0;
    int deletedFiles = 0;
    qint64 scannedBytes = 0;
    qint64 deletedBytes = 0;
};

class EvidenceCacheMaintenanceService final : public QObject
{
    Q_OBJECT

public:
    explicit EvidenceCacheMaintenanceService(QString cacheRootPath, QObject* parent = nullptr);
    EvidenceCacheMaintenanceService(
        QString cacheRootPath,
        std::function<qint64()> availableBytesProvider,
        QObject* parent = nullptr);

    QString cacheRootPath() const;
    EvidenceCacheCleanupResult cleanup(const EvidenceCacheCleanupPolicy& policy) const;

private:
    struct CacheFile {
        QString path;
        qint64 size = 0;
        qint64 lastModifiedEpochMs = 0;
    };

    QVector<CacheFile> collectCacheFiles(EvidenceCacheCleanupResult* result) const;
    bool removeCacheFile(const CacheFile& file, EvidenceCacheCleanupResult* result) const;
    qint64 availableBytes() const;

    QString cacheRootPath_;
    std::function<qint64()> availableBytesProvider_;
};

} // namespace rv1126b
