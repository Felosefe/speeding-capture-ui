#include "BoardFtpTaskSnapshotService.h"

#include <QDateTime>

#include <utility>

namespace rv1126b {
namespace {

qint64 nowEpochMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

StoredFtpTargetStatus targetFromDto(const FtpTaskTargetStatusDto& dto)
{
    StoredFtpTargetStatus target;
    target.targetId = dto.targetId;
    target.state = dto.state;
    target.total = dto.total;
    target.pending = dto.pending;
    target.uploading = dto.uploading;
    target.done = dto.done;
    target.failed = dto.failed;
    target.attempts = dto.attempts;
    target.lastError = dto.lastError;
    return target;
}

} // namespace

BoardFtpTaskSnapshotService::BoardFtpTaskSnapshotService(
    QString deviceId,
    IEventRepository* repository,
    QObject* parent)
    : QObject(parent)
    , deviceId_(std::move(deviceId))
    , repository_(repository)
{
}

QString BoardFtpTaskSnapshotService::deviceId() const
{
    return deviceId_;
}

StoredFtpTask BoardFtpTaskSnapshotService::snapshotFromDetail(
    const FtpTaskDetailDto& detail,
    qint64 refreshedEpochMs) const
{
    StoredFtpTask task;
    task.deviceId = deviceId_;
    task.taskId = detail.summary.taskId;
    task.startEpochMs = detail.summary.startEpochMs;
    task.endEpochMs = detail.summary.endEpochMs;
    task.state = detail.summary.state;
    task.createdEpochMs = detail.summary.createdEpochMs;
    task.refreshedEpochMs = refreshedEpochMs > 0 ? refreshedEpochMs : nowEpochMs();
    task.targets.reserve(detail.targets.size());
    for (const FtpTaskTargetStatusDto& target : detail.targets) {
        task.targets.append(targetFromDto(target));
    }
    return task;
}

RequestId BoardFtpTaskSnapshotService::saveTaskDetail(
    const FtpTaskDetailDto& detail,
    QObject* context,
    ApiCompletion<StoredFtpTask> completion)
{
    if (deviceId_.isEmpty() || !repository_) {
        return finish<StoredFtpTask>(context, std::move(completion), ApiResult<StoredFtpTask>::failure(serviceError(
            QStringLiteral("rv1126b.ftp_snapshot.invalid_dependencies"),
            QStringLiteral("FTP task snapshot service is missing device id or repository."))));
    }

    StoredFtpTask snapshot = snapshotFromDetail(detail);
    if (snapshot.taskId.isEmpty()) {
        return finish<StoredFtpTask>(context, std::move(completion), ApiResult<StoredFtpTask>::failure(serviceError(
            QStringLiteral("rv1126b.ftp_snapshot.empty_task_id"),
            QStringLiteral("FTP task detail is missing task id."))));
    }

    repository_->saveFtpTaskSnapshot(
        snapshot,
        context,
        [completion = std::move(completion), snapshot](ApiResult<void> result) mutable {
            if (!completion) {
                return;
            }

            if (!result.isSuccess()) {
                completion(ApiResult<StoredFtpTask>::failure(result.error()));
                return;
            }
            completion(ApiResult<StoredFtpTask>::success(snapshot));
        });
    return RequestId::createUuid();
}

RequestId BoardFtpTaskSnapshotService::loadTaskSnapshots(
    const FtpTaskQuery& query,
    QObject* context,
    ApiCompletion<QVector<StoredFtpTask>> completion)
{
    if (!repository_) {
        return finish<QVector<StoredFtpTask>>(
            context,
            std::move(completion),
            ApiResult<QVector<StoredFtpTask>>::failure(serviceError(
                QStringLiteral("rv1126b.ftp_snapshot.invalid_dependencies"),
                QStringLiteral("FTP task snapshot service is missing repository."))));
    }

    return repository_->loadFtpTaskSnapshots(query, context, std::move(completion));
}

ApiError BoardFtpTaskSnapshotService::serviceError(const QString& code, const QString& message) const
{
    ApiError error;
    error.code = code;
    error.message = message;
    error.category = ApiErrorCategory::Validation;
    error.retryable = false;
    return error;
}

} // namespace rv1126b
