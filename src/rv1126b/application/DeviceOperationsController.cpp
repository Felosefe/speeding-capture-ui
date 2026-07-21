#include "DeviceOperationsController.h"
#include "../services/BoardFtpTaskSnapshotService.h"

#include <QDateTime>
#include <QHostAddress>

#include <limits>

namespace rv1126b {
namespace {

constexpr qint64 FirstSupportedEpochMs = 1577836800000LL; // 2020-01-01 UTC
constexpr qint64 LastSupportedEpochMs = 4102444800000LL;  // 2100-01-01 UTC, exclusive
constexpr qint64 MaxTaskSpanMs = 366LL * 24 * 60 * 60 * 1000;

QString operationMessage(const QString& fallback, const ApiError& error)
{
    if (error.code == QStringLiteral("config_write_disabled"))
        return QStringLiteral("板端未开放展示配置写入能力");
    if (error.code == QStringLiteral("time_set_disabled"))
        return QStringLiteral("板端未开放校时能力");
    if (error.code == QStringLiteral("ftp_config_write_disabled"))
        return QStringLiteral("板端未开放 FTP 配置写入能力");
    if (error.code == QStringLiteral("ftp_task_write_disabled"))
        return QStringLiteral("板端未开放 FTP 历史任务能力");
    if (error.code == QStringLiteral("config_revision_conflict"))
        return QStringLiteral("FTP 配置已被其他客户端修改，正在重新读取");

    switch (error.category) {
    case ApiErrorCategory::Authentication:
        return QStringLiteral("认证失败，请重新配置 Token");
    case ApiErrorCategory::CapabilityDisabled:
        return QStringLiteral("板端未开放此项写入能力");
    case ApiErrorCategory::Validation:
        return error.message.isEmpty() ? QStringLiteral("输入内容不符合协议约束") : error.message;
    case ApiErrorCategory::Conflict:
        return QStringLiteral("配置存在并发冲突，请重新确认");
    case ApiErrorCategory::Network:
    case ApiErrorCategory::Temporary:
        return QStringLiteral("设备通信失败，请检查网络后重试");
    case ApiErrorCategory::Cancelled:
        return QString();
    case ApiErrorCategory::Protocol:
        return QStringLiteral("设备响应不符合 v1 协议");
    default:
        return fallback;
    }
}

} // namespace

DeviceOperationsController::DeviceOperationsController(DeviceOperationsDependencies dependencies,
                                                       QObject* parent)
    : QObject(parent)
    , dependencies_(std::move(dependencies))
{
}

DeviceOperationsController::~DeviceOperationsController()
{
    cancelPending();
}

QString DeviceOperationsController::deviceId() const { return deviceId_; }
bool DeviceOperationsController::boardApiAvailable() const { return boardApi_ != nullptr; }
bool DeviceOperationsController::ftpServiceAvailable() const { return ftpService_ != nullptr; }
bool DeviceOperationsController::ftpTaskSnapshotAvailable() const { return ftpTaskSnapshot_ != nullptr; }
bool DeviceOperationsController::isBusy(const QString& operation) const
{
    return busyOperations_.contains(operation);
}

void DeviceOperationsController::selectDevice(const QString& deviceId)
{
    if (shutdown_) return;
    if (deviceId_ == deviceId) {
        if (!boardApi_ && dependencies_.boardApiForDevice)
            boardApi_ = dependencies_.boardApiForDevice(deviceId);
        if (!ftpService_ && dependencies_.ftpServiceForDevice)
            ftpService_ = dependencies_.ftpServiceForDevice(deviceId);
        if (!ftpTaskSnapshot_ && dependencies_.ftpTaskSnapshotForDevice)
            ftpTaskSnapshot_ = dependencies_.ftpTaskSnapshotForDevice(deviceId);
        return;
    }
    cancelPending();
    deviceId_ = deviceId;
    boardApi_ = dependencies_.boardApiForDevice ? dependencies_.boardApiForDevice(deviceId) : nullptr;
    ftpService_ = dependencies_.ftpServiceForDevice ? dependencies_.ftpServiceForDevice(deviceId) : nullptr;
    ftpTaskSnapshot_ = dependencies_.ftpTaskSnapshotForDevice
        ? dependencies_.ftpTaskSnapshotForDevice(deviceId) : nullptr;
    ftpConfig_.reset();
    ftpControl_.reset();
    conflictUpdate_.reset();
    conflictPasswordTargets_.clear();
    awaitingConflictReload_ = false;
    emit selectedDeviceChanged(deviceId_);
}

void DeviceOperationsController::loadAll()
{
    loadEvidenceConfig();
    loadTime();
    loadFtpConfig();
    loadFtpControl();
    listFtpTasks();
}

void DeviceOperationsController::loadEvidenceConfig()
{
    if (!boardApi_) return emitUnavailable(QStringLiteral("board_api"));
    const QString operation = QStringLiteral("evidence.load");
    const int generation = beginOperation(operation);
    if (generation == 0) return;
    const RequestId requestId = boardApi_->getEvidenceConfig(this, [this, operation, generation](ApiResult<EvidenceConfigDto> result) {
        if (!finishOperation(operation, generation)) return;
        if (!result) return reportError(result.error(), QStringLiteral("读取展示配置失败"));
        emit evidenceConfigLoaded(result.value());
    });
    rememberBoardRequest(operation, generation, requestId);
}

void DeviceOperationsController::saveEvidenceConfig(const EvidenceConfigUpdate& update)
{
    if (!boardApi_) return emitUnavailable(QStringLiteral("board_api"));
    if (const auto error = validateEvidence(update)) return reportError(*error, error->message);
    const QString operation = QStringLiteral("evidence.save");
    const int generation = beginOperation(operation);
    if (generation == 0) return;
    const RequestId requestId = boardApi_->putEvidenceConfig(update, this,
        [this, operation, generation](ApiResult<EvidenceConfigDto> result) {
            if (!finishOperation(operation, generation)) return;
            if (!result) return reportError(result.error(), QStringLiteral("保存展示配置失败"));
            emit evidenceConfigSaved(result.value());
        });
    rememberBoardRequest(operation, generation, requestId);
}

void DeviceOperationsController::loadTime()
{
    if (!boardApi_) return emitUnavailable(QStringLiteral("board_api"));
    const QString operation = QStringLiteral("time.load");
    const int generation = beginOperation(operation);
    if (generation == 0) return;
    const RequestId requestId = boardApi_->getTime(this, [this, operation, generation](ApiResult<TimeStatusDto> result) {
        if (!finishOperation(operation, generation)) return;
        if (!result) return reportError(result.error(), QStringLiteral("读取设备时间失败"));
        emit timeLoaded(result.value());
    });
    rememberBoardRequest(operation, generation, requestId);
}

void DeviceOperationsController::setTimeUtc(qint64 utcEpochMs)
{
    if (!boardApi_) return emitUnavailable(QStringLiteral("board_api"));
    if (utcEpochMs < FirstSupportedEpochMs || utcEpochMs >= LastSupportedEpochMs)
        return reportError(validationError(QStringLiteral("invalid_utc_epoch_ms"),
                                           QStringLiteral("校时时间必须位于 2020 至 2099 年")),
                           QStringLiteral("校时时间无效"));
    TimeUpdate update;
    update.utcEpochMs = utcEpochMs;
    const QString operation = QStringLiteral("time.save");
    const int generation = beginOperation(operation);
    if (generation == 0) return;
    const RequestId requestId = boardApi_->putTime(update, this, [this, operation, generation](ApiResult<TimeStatusDto> result) {
        if (!finishOperation(operation, generation)) return;
        if (!result) return reportError(result.error(), QStringLiteral("设备校时失败"));
        emit timeSaved(result.value());
    });
    rememberBoardRequest(operation, generation, requestId);
}

void DeviceOperationsController::loadFtpConfig()
{
    if (!ftpService_) return emitUnavailable(QStringLiteral("ftp"));
    const QString operation = QStringLiteral("ftp.config.load");
    const int generation = beginOperation(operation);
    if (generation == 0) return;
    ftpService_->loadConfig(this, [this, operation, generation](ApiResult<FtpConfigSnapshotDto> result) {
        if (!finishOperation(operation, generation)) return;
        if (!result) {
            awaitingConflictReload_ = false;
            return reportError(result.error(), QStringLiteral("读取 FTP 配置失败"));
        }
        ftpConfig_ = result.value();
        if (awaitingConflictReload_ && conflictUpdate_) {
            awaitingConflictReload_ = false;
            conflictUpdate_->expectedRevision = result.value().revision;
            emit ftpRevisionConflict(result.value(), *conflictUpdate_, conflictPasswordTargets_);
        } else {
            emit ftpConfigLoaded(result.value());
        }
    });
}

void DeviceOperationsController::saveFtpConfigAndEnableNewEvents(const FtpConfigUpdate& update)
{
    if (!ftpService_) return emitUnavailable(QStringLiteral("ftp"));
    if (const auto error = validateFtpConfig(update)) return reportError(*error, error->message);
    conflictUpdate_ = update;
    conflictPasswordTargets_.clear();
    const QString operation = QStringLiteral("ftp.config.save");
    const int generation = beginOperation(operation);
    if (generation == 0) return;
    ftpService_->saveConfigAndEnableNewEvents(
        update, this, [this, operation, generation](ApiResult<FtpActivationResult> result) {
            if (!finishOperation(operation, generation)) return;
            if (!result) {
                if (result.error().code == QStringLiteral("config_revision_conflict")
                    || result.error().category == ApiErrorCategory::Conflict) {
                    if (conflictUpdate_)
                        *conflictUpdate_ = withoutPasswords(*conflictUpdate_, &conflictPasswordTargets_);
                    awaitingConflictReload_ = true;
                    reportError(result.error(), QStringLiteral("FTP 配置存在并发冲突"));
                    loadFtpConfig();
                    return;
                }
                conflictUpdate_.reset();
                return reportError(result.error(), QStringLiteral("保存 FTP 配置失败"));
            }
            conflictUpdate_.reset();
            if (result.value().configSaved && !result.value().newRevision.isEmpty() && ftpConfig_)
                ftpConfig_->revision = result.value().newRevision;
            if (result.value().configSaved) loadFtpConfig();
            if (result.value().autoEnabled) loadFtpControl();
            emit ftpActivationFinished(result.value());
        });
    if (conflictUpdate_)
        *conflictUpdate_ = withoutPasswords(*conflictUpdate_, &conflictPasswordTargets_);
}

void DeviceOperationsController::loadFtpControl()
{
    if (!ftpService_) return emitUnavailable(QStringLiteral("ftp"));
    const QString operation = QStringLiteral("ftp.control.load");
    const int generation = beginOperation(operation);
    if (generation == 0) return;
    ftpService_->loadControl(this, [this, operation, generation](ApiResult<FtpControlDto> result) {
        if (!finishOperation(operation, generation)) return;
        if (!result) return reportError(result.error(), QStringLiteral("读取 FTP 自动下发状态失败"));
        ftpControl_ = result.value();
        emit ftpControlLoaded(result.value());
    });
}

void DeviceOperationsController::updateFtpControl(bool enabled, FtpControlScope scope)
{
    if (!ftpService_) return emitUnavailable(QStringLiteral("ftp"));
    if (!ftpControl_) return reportError(validationError(QStringLiteral("ftp_control_not_loaded"),
                                                         QStringLiteral("请先读取自动下发状态")),
                                             QStringLiteral("自动下发状态尚未读取"));
    FtpControlUpdate update;
    update.expectedRevision = ftpControl_->revision;
    update.enabled = enabled;
    update.scope.value = enabled ? scope : FtpControlScope::Preserve;
    const QString operation = QStringLiteral("ftp.control.save");
    const int generation = beginOperation(operation);
    if (generation == 0) return;
    ftpService_->updateControl(update, this,
        [this, operation, generation](ApiResult<FtpControlDto> result) {
            if (!finishOperation(operation, generation)) return;
            if (!result) return reportError(result.error(), QStringLiteral("更新 FTP 自动下发状态失败"));
            ftpControl_ = result.value();
            emit ftpControlSaved(result.value());
        });
}

void DeviceOperationsController::rollbackFtpConfig()
{
    if (!ftpService_) return emitUnavailable(QStringLiteral("ftp"));
    if (!ftpConfig_) return reportError(validationError(QStringLiteral("ftp_config_not_loaded"),
                                                        QStringLiteral("请先读取 FTP 配置")),
                                            QStringLiteral("FTP 配置尚未读取"));
    const QString operation = QStringLiteral("ftp.config.rollback");
    const int generation = beginOperation(operation);
    if (generation == 0) return;
    ftpService_->rollbackConfig(ftpConfig_->revision, this,
        [this, operation, generation](ApiResult<FtpConfigSnapshotDto> result) {
            if (!finishOperation(operation, generation)) return;
            if (!result) return reportError(result.error(), QStringLiteral("回滚 FTP 配置失败"));
            ftpConfig_ = result.value();
            emit ftpConfigRolledBack(result.value());
            emit ftpConfigLoaded(result.value());
        });
}

void DeviceOperationsController::listFtpTasks(const std::optional<QString>& cursor)
{
    if (!ftpService_) return emitUnavailable(QStringLiteral("ftp"));
    const QString operation = QStringLiteral("ftp.tasks.list");
    const int generation = beginOperation(operation);
    if (generation == 0) return;
    const QString requestedCursor = cursor.value_or(QString());
    ftpService_->listTasks(FtpTaskPageSize, cursor, this,
        [this, operation, generation, requestedCursor](ApiResult<FtpTaskPageDto> result) {
            if (!finishOperation(operation, generation)) return;
            if (!result) return reportError(result.error(), QStringLiteral("读取 FTP 历史任务失败"));
            emit ftpTasksLoaded(result.value(), requestedCursor);
        });
}

void DeviceOperationsController::loadFtpTask(const QString& taskId)
{
    if (!ftpService_) return emitUnavailable(QStringLiteral("ftp"));
    if (taskId.trimmed().isEmpty())
        return reportError(validationError(QStringLiteral("invalid_task_id"), QStringLiteral("任务 ID 不能为空")),
                           QStringLiteral("任务 ID 无效"));
    const QString operation = QStringLiteral("ftp.task.load");
    const int generation = beginOperation(operation);
    if (generation == 0) return;
    ftpService_->loadTask(taskId, this,
        [this, operation, generation](ApiResult<FtpTaskDetailDto> result) {
            if (!finishOperation(operation, generation)) return;
            if (!result) return reportError(result.error(), QStringLiteral("读取 FTP 任务详情失败"));
            if (ftpTaskSnapshot_) {
                ftpTaskSnapshot_->saveTaskDetail(result.value(), this,
                    [this](ApiResult<StoredFtpTask> saved) {
                        if (!saved) reportError(saved.error(), QStringLiteral("保存 FTP 本地快照失败"));
                    });
            }
            emit ftpTaskLoaded(result.value());
        });
}

void DeviceOperationsController::createFtpTask(const FtpTaskCreate& request)
{
    if (!ftpService_) return emitUnavailable(QStringLiteral("ftp"));
    if (const auto error = validateTask(request)) return reportError(*error, error->message);
    const QString operation = QStringLiteral("ftp.task.create");
    const int generation = beginOperation(operation);
    if (generation == 0) return;
    ftpService_->createTask(request, this,
        [this, operation, generation](ApiResult<FtpTaskDetailDto> result) {
            if (!finishOperation(operation, generation)) return;
            if (!result) return reportError(result.error(), QStringLiteral("创建 FTP 历史任务失败"));
            if (ftpTaskSnapshot_) {
                ftpTaskSnapshot_->saveTaskDetail(result.value(), this,
                    [this](ApiResult<StoredFtpTask> saved) {
                        if (!saved) reportError(saved.error(), QStringLiteral("保存 FTP 本地快照失败"));
                    });
            }
            emit ftpTaskCreated(result.value());
        });
}

void DeviceOperationsController::retryFtpTask(const QString& taskId)
{
    if (!ftpService_) return emitUnavailable(QStringLiteral("ftp"));
    if (taskId.trimmed().isEmpty())
        return reportError(validationError(QStringLiteral("invalid_task_id"), QStringLiteral("任务 ID 不能为空")),
                           QStringLiteral("任务 ID 无效"));
    const QString operation = QStringLiteral("ftp.task.retry");
    const int generation = beginOperation(operation);
    if (generation == 0) return;
    ftpService_->retryTask(taskId, this,
        [this, operation, generation](ApiResult<FtpTaskDetailDto> result) {
            if (!finishOperation(operation, generation)) return;
            if (!result) return reportError(result.error(), QStringLiteral("重试 FTP 历史任务失败"));
            if (ftpTaskSnapshot_) {
                ftpTaskSnapshot_->saveTaskDetail(result.value(), this,
                    [this](ApiResult<StoredFtpTask> saved) {
                        if (!saved) reportError(saved.error(), QStringLiteral("保存 FTP 本地快照失败"));
                    });
            }
            emit ftpTaskRetried(result.value());
        });
}

void DeviceOperationsController::loadLocalFtpTaskSnapshots()
{
    if (!ftpTaskSnapshot_) return emitUnavailable(QStringLiteral("ftp_snapshot"));
    FtpTaskQuery query;
    query.deviceId = deviceId_;
    query.limit = 200;
    ftpTaskSnapshot_->loadTaskSnapshots(query, this,
        [this](ApiResult<QVector<StoredFtpTask>> result) {
            if (!result) return reportError(result.error(), QStringLiteral("读取 FTP 本地快照失败"));
            emit localFtpTaskSnapshotsLoaded(result.value());
        });
}

void DeviceOperationsController::cancelPending()
{
    if (boardApi_) {
        for (const RequestId& requestId : boardRequests_) boardApi_->cancel(requestId);
    }
    boardRequests_.clear();
    if (ftpService_) ftpService_->cancelAll();
    invalidateOperations();
}

void DeviceOperationsController::shutdown()
{
    if (shutdown_) return;
    shutdown_ = true;
    cancelPending();
    boardApi_ = nullptr;
    ftpService_ = nullptr;
    ftpTaskSnapshot_ = nullptr;
}

int DeviceOperationsController::beginOperation(const QString& operation)
{
    if (busyOperations_.contains(operation)) return 0;
    const int generation = generations_.value(operation) + 1;
    generations_.insert(operation, generation);
    if (!busyOperations_.contains(operation)) {
        busyOperations_.insert(operation);
        emit operationBusyChanged(operation, true);
    }
    return generation;
}

void DeviceOperationsController::rememberBoardRequest(const QString& operation,
                                                      int generation,
                                                      const RequestId& requestId)
{
    if (!requestId.isNull() && generations_.value(operation) == generation
        && busyOperations_.contains(operation))
        boardRequests_.insert(operation, requestId);
}

bool DeviceOperationsController::finishOperation(const QString& operation, int generation)
{
    if (shutdown_ || generations_.value(operation) != generation) return false;
    boardRequests_.remove(operation);
    if (busyOperations_.remove(operation)) emit operationBusyChanged(operation, false);
    return true;
}

void DeviceOperationsController::invalidateOperations()
{
    for (auto it = generations_.begin(); it != generations_.end(); ++it) ++it.value();
    const QSet<QString> busy = busyOperations_;
    busyOperations_.clear();
    boardRequests_.clear();
    for (const QString& operation : busy) emit operationBusyChanged(operation, false);
}

void DeviceOperationsController::emitUnavailable(const QString& service)
{
    emit userError(QStringLiteral("service_unavailable"),
                   service == QStringLiteral("ftp")
                       ? QStringLiteral("当前设备的 FTP 服务尚未装配")
                       : service == QStringLiteral("ftp_snapshot")
                           ? QStringLiteral("当前设备没有可用的 FTP 本地快照服务")
                       : QStringLiteral("当前设备的配置与时间服务尚未装配"));
}

void DeviceOperationsController::reportError(const ApiError& error, const QString& fallback)
{
    const QString message = operationMessage(fallback, error);
    if (message.isEmpty()) return;
    emit userError(error.code.isEmpty() ? QStringLiteral("device_operation_failed") : error.code, message);
}

std::optional<ApiError> DeviceOperationsController::validateEvidence(const EvidenceConfigUpdate& update) const
{
    const auto invalidText = [](const QString& value, int maxBytes) {
        return value.toUtf8().size() > maxBytes || containsControlCharacter(value);
    };
    if (invalidText(update.siteName, 128))
        return validationError(QStringLiteral("invalid_site_name"), QStringLiteral("点位名称超过 128 个 UTF-8 字节或包含控制字符"));
    if (invalidText(update.roadDirection, 64))
        return validationError(QStringLiteral("invalid_road_direction"), QStringLiteral("道路方向超过 64 个 UTF-8 字节或包含控制字符"));
    if (invalidText(update.statusText, 64))
        return validationError(QStringLiteral("invalid_status_text"), QStringLiteral("状态文字超过 64 个 UTF-8 字节或包含控制字符"));
    if (invalidText(update.codeText, 128))
        return validationError(QStringLiteral("invalid_code_text"), QStringLiteral("代码文字超过 128 个 UTF-8 字节或包含控制字符"));
    if (update.speedLimitKmh < 0 || update.speedLimitKmh > 300)
        return validationError(QStringLiteral("invalid_speed_limit"), QStringLiteral("限速必须位于 0..300 km/h"));
    return std::nullopt;
}

std::optional<ApiError> DeviceOperationsController::validateFtpConfig(const FtpConfigUpdate& update) const
{
    if (update.expectedRevision.trimmed().isEmpty())
        return validationError(QStringLiteral("invalid_expected_revision"), QStringLiteral("请先读取最新 FTP 配置"));
    if (update.deviceId != deviceId_)
        return validationError(QStringLiteral("invalid_device_id"), QStringLiteral("FTP 配置设备身份与当前设备不一致"));
    if (update.targets.size() > MaxFtpTargets)
        return validationError(QStringLiteral("too_many_ftp_targets"), QStringLiteral("FTP 目标最多 8 个"));
    if (update.retryMax < 0 || update.retryIntervalSec < 0 || update.connectTimeoutSec < 0
        || update.transferTimeoutSec < 0 || update.scanIntervalSec < 0)
        return validationError(QStringLiteral("invalid_ftp_timing"), QStringLiteral("FTP 重试和超时参数不能为负数"));
    QSet<QString> ids;
    for (const FtpTargetUpdate& target : update.targets) {
        const QString id = target.id.trimmed();
        if (id.isEmpty() || ids.contains(id))
            return validationError(QStringLiteral("invalid_ftp_target_id"), QStringLiteral("FTP 目标 ID 不能为空或重复"));
        ids.insert(id);
        QHostAddress address;
        if (!address.setAddress(target.host.trimmed()) || address.protocol() != QAbstractSocket::IPv4Protocol)
            return validationError(QStringLiteral("invalid_ftp_host"), QStringLiteral("FTP 主机当前只支持 IPv4 地址"));
        if (target.port == 0)
            return validationError(QStringLiteral("invalid_ftp_port"), QStringLiteral("FTP 端口必须位于 1..65535"));
        if (target.user.trimmed().isEmpty())
            return validationError(QStringLiteral("invalid_ftp_user"), QStringLiteral("FTP 用户名不能为空"));
        if (target.remoteDir.trimmed().isEmpty())
            return validationError(QStringLiteral("invalid_ftp_remote_dir"), QStringLiteral("FTP 远端目录不能为空"));
        if (target.passwordAction.value == FtpPasswordAction::Replace
            && (!target.replacementPassword || target.replacementPassword->isEmpty()))
            return validationError(QStringLiteral("invalid_ftp_password"), QStringLiteral("替换密码时必须输入新密码"));
        if (target.passwordAction.value != FtpPasswordAction::Replace && target.replacementPassword)
            return validationError(QStringLiteral("invalid_ftp_password_action"), QStringLiteral("仅替换密码时允许携带密码"));
        if (target.passwordAction.value == FtpPasswordAction::Unknown)
            return validationError(QStringLiteral("invalid_ftp_password_action"), QStringLiteral("请选择有效的密码处理方式"));
    }
    return std::nullopt;
}

std::optional<ApiError> DeviceOperationsController::validateTask(const FtpTaskCreate& request) const
{
    if (request.startEpochMs < FirstSupportedEpochMs || request.endEpochMs >= LastSupportedEpochMs
        || request.startEpochMs >= request.endEpochMs)
        return validationError(QStringLiteral("invalid_ftp_task_range"), QStringLiteral("任务时间必须位于 2020 至 2099 年且开始早于结束"));
    if (request.endEpochMs - request.startEpochMs > MaxTaskSpanMs)
        return validationError(QStringLiteral("ftp_task_range_too_large"), QStringLiteral("任务时间跨度不能超过 366 天"));
    if (request.targetIds.isEmpty())
        return validationError(QStringLiteral("invalid_ftp_task_targets"), QStringLiteral("请至少选择一个已启用 FTP 目标"));
    if (!ftpConfig_)
        return validationError(QStringLiteral("ftp_config_not_loaded"), QStringLiteral("请先读取 FTP 配置"));
    QSet<QString> enabled;
    for (const FtpTargetSnapshotDto& target : ftpConfig_->targets)
        if (target.enabled) enabled.insert(target.id);
    QSet<QString> requested;
    for (const QString& id : request.targetIds) {
        if (!enabled.contains(id) || requested.contains(id))
            return validationError(QStringLiteral("invalid_ftp_task_targets"), QStringLiteral("任务目标必须存在、已启用且不能重复"));
        requested.insert(id);
    }
    return std::nullopt;
}

bool DeviceOperationsController::containsControlCharacter(const QString& value)
{
    for (const QChar ch : value) if (ch.category() == QChar::Other_Control) return true;
    return false;
}

ApiError DeviceOperationsController::validationError(const QString& code, const QString& message)
{
    ApiError error;
    error.code = code;
    error.message = message;
    error.category = ApiErrorCategory::Validation;
    return error;
}

FtpConfigUpdate DeviceOperationsController::withoutPasswords(FtpConfigUpdate update,
                                                             QStringList* passwordTargetIds)
{
    if (passwordTargetIds) passwordTargetIds->clear();
    for (FtpTargetUpdate& target : update.targets) {
        if (target.passwordAction.value == FtpPasswordAction::Replace && passwordTargetIds)
            passwordTargetIds->append(target.id);
        target.replacementPassword.reset();
    }
    return update;
}

} // namespace rv1126b
