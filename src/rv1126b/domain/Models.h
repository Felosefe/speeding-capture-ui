#pragma once

#include "../core/Result.h"
#include "../core/ValueTypes.h"
#include "../protocol/ApiDtos.h"

#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

#include <optional>

namespace rv1126b {

enum class DeviceSessionState {
    Disconnected,
    Connecting,
    Online,
    Degraded,
    AuthenticationFailed,
    Disconnecting
};

enum class EvidenceCacheStatus {
    NotRequested,
    Queued,
    Downloading,
    RetryWait,
    Available,
    Missing,
    Failed
};

struct DeviceEndpoint {
    QString ipv4;
    QUrl apiBaseUrl;
    quint16 httpPort = 18080;
    quint16 discoveryPort = 18081;
};

struct DeviceProfile {
    QString deviceId;
    QString deviceModel;
    QString releaseVersion;
    DeviceEndpoint endpoint;
    QString credentialRef;
    QStringList advertisedCapabilities;
    qint64 lastOnlineEpochMs = 0;
};

struct DeviceSessionSnapshot {
    DeviceProfile profile;
    DeviceSessionState state = DeviceSessionState::Disconnected;
    qint64 lastHealthEpochMs = 0;
    std::optional<HealthDto> lastHealth;
    std::optional<ApiError> lastError;
};

struct VehicleEvent {
    EventIdentity identity;
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
    QString captureStatus;
    QString captureError;
    QString detailRelativeUrl;
    QString evidenceRelativeUrl;
    qint64 firstSeenEpochMs = 0;
    qint64 lastUpdatedEpochMs = 0;
};

struct EventDetailSnapshot {
    EventIdentity identity;
    QString triggerMode;
    QString captureReason;
    QJsonObject vehicle;
    QJsonObject lineRegion;
    QJsonObject radar;
    QJsonObject ocr;
    QJsonObject images;
    QJsonObject captureTiming;
    QJsonObject rawJson;
    qint64 fetchedEpochMs = 0;
};

struct EvidenceCacheEntry {
    EventIdentity identity;
    QString role = QStringLiteral("evidence");
    QString remoteRelativeUrl;
    QString localFilePath;
    qint64 contentLength = -1;
    EvidenceCacheStatus status = EvidenceCacheStatus::NotRequested;
    QString failureCode;
    qint64 updatedEpochMs = 0;
};

struct StoredFtpTargetStatus {
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

struct StoredFtpTask {
    QString deviceId;
    QString taskId;
    qint64 startEpochMs = 0;
    qint64 endEpochMs = 0;
    WireEnum<FtpTaskState> state;
    qint64 createdEpochMs = 0;
    qint64 refreshedEpochMs = 0;
    QVector<StoredFtpTargetStatus> targets;
};

struct FtpTaskQuery {
    std::optional<QString> deviceId;
    std::optional<qint64> startEpochMs;
    std::optional<qint64> endEpochMs;
    int limit = 100;
    int offset = 0;
    bool newestFirst = true;
};

struct EventQuery {
    std::optional<QString> deviceId;
    std::optional<QString> plateText;
    std::optional<qint64> startEpochMs;
    std::optional<qint64> endEpochMs;
    int limit = 100;
    int offset = 0;
    bool newestFirst = true;
};

struct SyncAnchor {
    QString deviceId;
    EventSortKey previousHead;
    qint64 savedEpochMs = 0;
};

struct FtpActivationResult {
    bool configSaved = false;
    bool autoEnabled = false;
    QString newRevision;
    std::optional<ApiError> error;
};

} // namespace rv1126b

Q_DECLARE_METATYPE(rv1126b::DeviceProfile)
Q_DECLARE_METATYPE(rv1126b::DeviceSessionState)
Q_DECLARE_METATYPE(rv1126b::DeviceSessionSnapshot)
Q_DECLARE_METATYPE(rv1126b::VehicleEvent)
Q_DECLARE_METATYPE(rv1126b::EvidenceCacheEntry)
Q_DECLARE_METATYPE(rv1126b::StoredFtpTask)

