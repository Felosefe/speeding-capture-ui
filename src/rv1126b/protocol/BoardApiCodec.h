#pragma once

#include "ApiCodec.h"

namespace rv1126b {

class BoardApiCodec final : public IApiCodec
{
public:
    ApiResult<DiscoveredDeviceDto> parseDiscoveryResponse(
        const QByteArray& payload,
        const QString& expectedNonce) const override;
    ApiError parseError(int httpStatus, const QByteArray& payload) const override;
    ApiResult<HealthDto> parseHealth(const QByteArray& payload) const override;
    ApiResult<EventPageDto> parseEventPage(const QByteArray& payload) const override;
    ApiResult<EventDetailDto> parseEventDetail(const QByteArray& payload) const override;
    ApiResult<EvidenceConfigDto> parseEvidenceConfig(const QByteArray& payload) const override;
    ApiResult<TimeStatusDto> parseTimeStatus(const QByteArray& payload) const override;
    ApiResult<VideoStreamsConfigDto> parseVideoStreamsConfig(const QByteArray& payload) const override;
    ApiResult<QByteArray> encodeVideoStreamsConfig(const VideoStreamsUpdate& update) const override;
    ApiResult<TriggerModeConfigDto> parseTriggerModeConfig(const QByteArray& payload) const override;
    ApiResult<LineRegionConfigDto> parseLineRegionConfig(const QByteArray& payload) const override;
    ApiResult<RuntimeApplyDto> parseRuntimeApply(const QByteArray& payload) const override;
    ApiResult<FtpConfigSnapshotDto> parseFtpConfig(const QByteArray& payload) const override;
    ApiResult<FtpControlDto> parseFtpControl(const QByteArray& payload) const override;
    ApiResult<FtpTaskPageDto> parseFtpTaskPage(const QByteArray& payload) const override;
    ApiResult<FtpTaskDetailDto> parseFtpTaskDetail(const QByteArray& payload) const override;
    ApiResult<ClientAckDto> parseClientAck(const QByteArray& payload) const override;

    ApiResult<QByteArray> encodeEvidenceConfig(const EvidenceConfigUpdate& update) const override;
    ApiResult<QByteArray> encodeTimeUpdate(const TimeUpdate& update) const override;
    ApiResult<QByteArray> encodeTriggerModeConfig(const TriggerModeUpdate& update) const override;
    ApiResult<QByteArray> encodeLineRegionConfig(const LineRegionUpdate& update) const override;
    ApiResult<QByteArray> encodeRuntimeApply(const RuntimeApplyUpdate& update) const override;
    ApiResult<QByteArray> encodeFtpConfig(const FtpConfigUpdate& update) const override;
    ApiResult<QByteArray> encodeFtpControl(const FtpControlUpdate& update) const override;
    ApiResult<QByteArray> encodeFtpTaskCreate(const FtpTaskCreate& request) const override;
    ApiResult<QByteArray> encodeExpectedRevision(const QString& expectedRevision) const override;
    ApiResult<QByteArray> encodeClientAck(const ClientAckCreate& request) const override;
};

} // namespace rv1126b
