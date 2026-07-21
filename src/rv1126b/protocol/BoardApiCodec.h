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
    ApiResult<FtpConfigSnapshotDto> parseFtpConfig(const QByteArray& payload) const override;
    ApiResult<FtpControlDto> parseFtpControl(const QByteArray& payload) const override;
    ApiResult<FtpTaskPageDto> parseFtpTaskPage(const QByteArray& payload) const override;
    ApiResult<FtpTaskDetailDto> parseFtpTaskDetail(const QByteArray& payload) const override;

    ApiResult<QByteArray> encodeEvidenceConfig(const EvidenceConfigUpdate& update) const override;
    ApiResult<QByteArray> encodeTimeUpdate(const TimeUpdate& update) const override;
    ApiResult<QByteArray> encodeFtpConfig(const FtpConfigUpdate& update) const override;
    ApiResult<QByteArray> encodeFtpControl(const FtpControlUpdate& update) const override;
    ApiResult<QByteArray> encodeFtpTaskCreate(const FtpTaskCreate& request) const override;
    ApiResult<QByteArray> encodeExpectedRevision(const QString& expectedRevision) const override;
};

} // namespace rv1126b
