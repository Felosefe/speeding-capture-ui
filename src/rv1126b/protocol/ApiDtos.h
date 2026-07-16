#pragma once

#include "../core/ValueTypes.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

#include <optional>

namespace rv1126b {

enum class OcrStatus {
    Queued,
    Matched,
    NoPlate,
    Ambiguous,
    NoVehicle,
    Failed,
    TimedOut,
    QueueFull,
    Unknown
};

enum class FtpPasswordAction {
    Keep,
    Replace,
    Clear,
    Unknown
};

enum class FtpControlScope {
    NewEventsOnly,
    AllExisting,
    Preserve,
    Unknown
};

enum class FtpTaskState {
    Queued,
    Running,
    Done,
    Failed,
    Unknown
};

struct DiscoveredDeviceDto {
    QString magic;
    int version = 0;
    QString type;
    QString nonce;
    QString deviceId;
    QString deviceModel;
    QString ipv4;
    QString apiVersion;
    QUrl apiUrl;
    QString releaseVersion;
    bool authRequired = true;
    QStringList capabilities;
    QJsonObject rawJson;
};

struct ApplicationApiStatusDto {
    bool alive = false;
    quint16 httpPort = 0;
    quint16 discoveryPort = 0;
    bool authRequired = true;
};

struct HealthDto {
    QString apiVersion;
    QString deviceId;
    QString deviceModel;
    QString releaseVersion;
    NormalizedTime serverTime;
    bool pipelineHealthAvailable = false;
    QJsonObject pipeline;
    ApplicationApiStatusDto applicationApi;
    QJsonObject rawJson;
};

struct EventSummaryDto {
    qint64 eventId = 0;
    qint64 trackId = 0;
    NormalizedTime eventTime;
    QString motionDirection;
    int speedKmh = 0;
    bool speedValid = false;
    QString speedStatus;
    WireEnum<OcrStatus> ocrStatus;
    QString plateText;
    QString plateAscii;
    QString plateColor;
    QString evidenceStatus;
    bool evidenceAvailable = false;
    QString detailRelativeUrl;
    QString evidenceRelativeUrl;
    QJsonObject rawJson;
};

struct EventPageDto {
    QString apiVersion;
    QVector<EventSummaryDto> items;
    int count = 0;
    bool hasMore = false;
    std::optional<QString> nextCursor;
    QJsonObject rawJson;
};

struct EventDetailDto {
    EventSummaryDto summary;
    QString triggerMode;
    QString captureReason;
    QJsonObject vehicle;
    QJsonObject lineRegion;
    QJsonObject radar;
    QJsonObject ocr;
    QJsonObject images;
    std::optional<QString> evidenceRelativeUrl;
    QJsonObject rawJson;
};

struct EvidenceConfigUpdate {
    QString siteName;
    QString roadDirection;
    int speedLimitKmh = 0;
    QString statusText;
    QString codeText;
};

struct EvidenceConfigDto {
    QString apiVersion;
    EvidenceConfigUpdate evidence;
    bool restartRequired = false;
    QString applyMode;
    QString effectiveScope;
    bool existingEventsUnchanged = true;
    QJsonObject rawJson;
};

struct TimeUpdate {
    qint64 utcEpochMs = 0;
};

struct TimeStatusDto {
    QString apiVersion;
    NormalizedTime time;
    QString timezoneContract;
    QString ntpStatus;
    bool timeSetEnabled = false;
    QJsonObject rawJson;
};

struct FtpTargetSnapshotDto {
    QString id;
    bool enabled = false;
    QString host;
    quint16 port = 21;
    QString user;
    QString remoteDir;
    bool passive = true;
    bool passwordConfigured = false;
};

struct FtpTargetUpdate {
    QString id;
    bool enabled = false;
    QString host;
    quint16 port = 21;
    QString user;
    WireEnum<FtpPasswordAction> passwordAction;
    std::optional<QString> replacementPassword;
    QString remoteDir;
    bool passive = true;
};

struct FtpConfigSnapshotDto {
    QString apiVersion;
    QString revision;
    QString deviceId;
    int retryMax = 0;
    int retryIntervalSec = 0;
    int connectTimeoutSec = 0;
    int transferTimeoutSec = 0;
    int scanIntervalSec = 0;
    QVector<FtpTargetSnapshotDto> targets;
    bool restartRequired = false;
    QJsonObject rawJson;
};

struct FtpConfigUpdate {
    QString expectedRevision;
    QString deviceId;
    int retryMax = 0;
    int retryIntervalSec = 0;
    int connectTimeoutSec = 0;
    int transferTimeoutSec = 0;
    int scanIntervalSec = 0;
    QVector<FtpTargetUpdate> targets;
};

struct FtpControlUpdate {
    QString expectedRevision;
    bool enabled = false;
    WireEnum<FtpControlScope> scope;
};

struct FtpControlDto {
    QString apiVersion;
    QString revision;
    bool enabled = false;
    WireEnum<FtpControlScope> scope;
    QJsonObject rawJson;
};

struct FtpTaskCreate {
    qint64 startEpochMs = 0;
    qint64 endEpochMs = 0;
    QStringList targetIds;
};

struct FtpTaskTargetStatusDto {
    QString targetId;
    WireEnum<FtpTaskState> state;
    int total = 0;
    int pending = 0;
    int uploading = 0;
    int done = 0;
    int failed = 0;
    int attempts = 0;
    QString lastError;
};

struct FtpTaskSummaryDto {
    QString taskId;
    WireEnum<FtpTaskState> state;
    qint64 startEpochMs = 0;
    qint64 endEpochMs = 0;
    qint64 createdEpochMs = 0;
    QStringList targetIds;
    QJsonObject rawJson;
};

struct FtpTaskDetailDto {
    FtpTaskSummaryDto summary;
    QVector<FtpTaskTargetStatusDto> targets;
    QJsonObject rawJson;
};

struct FtpTaskPageDto {
    QString apiVersion;
    QVector<FtpTaskSummaryDto> items;
    int count = 0;
    bool hasMore = false;
    std::optional<QString> nextCursor;
    QJsonObject rawJson;
};

struct EvidenceDownloadResult {
    QString partFilePath;
    QString contentType;
    qint64 expectedContentLength = -1;
    qint64 receivedBytes = 0;
};

} // namespace rv1126b

Q_DECLARE_METATYPE(rv1126b::DiscoveredDeviceDto)
Q_DECLARE_METATYPE(rv1126b::HealthDto)
