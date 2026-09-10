#pragma once

#include "../core/Result.h"
#include "../core/ValueTypes.h"
#include "../protocol/ApiDtos.h"

#include <QObject>
#include <QString>

#include <optional>
#include <utility>

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
    virtual RequestId downloadFileToPartFile(
        const QString& relativeUrl,
        const QString& partFilePath,
        QObject* context,
        ApiCompletion<EvidenceDownloadResult> completion)
    {
        Q_UNUSED(relativeUrl)
        Q_UNUSED(partFilePath)
        Q_UNUSED(context)
        ApiError error;
        error.code = QStringLiteral("file_download_unsupported");
        error.message = QStringLiteral("File download is not supported by this API client.");
        error.category = ApiErrorCategory::CapabilityDisabled;
        if (completion) completion(ApiResult<EvidenceDownloadResult>::failure(std::move(error)));
        return RequestId::createUuid();
    }
    virtual RequestId putClientAck(
        const EventIdentity& identity,
        const ClientAckCreate& request,
        QObject* context,
        ApiCompletion<ClientAckDto> completion)
    {
        Q_UNUSED(identity)
        Q_UNUSED(request)
        Q_UNUSED(context)
        ApiError error;
        error.code = QStringLiteral("client_ack_unsupported");
        error.message = QStringLiteral("Client acknowledgement is not supported by this API client.");
        error.category = ApiErrorCategory::CapabilityDisabled;
        if (completion) {
            completion(ApiResult<ClientAckDto>::failure(std::move(error)));
        }
        return RequestId::createUuid();
    }

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

    virtual RequestId getVideoStreamsConfig(QObject* context, ApiCompletion<VideoStreamsConfigDto> completion)
    {
        Q_UNUSED(context)
        ApiError error;
        error.code = QStringLiteral("video_streams_config_unsupported");
        error.message = QStringLiteral("设备不支持码流配置，请升级板端接口");
        error.category = ApiErrorCategory::CapabilityDisabled;
        if (completion) completion(ApiResult<VideoStreamsConfigDto>::failure(std::move(error)));
        return RequestId::createUuid();
    }

    virtual RequestId putVideoStreamsConfig(const VideoStreamsUpdate& update, QObject* context,
                                           ApiCompletion<VideoStreamsConfigDto> completion)
    {
        Q_UNUSED(update)
        Q_UNUSED(context)
        ApiError error;
        error.code = QStringLiteral("video_streams_config_unsupported");
        error.message = QStringLiteral("设备不支持码流配置，请升级板端接口");
        error.category = ApiErrorCategory::CapabilityDisabled;
        if (completion) completion(ApiResult<VideoStreamsConfigDto>::failure(std::move(error)));
        return RequestId::createUuid();
    }

    virtual RequestId getTriggerModeConfig(QObject* context, ApiCompletion<TriggerModeConfigDto> completion)
    {
        Q_UNUSED(context)
        ApiError error;
        error.code = QStringLiteral("trigger_mode_config_unsupported");
        error.message = QStringLiteral("Trigger mode configuration is not supported by this API client.");
        error.category = ApiErrorCategory::CapabilityDisabled;
        if (completion) completion(ApiResult<TriggerModeConfigDto>::failure(std::move(error)));
        return RequestId::createUuid();
    }

    virtual RequestId putTriggerModeConfig(
        const TriggerModeUpdate& update,
        QObject* context,
        ApiCompletion<TriggerModeConfigDto> completion)
    {
        Q_UNUSED(update)
        Q_UNUSED(context)
        ApiError error;
        error.code = QStringLiteral("trigger_mode_config_unsupported");
        error.message = QStringLiteral("Trigger mode configuration is not supported by this API client.");
        error.category = ApiErrorCategory::CapabilityDisabled;
        if (completion) completion(ApiResult<TriggerModeConfigDto>::failure(std::move(error)));
        return RequestId::createUuid();
    }

    virtual RequestId getLineRegionConfig(QObject* context, ApiCompletion<LineRegionConfigDto> completion)
    {
        Q_UNUSED(context)
        ApiError error;
        error.code = QStringLiteral("line_region_config_unsupported");
        error.message = QStringLiteral("Line-region configuration is not supported by this API client.");
        error.category = ApiErrorCategory::CapabilityDisabled;
        if (completion) completion(ApiResult<LineRegionConfigDto>::failure(std::move(error)));
        return RequestId::createUuid();
    }

    virtual RequestId putLineRegionConfig(
        const LineRegionUpdate& update,
        QObject* context,
        ApiCompletion<LineRegionConfigDto> completion)
    {
        Q_UNUSED(update)
        Q_UNUSED(context)
        ApiError error;
        error.code = QStringLiteral("line_region_config_unsupported");
        error.message = QStringLiteral("Line-region configuration is not supported by this API client.");
        error.category = ApiErrorCategory::CapabilityDisabled;
        if (completion) completion(ApiResult<LineRegionConfigDto>::failure(std::move(error)));
        return RequestId::createUuid();
    }

    virtual RequestId applyRuntimeConfig(
        const RuntimeApplyUpdate& update,
        QObject* context,
        ApiCompletion<RuntimeApplyDto> completion)
    {
        Q_UNUSED(update)
        Q_UNUSED(context)
        ApiError error;
        error.code = QStringLiteral("runtime_apply_unsupported");
        error.message = QStringLiteral("Runtime configuration apply is not supported by this API client.");
        error.category = ApiErrorCategory::CapabilityDisabled;
        if (completion) completion(ApiResult<RuntimeApplyDto>::failure(std::move(error)));
        return RequestId::createUuid();
    }

    virtual RequestId getIspConfig(QObject* context, ApiCompletion<QJsonObject> completion)
    {
        Q_UNUSED(context)
        ApiError error;
        error.code = QStringLiteral("isp_config_unsupported");
        error.message = QStringLiteral("ISP configuration is not supported by this API client.");
        error.category = ApiErrorCategory::CapabilityDisabled;
        if (completion) completion(ApiResult<QJsonObject>::failure(std::move(error)));
        return RequestId::createUuid();
    }

    virtual RequestId saveCurrentIspConfig(QObject* context, ApiCompletion<QJsonObject> completion)
    {
        Q_UNUSED(context)
        ApiError error;
        error.code = QStringLiteral("isp_config_unsupported");
        error.message = QStringLiteral("ISP configuration is not supported by this API client.");
        error.category = ApiErrorCategory::CapabilityDisabled;
        if (completion) completion(ApiResult<QJsonObject>::failure(std::move(error)));
        return RequestId::createUuid();
    }

    virtual RequestId clearIspConfig(QObject* context, ApiCompletion<QJsonObject> completion)
    {
        Q_UNUSED(context)
        ApiError error;
        error.code = QStringLiteral("isp_config_unsupported");
        error.message = QStringLiteral("ISP configuration is not supported by this API client.");
        error.category = ApiErrorCategory::CapabilityDisabled;
        if (completion) completion(ApiResult<QJsonObject>::failure(std::move(error)));
        return RequestId::createUuid();
    }

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

