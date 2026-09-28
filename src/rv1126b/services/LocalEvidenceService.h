#pragma once

#include "EvidenceCache.h"

#include "../core/Result.h"
#include "../ports/IEventRepository.h"

#include <QObject>
#include <QString>

namespace rv1126b {

struct LocalEvidenceResult {
    EventIdentity identity;
    EvidenceCacheStatus status = EvidenceCacheStatus::NotRequested;
    QString localFilePath;
    QString failureCode;
    bool downloadQueued = false;
};

class LocalEvidenceService final : public QObject
{
    Q_OBJECT

public:
    LocalEvidenceService(
        IEventRepository* repository,
        EvidenceCache* evidenceCache,
        QObject* parent = nullptr);

    RequestId resolve(
        const VehicleEvent& event,
        bool deviceOnline,
        QObject* context,
        ApiCompletion<LocalEvidenceResult> completion);

private:
    void finishResolve(
        const VehicleEvent& event,
        bool deviceOnline,
        QObject* context,
        ApiCompletion<LocalEvidenceResult> completion,
        ApiResult<std::optional<EvidenceCacheEntry>> stateResult);
    LocalEvidenceResult resultFromEntry(const VehicleEvent& event, const EvidenceCacheEntry& entry) const;
    LocalEvidenceResult missingResult(const VehicleEvent& event, EvidenceCacheStatus status) const;
    ApiError serviceError(const QString& code, const QString& message) const;

    IEventRepository* repository_ = nullptr;
    EvidenceCache* evidenceCache_ = nullptr;
};

} // namespace rv1126b

Q_DECLARE_METATYPE(rv1126b::LocalEvidenceResult)
