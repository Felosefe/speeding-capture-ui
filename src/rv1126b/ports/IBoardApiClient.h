#pragma once

#include "../core/Result.h"
#include "../core/ValueTypes.h"
#include "../protocol/ApiDtos.h"

#include <QObject>
#include <QString>

#include <optional>

namespace rv1126b {

class IBoardApiClient : public QObject
{
public:
    explicit IBoardApiClient(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    ~IBoardApiClient() override = default;

    virtual RequestId getHealth(QObject* context, ApiCompletion<HealthDto> completion) = 0;
    virtual RequestId listEvents(
        int limit,
        const std::optional<QString>& cursor,
        QObject* context,
        ApiCompletion<EventPageDto> completion) = 0;
    virtual RequestId getEventDetail(
        const EventIdentity& identity,
        QObject* context,
        ApiCompletion<EventDetailDto> completion) = 0;
    virtual RequestId downloadEvidenceToPartFile(
        const EventIdentity& identity,
        const QString& evidenceRelativeUrl,
        const QString& partFilePath,
        QObject* context,
        ApiCompletion<EvidenceDownloadResult> completion) = 0;

    virtual RequestId getEvidenceConfig(QObject* context, ApiCompletion<EvidenceConfigDto> completion) = 0;
    virtual RequestId putEvidenceConfig(
        const EvidenceConfigUpdate& update,
        QObject* context,
        ApiCompletion<EvidenceConfigDto> completion) = 0;
    virtual RequestId getTime(QObject* context, ApiCompletion<TimeStatusDto> completion) = 0;
    virtual RequestId putTime(
        const TimeUpdate& update,
        QObject* context,
        ApiCompletion<TimeStatusDto> completion) = 0;

    virtual RequestId getFtpConfig(QObject* context, ApiCompletion<FtpConfigSnapshotDto> completion) = 0;
    virtual RequestId putFtpConfig(
        const FtpConfigUpdate& update,
        QObject* context,
        ApiCompletion<FtpConfigSnapshotDto> completion) = 0;
    virtual RequestId rollbackFtpConfig(
        const QString& expectedRevision,
        QObject* context,
        ApiCompletion<FtpConfigSnapshotDto> completion) = 0;
    virtual RequestId getFtpControl(QObject* context, ApiCompletion<FtpControlDto> completion) = 0;
    virtual RequestId putFtpControl(
        const FtpControlUpdate& update,
        QObject* context,
        ApiCompletion<FtpControlDto> completion) = 0;
    virtual RequestId createFtpTask(
        const FtpTaskCreate& request,
        QObject* context,
        ApiCompletion<FtpTaskDetailDto> completion) = 0;
    virtual RequestId listFtpTasks(
        int limit,
        const std::optional<QString>& cursor,
        QObject* context,
        ApiCompletion<FtpTaskPageDto> completion) = 0;
    virtual RequestId getFtpTask(
        const QString& taskId,
        QObject* context,
        ApiCompletion<FtpTaskDetailDto> completion) = 0;
    virtual RequestId retryFtpTask(
        const QString& taskId,
        QObject* context,
        ApiCompletion<FtpTaskDetailDto> completion) = 0;

    virtual void cancel(const RequestId& requestId) = 0;
    virtual void cancelAll() = 0;
};

} // namespace rv1126b

