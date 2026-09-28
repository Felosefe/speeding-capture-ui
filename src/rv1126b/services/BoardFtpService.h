#pragma once

#include "../ports/IBoardApiClient.h"
#include "FtpService.h"

#include <QHash>
#include <QPointer>

namespace rv1126b {

class BoardFtpService final : public FtpService
{
    Q_OBJECT

public:
    explicit BoardFtpService(IBoardApiClient* apiClient, QObject* parent = nullptr);

    RequestId loadConfig(QObject* context, ApiCompletion<FtpConfigSnapshotDto> completion) override;
    RequestId saveConfigAndEnableNewEvents(
        const FtpConfigUpdate& update,
        QObject* context,
        ApiCompletion<FtpActivationResult> completion) override;
    RequestId rollbackConfig(
        const QString& expectedRevision,
        QObject* context,
        ApiCompletion<FtpConfigSnapshotDto> completion) override;
    RequestId loadControl(QObject* context, ApiCompletion<FtpControlDto> completion) override;
    RequestId updateControl(
        const FtpControlUpdate& update,
        QObject* context,
        ApiCompletion<FtpControlDto> completion) override;
    RequestId createTask(
        const FtpTaskCreate& request,
        QObject* context,
        ApiCompletion<FtpTaskDetailDto> completion) override;
    RequestId listTasks(
        int limit,
        const std::optional<QString>& cursor,
        QObject* context,
        ApiCompletion<FtpTaskPageDto> completion) override;
    RequestId loadTask(
        const QString& taskId,
        QObject* context,
        ApiCompletion<FtpTaskDetailDto> completion) override;
    RequestId retryTask(
        const QString& taskId,
        QObject* context,
        ApiCompletion<FtpTaskDetailDto> completion) override;
    void cancel(const RequestId& requestId) override;
    void cancelAll() override;

private:
    struct PendingActivation {
        RequestId innerRequestId;
        QPointer<QObject> context;
        ApiCompletion<FtpActivationResult> completion;
    };

    template<typename T>
    RequestId missingClient(QObject* context, ApiCompletion<T> completion)
    {
        const RequestId requestId = RequestId::createUuid();
        finish(context, std::move(completion), ApiResult<T>::failure(localError(
            QStringLiteral("rv1126b.ftp.missing_api_client"),
            QStringLiteral("The board API client is unavailable."),
            ApiErrorCategory::Network)));
        return requestId;
    }

    template<typename T>
    static void finish(QObject* context, ApiCompletion<T> completion, ApiResult<T> result)
    {
        if (!context || !completion) {
            return;
        }
        QPointer<QObject> guardedContext(context);
        QMetaObject::invokeMethod(context, [guardedContext, completion = std::move(completion), result = std::move(result)]() mutable {
            if (guardedContext) {
                completion(std::move(result));
            }
        }, Qt::QueuedConnection);
    }

    void handleConfigSaved(const RequestId& requestId, ApiResult<FtpConfigSnapshotDto> result);
    void handleControlSaved(
        const RequestId& requestId,
        const QString& newRevision,
        ApiResult<FtpControlDto> result);
    void finishActivation(const RequestId& requestId, ApiResult<FtpActivationResult> result);
    ApiError localError(const QString& code, const QString& message, ApiErrorCategory category) const;

    QPointer<IBoardApiClient> apiClient_;
    QHash<RequestId, PendingActivation> pendingActivations_;
};

} // namespace rv1126b
