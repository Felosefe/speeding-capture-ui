#include "CaptureStorageService.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <algorithm>

CaptureStorageService::CaptureStorageService(const StorageSettings& settings)
    : settings_(settings)
{
}

void CaptureStorageService::setSettings(const StorageSettings& settings)
{
    settings_ = settings;
}

StorageSettings CaptureStorageService::settings() const
{
    return settings_;
}

CaptureStorageResult CaptureStorageService::saveCaptureAssets(const CaptureRecord& record, CaptureAssetKind kind)
{
    CaptureStorageResult result;
    const int index = allocateIndex();
    result.primaryPath = buildAssetPath(record, kind, index);

    QDir().mkpath(QFileInfo(result.primaryPath).absolutePath());
    QFile file(result.primaryPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        result.errorMessage = file.errorString();
        return result;
    }
    file.close();

    if (settings_.syncTextInfoFile
        && !writeTextInfo(result.primaryPath, record, &result.textInfoPath, &result.errorMessage)) {
        return result;
    }

    result.ok = true;
    return result;
}

QString CaptureStorageService::buildAssetPath(const CaptureRecord& record, CaptureAssetKind kind, int index) const
{
    QString relative = templateForKind(kind);
    const QDateTime timestamp = record.timestamp.isValid() ? record.timestamp : QDateTime::currentDateTime();
    relative.replace(QStringLiteral("{yyyyMMdd}"), timestamp.toString(QStringLiteral("yyyyMMdd")));
    relative.replace(QStringLiteral("{HHmmss}"), timestamp.toString(QStringLiteral("HHmmss")));
    relative.replace(QStringLiteral("{deviceId}"), sanitized(record.deviceId));
    relative.replace(QStringLiteral("{plate}"), sanitized(record.plateNumber));
    relative.replace(QStringLiteral("{type}"), captureTypeToken(record.type));
    relative.replace(QStringLiteral("{index}"), QString::number(index).rightJustified(settings_.indexDigits, QLatin1Char('0')));
    return QDir(settings_.rootPath).filePath(relative);
}

int CaptureStorageService::deleteOldestFiles(int maxFilesToDelete, qint64 minBytesToFree)
{
    struct Candidate {
        QString path;
        QDateTime modified;
        qint64 size = 0;
    };

    QVector<Candidate> candidates;
    QDirIterator it(settings_.rootPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info(path);
        candidates.append({path, info.lastModified(), info.size()});
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
        return left.modified < right.modified;
    });

    int removed = 0;
    qint64 freed = 0;
    for (const Candidate& candidate : candidates) {
        if (maxFilesToDelete > 0 && removed >= maxFilesToDelete) {
            break;
        }
        if (minBytesToFree > 0 && freed >= minBytesToFree) {
            break;
        }
        if (QFile::remove(candidate.path)) {
            ++removed;
            freed += candidate.size;
        }
    }
    return removed;
}

int CaptureStorageService::deleteFilesOlderThan(int maxAgeDays, const QDateTime& now)
{
    if (maxAgeDays <= 0) {
        return 0;
    }

    const QDateTime threshold = now.addDays(-maxAgeDays);
    int removed = 0;
    QDirIterator it(settings_.rootPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info(path);
        if (info.lastModified() < threshold && QFile::remove(path)) {
            ++removed;
        }
    }
    return removed;
}

QString CaptureStorageService::templateForKind(CaptureAssetKind kind) const
{
    switch (kind) {
    case CaptureAssetKind::OverspeedImage:
        return settings_.overspeedImageTemplate;
    case CaptureAssetKind::WatchedVehicleImage:
        return settings_.watchedVehicleImageTemplate;
    case CaptureAssetKind::ViolationVideo:
        return settings_.violationVideoTemplate;
    case CaptureAssetKind::PlateCloseup:
        return settings_.plateCloseupTemplate;
    case CaptureAssetKind::TestDeviceAsset:
        return settings_.testDeviceAssetTemplate;
    case CaptureAssetKind::RegularVideo:
        return settings_.regularVideoTemplate;
    case CaptureAssetKind::NormalImage:
    default:
        return settings_.normalImageTemplate;
    }
}

QString CaptureStorageService::captureTypeToken(CaptureType type) const
{
    switch (type) {
    case CaptureType::Overspeed:
        return QStringLiteral("overspeed");
    case CaptureType::UnknownPlate:
        return QStringLiteral("unknown");
    case CaptureType::Blacklist:
        return QStringLiteral("watched");
    case CaptureType::Normal:
    default:
        return QStringLiteral("normal");
    }
}

QString CaptureStorageService::sanitized(QString value) const
{
    if (value.trimmed().isEmpty()) {
        value = QStringLiteral("unknown");
    }
    static const QString invalid = QStringLiteral("\\/:*?\"<>|");
    for (const QChar ch : invalid) {
        value.replace(ch, QLatin1Char('_'));
    }
    return value;
}

int CaptureStorageService::allocateIndex()
{
    return nextIndex_++;
}

bool CaptureStorageService::writeTextInfo(
    const QString& assetPath,
    const CaptureRecord& record,
    QString* textInfoPath,
    QString* errorMessage) const
{
    const QString path = QFileInfo(assetPath).absolutePath()
        + QLatin1Char('/')
        + QFileInfo(assetPath).completeBaseName()
        + QStringLiteral(".txt");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << QStringLiteral("id=") << record.id << Qt::endl;
    stream << QStringLiteral("time=") << record.timestamp.toString(Qt::ISODateWithMs) << Qt::endl;
    stream << QStringLiteral("deviceId=") << record.deviceId << Qt::endl;
    stream << QStringLiteral("plate=") << record.plateNumber << Qt::endl;
    stream << QStringLiteral("speedKmh=") << record.speedKmh << Qt::endl;
    stream << QStringLiteral("speedLimitKmh=") << record.speedLimitKmh << Qt::endl;
    stream.flush();

    if (textInfoPath) {
        *textInfoPath = path;
    }
    return true;
}
