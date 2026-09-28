#pragma once

#include "../core/Result.h"
#include "../domain/Models.h"
#include "../protocol/ApiDtos.h"

#include <QObject>
#include <QString>

#include <optional>

namespace rv1126b {

class FtpService : public QObject
{
    Q_OBJECT

public:
    explicit FtpService(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    ~FtpService() override = default;

    virtual RequestId loadConfig(QObject* context, ApiCompletion<FtpConfigSnapshotDto> completion) = 0;
    virtual RequestId saveConfigAndEnableNewEvents(
        const FtpConfigUpdate& update,
        QObject* context,
        ApiCompletion<FtpActivationResult> completion) = 0;
    virtual RequestId rollbackConfig(
        const QString& expectedRevision,
        QObject* context,
        ApiCompletion<FtpConfigSnapshotDto> completion) = 0;
    virtual RequestId loadControl(QObject* context, ApiCompletion<FtpControlDto> completion) = 0;
    virtual RequestId updateControl(
        const FtpControlUpdate& update,
        QObject* context,
        ApiCompletion<FtpControlDto> completion) = 0;
    virtual RequestId createTask(
        const FtpTaskCreate& request,
        QObject* context,
        ApiCompletion<FtpTaskDetailDto> completion) = 0;
    virtual RequestId listTasks(
        int limit,
        const std::optional<QString>& cursor,
        QObject* context,
        ApiCompletion<FtpTaskPageDto> completion) = 0;
    virtual RequestId loadTask(
        const QString& taskId,
        QObject* context,
        ApiCompletion<FtpTaskDetailDto> completion) = 0;
    virtual RequestId retryTask(
        const QString& taskId,
        QObject* context,
        ApiCompletion<FtpTaskDetailDto> completion) = 0;
    virtual void cancel(const RequestId& requestId) = 0;
    virtual void cancelAll() = 0;
};

} // namespace rv1126b

