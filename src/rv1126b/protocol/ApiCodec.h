#pragma once

#include "ApiDtos.h"
#include "../core/Result.h"

#include <QByteArray>

namespace rv1126b {

class IApiCodec
{
public:
    virtual ~IApiCodec() = default;

    virtual ApiResult<DiscoveredDeviceDto> parseDiscoveryResponse(
        const QByteArray& payload,
        const QString& expectedNonce) const = 0;
    // Always returns a usable normalized error. Malformed error JSON is mapped
    // to a Protocol or Unknown ApiError instead of nesting ApiError in itself.
    virtual ApiError parseError(int httpStatus, const QByteArray& payload) const = 0;
    virtual ApiResult<HealthDto> parseHealth(const QByteArray& payload) const = 0;
    virtual ApiResult<EventPageDto> parseEventPage(const QByteArray& payload) const = 0;
    virtual ApiResult<EventDetailDto> parseEventDetail(const QByteArray& payload) const = 0;
    virtual ApiResult<EvidenceConfigDto> parseEvidenceConfig(const QByteArray& payload) const = 0;
    virtual ApiResult<TimeStatusDto> parseTimeStatus(const QByteArray& payload) const = 0;
    virtual ApiResult<FtpConfigSnapshotDto> parseFtpConfig(const QByteArray& payload) const = 0;
    virtual ApiResult<FtpControlDto> parseFtpControl(const QByteArray& payload) const = 0;
    virtual ApiResult<FtpTaskPageDto> parseFtpTaskPage(const QByteArray& payload) const = 0;
    virtual ApiResult<FtpTaskDetailDto> parseFtpTaskDetail(const QByteArray& payload) const = 0;

    virtual ApiResult<QByteArray> encodeEvidenceConfig(const EvidenceConfigUpdate& update) const = 0;
    virtual ApiResult<QByteArray> encodeTimeUpdate(const TimeUpdate& update) const = 0;
    virtual ApiResult<QByteArray> encodeFtpConfig(const FtpConfigUpdate& update) const = 0;
    virtual ApiResult<QByteArray> encodeFtpControl(const FtpControlUpdate& update) const = 0;
    virtual ApiResult<QByteArray> encodeFtpTaskCreate(const FtpTaskCreate& request) const = 0;
    virtual ApiResult<QByteArray> encodeExpectedRevision(const QString& expectedRevision) const = 0;
};

} // namespace rv1126b
