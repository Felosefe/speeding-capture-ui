#include "EvidenceCacheMaintenanceService.h"

#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>

#include <algorithm>

namespace rv1126b {
namespace {

constexpr qint64 BytesPerGb = 1024LL * 1024LL * 1024LL;
constexpr qint64 MsPerDay = 24LL * 60LL * 60LL * 1000LL;

bool isCacheImage(const QFileInfo& info)
{
    return info.isFile()
        && !info.fileName().endsWith(QStringLiteral(".part"), Qt::CaseInsensitive)
        && info.suffix().compare(QStringLiteral("jpg"), Qt::CaseInsensitive) == 0;
}

} // namespace

EvidenceCacheCleanupPolicy EvidenceCacheCleanupPolicy::fromMaintenanceSettings(
    const MaintenanceSettings& settings)
{
    EvidenceCacheCleanupPolicy policy;
    policy.retentionDays = settings.expireCaptureDays;
    policy.minFreeSpaceBytes = static_cast<qint64>(settings.minFreeSpaceGb) * BytesPerGb;
    policy.deleteOldestWhenLowSpace = settings.deleteOldestWhenLowSpace;
    return policy;
}

EvidenceCacheMaintenanceService::EvidenceCacheMaintenanceService(
    QString cacheRootPath,
    QObject* parent)
    : QObject(parent)
    , cacheRootPath_(std::move(cacheRootPath))
{
}

EvidenceCacheMaintenanceService::EvidenceCacheMaintenanceService(
    QString cacheRootPath,
    std::function<qint64()> availableBytesProvider,
    QObject* parent)
    : QObject(parent)
    , cacheRootPath_(std::move(cacheRootPath))
    , availableBytesProvider_(std::move(availableBytesProvider))
{
}

QString EvidenceCacheMaintenanceService::cacheRootPath() const
{
    return cacheRootPath_;
}

void EvidenceCacheMaintenanceService::setCacheRootPath(const QString& cacheRootPath)
{
    cacheRootPath_ = cacheRootPath;
}

EvidenceCacheCleanupResult EvidenceCacheMaintenanceService::cleanup(
    const EvidenceCacheCleanupPolicy& policy) const
{
    EvidenceCacheCleanupResult result;
    QVector<CacheFile> files = collectCacheFiles(&result);
    std::sort(files.begin(), files.end(), [](const CacheFile& left, const CacheFile& right) {
        return left.lastModifiedEpochMs < right.lastModifiedEpochMs;
    });

    const qint64 expireBefore = QDateTime::currentMSecsSinceEpoch()
        - static_cast<qint64>(std::max(1, policy.retentionDays)) * MsPerDay;
    qint64 remainingBytes = result.scannedBytes;

    for (const CacheFile& file : files) {
        if (file.lastModifiedEpochMs >= expireBefore) {
            continue;
        }
        if (removeCacheFile(file, &result)) {
            remainingBytes -= file.size;
        }
    }

    if (policy.maxCacheBytes > 0 && remainingBytes > policy.maxCacheBytes) {
        for (const CacheFile& file : files) {
            if (remainingBytes <= policy.maxCacheBytes) {
                break;
            }
            if (!QFileInfo::exists(file.path)) {
                continue;
            }
            if (removeCacheFile(file, &result)) {
                remainingBytes -= file.size;
            }
        }
    }

    if (policy.deleteOldestWhenLowSpace && policy.minFreeSpaceBytes > 0) {
        for (const CacheFile& file : files) {
            if (availableBytes() >= policy.minFreeSpaceBytes) {
                break;
            }
            if (!QFileInfo::exists(file.path)) {
                continue;
            }
            removeCacheFile(file, &result);
        }
    }

    return result;
}

QVector<EvidenceCacheMaintenanceService::CacheFile>
EvidenceCacheMaintenanceService::collectCacheFiles(EvidenceCacheCleanupResult* result) const
{
    QVector<CacheFile> files;
    if (!QFileInfo::exists(cacheRootPath_)) {
        return files;
    }

    QDirIterator iterator(
        cacheRootPath_,
        QDir::Files | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        const QFileInfo info = iterator.fileInfo();
        if (!isCacheImage(info)) {
            continue;
        }

        CacheFile file;
        file.path = info.absoluteFilePath();
        file.size = info.size();
        file.lastModifiedEpochMs = info.lastModified().toMSecsSinceEpoch();
        files.append(file);

        if (result) {
            ++result->scannedFiles;
            result->scannedBytes += file.size;
        }
    }

    return files;
}

bool EvidenceCacheMaintenanceService::removeCacheFile(
    const CacheFile& file,
    EvidenceCacheCleanupResult* result) const
{
    const qint64 size = QFileInfo(file.path).size();
    if (!QFile::remove(file.path)) {
        return false;
    }

    if (result) {
        ++result->deletedFiles;
        result->deletedBytes += size;
    }
    return true;
}

qint64 EvidenceCacheMaintenanceService::availableBytes() const
{
    if (availableBytesProvider_) {
        return availableBytesProvider_();
    }

    QStorageInfo storage(cacheRootPath_);
    if (!storage.isValid()) {
        storage = QStorageInfo(QFileInfo(cacheRootPath_).absolutePath());
    }
    return storage.bytesAvailable();
}

} // namespace rv1126b
