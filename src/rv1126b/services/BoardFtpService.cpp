#include "BoardFtpService.h"

namespace rv1126b {

BoardFtpService::BoardFtpService(IBoardApiClient* apiClient, QObject* parent)
    : FtpService(parent)
    , apiClient_(apiClient)
{
}

RequestId BoardFtpService::loadConfig(QObject* context, ApiCompletion<FtpConfigSnapshotDto> completion)
{
    return apiClient_ ? apiClient_->getFtpConfig(context, std::move(completion))
                      : missingClient(context, std::move(completion));
}

RequestId BoardFtpService::saveConfigAndEnableNewEvents(
    const FtpConfigUpdate& update,
    QObject* context,
    ApiCompletion<FtpActivationResult> completion)
{
    if (!apiClient_) {
        return missingClient(context, std::move(completion));
    }

    const RequestId requestId = RequestId::createUuid();
    PendingActivation pending;
    pending.context = context;
    pending.completion = std::move(completion);
    pendingActivations_.insert(requestId, std::move(pending));

    const RequestId innerRequestId = apiClient_->putFtpConfig(
        update,
        this,
        [this, requestId](ApiResult<FtpConfigSnapshotDto> result) {
            handleConfigSaved(requestId, std::move(result));
        });
    const auto it = pendingActivations_.find(requestId);
    if (it != pendingActivations_.end()) {
        it->innerRequestId = innerRequestId;
    }
    return requestId;
}

RequestId BoardFtpService::rollbackConfig(
    const QString& expectedRevision,
    QObject* context,
    ApiCompletion<FtpConfigSnapshotDto> completion)
{
    return apiClient_ ? apiClient_->rollbackFtpConfig(expectedRevision, context, std::move(completion))
                      : missingClient(context, std::move(completion));
}

RequestId BoardFtpService::loadControl(QObject* context, ApiCompletion<FtpControlDto> completion)
{
    return apiClient_ ? apiClient_->getFtpControl(context, std::move(completion))
                      : missingClient(context, std::move(completion));
}

RequestId BoardFtpService::updateControl(
    const FtpControlUpdate& update,
    QObject* context,
    ApiCompletion<FtpControlDto> completion)
{
    return apiClient_ ? apiClient_->putFtpControl(update, context, std::move(completion))
                      : missingClient(context, std::move(completion));
}

RequestId BoardFtpService::createTask(
    const FtpTaskCreate& request,
    QObject* context,
    ApiCompletion<FtpTaskDetailDto> completion)
{
    return apiClient_ ? apiClient_->createFtpTask(request, context, std::move(completion))
                      : missingClient(context, std::move(completion));
}

RequestId BoardFtpService::listTasks(
    int limit,
    const std::optional<QString>& cursor,
    QObject* context,
    ApiCompletion<FtpTaskPageDto> completion)
{
    return apiClient_ ? apiClient_->listFtpTasks(limit, cursor, context, std::move(completion))
                      : missingClient(context, std::move(completion));
}

RequestId BoardFtpService::loadTask(
    const QString& taskId,
    QObject* context,
    ApiCompletion<FtpTaskDetailDto> completion)
{
    return apiClient_ ? apiClient_->getFtpTask(taskId, context, std::move(completion))
                      : missingClient(context, std::move(completion));
}

RequestId BoardFtpService::retryTask(
    const QString& taskId,
    QObject* context,
    ApiCompletion<FtpTaskDetailDto> completion)
{
    return apiClient_ ? apiClient_->retryFtpTask(taskId, context, std::move(completion))
                      : missingClient(context, std::move(completion));
}

void BoardFtpService::cancel(const RequestId& requestId)
{
    const auto it = pendingActivations_.find(requestId);
    if (it == pendingActivations_.end()) {
        if (apiClient_) {
            apiClient_->cancel(requestId);
        }
        return;
    }

    const RequestId innerRequestId = it->innerRequestId;
    if (apiClient_ && !innerRequestId.isNull()) {
        apiClient_->cancel(innerRequestId);
    }
    finishActivation(requestId, ApiResult<FtpActivationResult>::failure(localError(
        QStringLiteral("cancelled"),
        QStringLiteral("FTP activation request cancelled."),
        ApiErrorCategory::Cancelled)));
}

void BoardFtpService::cancelAll()
{
    const QList<RequestId> requestIds = pendingActivations_.keys();
    for (const RequestId& requestId : requestIds) {
        cancel(requestId);
    }
    if (apiClient_) {
        apiClient_->cancelAll();
    }
}

void BoardFtpService::handleConfigSaved(const RequestId& requestId, ApiResult<FtpConfigSnapshotDto> result)
{
    if (!pendingActivations_.contains(requestId)) {
        return;
    }
    if (!result.isSuccess()) {
        finishActivation(requestId, ApiResult<FtpActivationResult>::failure(result.error()));
        return;
    }

    FtpControlUpdate control;
    control.expectedRevision = result.value().revision;
    control.enabled = true;
    control.scope.value = FtpControlScope::NewEventsOnly;
    control.scope.rawValue = QStringLiteral("new_events_only");
    const RequestId innerRequestId = apiClient_->putFtpControl(
        control,
        this,
        [this, requestId, newRevision = result.value().revision](ApiResult<FtpControlDto> controlResult) {
            handleControlSaved(requestId, newRevision, std::move(controlResult));
        });
    const auto it = pendingActivations_.find(requestId);
    if (it != pendingActivations_.end()) {
        it->innerRequestId = innerRequestId;
    }
}

void BoardFtpService::handleControlSaved(
    const RequestId& requestId,
    const QString& newRevision,
    ApiResult<FtpControlDto> result)
{
    if (!pendingActivations_.contains(requestId)) {
        return;
    }

    FtpActivationResult activation;
    activation.configSaved = true;
    activation.newRevision = newRevision;
    if (result.isSuccess()) {
        activation.autoEnabled = true;
    } else {
        activation.error = result.error();
    }
    finishActivation(requestId, ApiResult<FtpActivationResult>::success(std::move(activation)));
}

void BoardFtpService::finishActivation(
    const RequestId& requestId,
    ApiResult<FtpActivationResult> result)
{
    const auto it = pendingActivations_.find(requestId);
    if (it == pendingActivations_.end()) {
        return;
    }
    PendingActivation pending = std::move(it.value());
    pendingActivations_.erase(it);
    finish(pending.context, std::move(pending.completion), std::move(result));
}

ApiError BoardFtpService::localError(
    const QString& code,
    const QString& message,
    ApiErrorCategory category) const
{
    ApiError error;
    error.code = code;
    error.message = message;
    error.category = category;
    return error;
}

} // namespace rv1126b
