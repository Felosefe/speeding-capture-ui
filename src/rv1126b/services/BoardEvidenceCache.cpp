#include "BoardEvidenceCache.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <QTimer>
#include <QStandardPaths>

#include <algorithm>
#include <array>

namespace rv1126b {
namespace {

qint64 nowEpochMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

QString defaultCacheRootPath()
{
    QDir root(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    return root.filePath(QStringLiteral("data/evidence-cache"));
}

constexpr std::array<int, 6> RetryBackoffSeconds {1, 2, 4, 8, 16, 30};
bool hasIdentity(const VehicleEvent& event, const EventIdentity& identity)
{
    return event.identity == identity;
}

} // namespace

BoardEvidenceCache::BoardEvidenceCache(
    IBoardApiClient* apiClient,
    IEventRepository* repository,
    QObject* parent)
    : BoardEvidenceCache(apiClient, repository, defaultCacheRootPath(), parent)
{
}

BoardEvidenceCache::BoardEvidenceCache(
    IBoardApiClient* apiClient,
    IEventRepository* repository,
    QString cacheRootPath,
    QObject* parent)
    : BoardEvidenceCache(
          [apiClient](const QString&) { return apiClient; },
          repository,
          std::move(cacheRootPath),
          parent)
{
}

BoardEvidenceCache::BoardEvidenceCache(
    ApiClientResolver apiClientResolver,
    IEventRepository* repository,
    QString cacheRootPath,
    QObject* parent)
    : EvidenceCache(parent)
    , apiClientResolver_(std::move(apiClientResolver))
    , repository_(repository)
    , cacheRootPath_(std::move(cacheRootPath))
{
}

void BoardEvidenceCache::enqueue(const VehicleEvent& event)
{
    if (event.identity.deviceId.isEmpty()) {
        return;
    }

    if (!event.evidenceAvailable || event.evidenceRelativeUrl.isEmpty()) {
        persistState(entryFor(event, EvidenceCacheStatus::Missing));
        return;
    }

    const QString finalPath = finalPathFor(event);
    if (QFileInfo::exists(finalPath)) {
        EvidenceCacheEntry entry = entryFor(event, EvidenceCacheStatus::Available);
        entry.localFilePath = finalPath;
        entry.contentLength = QFileInfo(finalPath).size();
        persistState(entry);
        return;
    }

    if (queuedIdentities_.contains(event.identity) || active_.contains(event.identity)) {
        return;
    }

    queue_.append(event);
    queuedIdentities_.insert(event.identity);
    persistState(entryFor(event, EvidenceCacheStatus::Queued));
    drainQueue();
}

RequestId BoardEvidenceCache::removeLocal(
    const VehicleEvent& event,
    QObject* context,
    ApiCompletion<void> completion)
{
    Q_UNUSED(context)
    const RequestId requestId = RequestId::createUuid();
    cancel(event.identity);

    const QString finalPath = finalPathFor(event);
    if (QFileInfo::exists(finalPath) && !QFile::remove(finalPath)) {
        if (completion) {
            completion(ApiResult<void>::failure(makeCacheError(
                QStringLiteral("cache_remove_failed"),
                QStringLiteral("Failed to remove cached evidence file: %1").arg(finalPath))));
        }
        return requestId;
    }

    if (completion) {
        completion(ApiResult<void>::success());
    }
    return requestId;
}

void BoardEvidenceCache::cancel(const EventIdentity& identity)
{
    cancelRetry(identity);

    const auto queuedEnd = std::remove_if(queue_.begin(), queue_.end(), [&identity](const VehicleEvent& event) {
        return hasIdentity(event, identity);
    });
    if (queuedEnd != queue_.end()) {
        queue_.erase(queuedEnd, queue_.end());
        queuedIdentities_.remove(identity);
    }

    const auto activeIt = active_.find(identity);
    if (activeIt == active_.end()) {
        return;
    }

    if (activeIt->apiClient) {
        activeIt->apiClient->cancel(activeIt->requestId);
    }
    QFile::remove(activeIt->partFilePath);
    finishActive(identity);
    drainQueue();
}

void BoardEvidenceCache::cancelDevice(const QString& deviceId)
{
    cancelRetriesForDevice(deviceId);

    const auto queuedEnd = std::remove_if(queue_.begin(), queue_.end(), [&deviceId, this](const VehicleEvent& event) {
        const bool matches = event.identity.deviceId == deviceId;
        if (matches) {
            queuedIdentities_.remove(event.identity);
        }
        return matches;
    });
    queue_.erase(queuedEnd, queue_.end());

    QVector<EventIdentity> activeIds;
    for (auto it = active_.cbegin(); it != active_.cend(); ++it) {
        if (it->event.identity.deviceId == deviceId) {
            activeIds.append(it.key());
        }
    }

    for (const EventIdentity& identity : activeIds) {
        cancel(identity);
    }
}

void BoardEvidenceCache::cancelAll()
{
    queue_.clear();
    queuedIdentities_.clear();
    scheduledRetries_.clear();
    retryAttempts_.clear();

    const QVector<ActiveDownload> activeDownloads = active_.values();
    for (const ActiveDownload& active : activeDownloads) {
        if (active.apiClient) {
            active.apiClient->cancel(active.requestId);
        }
        QFile::remove(active.partFilePath);
    }
    active_.clear();
    activePerDevice_.clear();
}

QString BoardEvidenceCache::finalPathFor(const VehicleEvent& event) const
{
    QDir root(cacheRootPath_);
    return root.filePath(QStringLiteral("%1/%2.jpg")
        .arg(sanitizePathSegment(event.identity.deviceId), eventFileStem(event)));
}

bool BoardEvidenceCache::setCacheRootPath(const QString& cacheRootPath)
{
    const QString normalized = QDir::cleanPath(cacheRootPath.trimmed());
    if (normalized.isEmpty() || !QDir().mkpath(normalized)) return false;
    cacheRootPath_ = normalized;
    return true;
}

void BoardEvidenceCache::drainQueue()
{
    bool started = true;
    while (started && active_.size() < MaxConcurrentDownloads) {
        started = false;
        for (int index = 0; index < queue_.size(); ++index) {
            const VehicleEvent event = queue_.at(index);
            if (!canStart(event)) {
                continue;
            }

            queue_.removeAt(index);
            queuedIdentities_.remove(event.identity);
            startDownload(event);
            started = true;
            break;
        }
    }
}

bool BoardEvidenceCache::canStart(const VehicleEvent& event) const
{
    return activePerDevice_.value(event.identity.deviceId, 0) < MaxConcurrentDownloadsPerDevice
        && active_.size() < MaxConcurrentDownloads;
}

void BoardEvidenceCache::startDownload(const VehicleEvent& event)
{
    IBoardApiClient* apiClient = apiClientResolver_ ? apiClientResolver_(event.identity.deviceId) : nullptr;
    if (!apiClient || !repository_) {
        persistFailure(
            event,
            EvidenceCacheStatus::Failed,
            makeCacheError(
                QStringLiteral("rv1126b.evidence.invalid_dependencies"),
                QStringLiteral("Evidence cache is missing API client or repository.")));
        return;
    }

    ApiError error;
    if (!ensureCacheDirectory(event, &error)) {
        persistFailure(event, EvidenceCacheStatus::Failed, error);
        return;
    }

    const QString partFilePath = partPathFor(event);
    QFile::remove(partFilePath);

    ActiveDownload active;
    active.event = event;
    active.entry = entryFor(event, EvidenceCacheStatus::Downloading);
    active.entry.localFilePath = finalPathFor(event);
    active.partFilePath = partFilePath;
    active.apiClient = apiClient;

    activePerDevice_[event.identity.deviceId] = activePerDevice_.value(event.identity.deviceId, 0) + 1;
    persistState(active.entry);

    const EventIdentity identity = event.identity;
    active_.insert(identity, active);

    const RequestId requestId = apiClient->downloadEvidenceToPartFile(
        event.identity,
        event.evidenceRelativeUrl,
        partFilePath,
        this,
        [this, identity](ApiResult<EvidenceDownloadResult> result) {
            handleDownloadFinished(identity, std::move(result));
        });

    const auto activeIt = active_.find(identity);
    if (activeIt != active_.end()) {
        activeIt->requestId = requestId;
    }
}

void BoardEvidenceCache::handleDownloadFinished(
    const EventIdentity& identity,
    ApiResult<EvidenceDownloadResult> result)
{
    const auto activeIt = active_.find(identity);
    if (activeIt == active_.end()) {
        return;
    }

    const VehicleEvent event = activeIt->event;
    if (!result.isSuccess()) {
        const ApiError error = result.error();
        QFile::remove(activeIt->partFilePath);
        finishActive(identity);
        persistFailure(event, statusForError(error), error);
        if (statusForError(error) == EvidenceCacheStatus::RetryWait) {
            scheduleRetry(event);
        }
        drainQueue();
        return;
    }

    const QString finalPath = finalPathFor(event);
    ApiError error;
    if (!validateDownloadedFile(result.value(), finalPath, &error)
        || !promotePartFile(result.value().partFilePath, finalPath, &error)) {
        QFile::remove(result.value().partFilePath);
        finishActive(identity);
        persistFailure(event, EvidenceCacheStatus::Failed, error);
        drainQueue();
        return;
    }

    EvidenceCacheEntry entry = entryFor(event, EvidenceCacheStatus::Available);
    entry.localFilePath = finalPath;
    entry.contentLength = QFileInfo(finalPath).size();
    QPointer<IBoardApiClient> apiClient = activeIt->apiClient;
    finishActive(identity);
    retryAttempts_.remove(identity);
    persistState(entry);
    ClientAckCreate ack;
    ack.clientId = QStringLiteral("qt_primary");
    ack.evidenceSize = entry.contentLength;
    if (apiClient) {
        apiClient->putClientAck(identity, ack, this, [](ApiResult<ClientAckDto>) {});
    }
    drainQueue();
}

void BoardEvidenceCache::finishActive(const EventIdentity& identity)
{
    const auto activeIt = active_.find(identity);
    if (activeIt == active_.end()) {
        return;
    }

    const QString deviceId = activeIt->event.identity.deviceId;
    const int remaining = std::max(0, activePerDevice_.value(deviceId, 0) - 1);
    if (remaining == 0) {
        activePerDevice_.remove(deviceId);
    } else {
        activePerDevice_[deviceId] = remaining;
    }
    active_.erase(activeIt);
}

void BoardEvidenceCache::scheduleRetry(const VehicleEvent& event)
{
    const EventIdentity identity = event.identity;
    if (scheduledRetries_.contains(identity)) {
        return;
    }

    scheduledRetries_.insert(identity);
    QTimer::singleShot(nextRetryDelayMs(identity), this, [this, event, identity] {
        if (!scheduledRetries_.remove(identity)) {
            return;
        }
        enqueue(event);
    });
}

void BoardEvidenceCache::cancelRetry(const EventIdentity& identity)
{
    scheduledRetries_.remove(identity);
    retryAttempts_.remove(identity);
}

void BoardEvidenceCache::cancelRetriesForDevice(const QString& deviceId)
{
    QVector<EventIdentity> identities;
    for (const EventIdentity& identity : scheduledRetries_) {
        if (identity.deviceId == deviceId) {
            identities.append(identity);
        }
    }
    for (const EventIdentity& identity : identities) {
        cancelRetry(identity);
    }
}

int BoardEvidenceCache::nextRetryDelayMs(const EventIdentity& identity)
{
    const int attempt = retryAttempts_.value(identity, 0);
    retryAttempts_.insert(identity, attempt + 1);
    const int index = std::min<int>(attempt, static_cast<int>(RetryBackoffSeconds.size()) - 1);
    return RetryBackoffSeconds[static_cast<size_t>(index)] * 1000;
}

void BoardEvidenceCache::persistState(const EvidenceCacheEntry& entry)
{
    if (!repository_) {
        emit stateChanged(entry);
        return;
    }

    repository_->saveEvidenceState(
        entry,
        this,
        [this, entry](ApiResult<void> result) {
            if (!result.isSuccess()) {
                emit cacheError(entry.identity, result.error());
                return;
            }
            emit stateChanged(entry);
        });
}

void BoardEvidenceCache::persistFailure(
    const VehicleEvent& event,
    EvidenceCacheStatus status,
    const ApiError& error)
{
    EvidenceCacheEntry entry = entryFor(event, status);
    entry.failureCode = error.code;
    persistState(entry);
    emit cacheError(event.identity, error);
}

EvidenceCacheEntry BoardEvidenceCache::entryFor(
    const VehicleEvent& event,
    EvidenceCacheStatus status) const
{
    EvidenceCacheEntry entry;
    entry.identity = event.identity;
    entry.role = QStringLiteral("evidence");
    entry.remoteRelativeUrl = event.evidenceRelativeUrl;
    entry.localFilePath = status == EvidenceCacheStatus::Available || status == EvidenceCacheStatus::Downloading
        ? finalPathFor(event)
        : QString();
    entry.contentLength = -1;
    entry.status = status;
    entry.updatedEpochMs = nowEpochMs();
    return entry;
}

QString BoardEvidenceCache::partPathFor(const VehicleEvent& event) const
{
    return finalPathFor(event) + QStringLiteral(".part");
}

QString BoardEvidenceCache::eventFileStem(const VehicleEvent& event) const
{
    return QStringLiteral("%1_%2_evidence")
        .arg(event.identity.eventId)
        .arg(event.identity.trackId);
}

QString BoardEvidenceCache::sanitizePathSegment(const QString& value) const
{
    QString sanitized = value;
    sanitized.replace(QRegularExpression(QStringLiteral(R"([^A-Za-z0-9_.-])")), QStringLiteral("_"));
    return sanitized.isEmpty() ? QStringLiteral("unknown") : sanitized;
}

bool BoardEvidenceCache::ensureCacheDirectory(const VehicleEvent& event, ApiError* error) const
{
    const QFileInfo finalInfo(finalPathFor(event));
    if (QDir().mkpath(finalInfo.absolutePath())) {
        return true;
    }

    if (error) {
        *error = makeCacheError(
            QStringLiteral("rv1126b.evidence.create_dir_failed"),
            QStringLiteral("Failed to create evidence cache directory."));
    }
    return false;
}

bool BoardEvidenceCache::validateDownloadedFile(
    const EvidenceDownloadResult& result,
    const QString& finalPath,
    ApiError* error) const
{
    Q_UNUSED(finalPath)

    QFileInfo partInfo(result.partFilePath);
    if (!partInfo.exists() || partInfo.size() <= 0) {
        if (error) {
            *error = makeCacheError(
                QStringLiteral("rv1126b.evidence.empty_file"),
                QStringLiteral("Downloaded evidence file is empty."));
        }
        return false;
    }

    if (result.expectedContentLength >= 0 && result.expectedContentLength != partInfo.size()) {
        if (error) {
            *error = makeCacheError(
                QStringLiteral("rv1126b.evidence.length_mismatch"),
                QStringLiteral("Downloaded evidence length does not match Content-Length."));
        }
        return false;
    }

    if (result.receivedBytes >= 0 && result.receivedBytes != partInfo.size()) {
        if (error) {
            *error = makeCacheError(
                QStringLiteral("rv1126b.evidence.received_mismatch"),
                QStringLiteral("Downloaded evidence length does not match received byte count."));
        }
        return false;
    }

    QImageReader reader(result.partFilePath);
    reader.setAutoDetectImageFormat(true);
    const QByteArray format = reader.format().toLower();
    if (!reader.canRead() || (format != QByteArrayLiteral("jpeg") && format != QByteArrayLiteral("jpg"))) {
        if (error) {
            *error = makeCacheError(
                QStringLiteral("rv1126b.evidence.invalid_jpeg"),
                QStringLiteral("Downloaded evidence file is not a decodable JPEG."));
        }
        return false;
    }

    return true;
}

bool BoardEvidenceCache::promotePartFile(
    const QString& partFilePath,
    const QString& finalPath,
    ApiError* error) const
{
    QFile::remove(finalPath);
    if (QFile::rename(partFilePath, finalPath)) {
        return true;
    }

    if (error) {
        *error = makeCacheError(
            QStringLiteral("rv1126b.evidence.rename_failed"),
            QStringLiteral("Failed to promote evidence part file."));
    }
    return false;
}

ApiError BoardEvidenceCache::makeCacheError(
    const QString& code,
    const QString& message,
    bool retryable) const
{
    ApiError error;
    error.code = code;
    error.message = message;
    error.category = ApiErrorCategory::Storage;
    error.retryable = retryable;
    return error;
}

EvidenceCacheStatus BoardEvidenceCache::statusForError(const ApiError& error) const
{
    if (error.category == ApiErrorCategory::NotFound || error.httpStatus == 404) {
        return EvidenceCacheStatus::Missing;
    }

    if (error.retryable
        || error.category == ApiErrorCategory::Temporary
        || error.category == ApiErrorCategory::Conflict
        || error.httpStatus == 409) {
        return EvidenceCacheStatus::RetryWait;
    }

    return EvidenceCacheStatus::Failed;
}

} // namespace rv1126b
