#include "LocalEvidenceService.h"

#include <QFileInfo>

namespace rv1126b {

LocalEvidenceService::LocalEvidenceService(
    IEventRepository* repository,
    EvidenceCache* evidenceCache,
    QObject* parent)
    : QObject(parent)
    , repository_(repository)
    , evidenceCache_(evidenceCache)
{
}

RequestId LocalEvidenceService::resolve(
    const VehicleEvent& event,
    bool deviceOnline,
    QObject* context,
    ApiCompletion<LocalEvidenceResult> completion)
{
    if (!repository_ || !evidenceCache_) {
        if (completion) {
            completion(ApiResult<LocalEvidenceResult>::failure(serviceError(
                QStringLiteral("rv1126b.local_evidence.invalid_dependencies"),
                QStringLiteral("Local evidence service is missing repository or evidence cache."))));
        }
        return RequestId::createUuid();
    }

    const QString finalPath = evidenceCache_->finalPathFor(event);
    if (QFileInfo::exists(finalPath)) {
        LocalEvidenceResult result;
        result.identity = event.identity;
        result.status = EvidenceCacheStatus::Available;
        result.localFilePath = finalPath;
        if (completion) {
            completion(ApiResult<LocalEvidenceResult>::success(std::move(result)));
        }
        return RequestId::createUuid();
    }

    return repository_->loadEvidenceState(
        event.identity,
        QStringLiteral("evidence"),
        context,
        [this, event, deviceOnline, context, completion = std::move(completion)](
            ApiResult<std::optional<EvidenceCacheEntry>> stateResult) mutable {
            finishResolve(
                event,
                deviceOnline,
                context,
                std::move(completion),
                std::move(stateResult));
        });
}

void LocalEvidenceService::finishResolve(
    const VehicleEvent& event,
    bool deviceOnline,
    QObject* context,
    ApiCompletion<LocalEvidenceResult> completion,
    ApiResult<std::optional<EvidenceCacheEntry>> stateResult)
{
    Q_UNUSED(context)

    if (!stateResult.isSuccess()) {
        if (completion) {
            completion(ApiResult<LocalEvidenceResult>::failure(stateResult.error()));
        }
        return;
    }

    if (stateResult.value()) {
        LocalEvidenceResult result = resultFromEntry(event, *stateResult.value());
        if (result.status == EvidenceCacheStatus::Available && QFileInfo::exists(result.localFilePath)) {
            if (completion) {
                completion(ApiResult<LocalEvidenceResult>::success(std::move(result)));
            }
            return;
        }

        if (result.status == EvidenceCacheStatus::Downloading
            || result.status == EvidenceCacheStatus::Queued
            || result.status == EvidenceCacheStatus::RetryWait) {
            if (completion) {
                completion(ApiResult<LocalEvidenceResult>::success(std::move(result)));
            }
            return;
        }
    }

    if (deviceOnline && event.evidenceAvailable && !event.evidenceRelativeUrl.isEmpty()) {
        evidenceCache_->enqueue(event);
        LocalEvidenceResult result = missingResult(event, EvidenceCacheStatus::Queued);
        result.downloadQueued = true;
        if (completion) {
            completion(ApiResult<LocalEvidenceResult>::success(std::move(result)));
        }
        return;
    }

    const EvidenceCacheStatus status = event.evidenceAvailable
        ? EvidenceCacheStatus::NotRequested
        : EvidenceCacheStatus::Missing;
    if (completion) {
        completion(ApiResult<LocalEvidenceResult>::success(missingResult(event, status)));
    }
}

LocalEvidenceResult LocalEvidenceService::resultFromEntry(
    const VehicleEvent& event,
    const EvidenceCacheEntry& entry) const
{
    LocalEvidenceResult result;
    result.identity = event.identity;
    result.status = entry.status;
    result.localFilePath = entry.localFilePath;
    result.failureCode = entry.failureCode;
    return result;
}

LocalEvidenceResult LocalEvidenceService::missingResult(
    const VehicleEvent& event,
    EvidenceCacheStatus status) const
{
    LocalEvidenceResult result;
    result.identity = event.identity;
    result.status = status;
    return result;
}

ApiError LocalEvidenceService::serviceError(const QString& code, const QString& message) const
{
    ApiError error;
    error.code = code;
    error.message = message;
    error.category = ApiErrorCategory::Validation;
    error.retryable = false;
    return error;
}

} // namespace rv1126b
