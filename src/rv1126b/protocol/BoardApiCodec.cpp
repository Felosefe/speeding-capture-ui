#include "BoardApiCodec.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

#include <cmath>
#include <limits>
#include <utility>

namespace rv1126b {
namespace {

constexpr qint64 MinSupportedEpochMs = 1577836800000LL;
constexpr qint64 MaxSupportedEpochMs = 4102444799999LL;
constexpr qint64 MaxTaskSpanMs = 366LL * 24LL * 60LL * 60LL * 1000LL;
constexpr auto ApiVersion = "v1";

ApiError protocolError(const QString& code, const QString& message)
{
    ApiError error;
    error.code = code;
    error.message = message;
    error.category = ApiErrorCategory::Protocol;
    return error;
}

ApiError validationError(const QString& code, const QString& message)
{
    ApiError error;
    error.code = code;
    error.message = message;
    error.category = ApiErrorCategory::Validation;
    return error;
}

template<typename T>
ApiResult<T> failure(const QString& code, const QString& message)
{
    return ApiResult<T>::failure(protocolError(code, message));
}

ApiResult<QJsonObject> parseObject(const QByteArray& payload)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return ApiResult<QJsonObject>::failure(protocolError(
            QStringLiteral("invalid_json"),
            QStringLiteral("Expected a JSON object response.")));
    }
    return ApiResult<QJsonObject>::success(document.object());
}

ApiResult<QString> requiredString(const QJsonObject& object, const char* key)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isString()) {
        return failure<QString>(QStringLiteral("invalid_field"), QStringLiteral("Expected string field: %1").arg(QLatin1String(key)));
    }
    return ApiResult<QString>::success(value.toString());
}

ApiResult<bool> requiredBool(const QJsonObject& object, const char* key)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isBool()) {
        return failure<bool>(QStringLiteral("invalid_field"), QStringLiteral("Expected boolean field: %1").arg(QLatin1String(key)));
    }
    return ApiResult<bool>::success(value.toBool());
}

ApiResult<qint64> requiredInteger(const QJsonObject& object, const char* key)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isDouble()) {
        return failure<qint64>(QStringLiteral("invalid_field"), QStringLiteral("Expected integer field: %1").arg(QLatin1String(key)));
    }

    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number
        || number < static_cast<double>(std::numeric_limits<qint64>::min())
        || number > static_cast<double>(std::numeric_limits<qint64>::max())) {
        return failure<qint64>(QStringLiteral("invalid_field"), QStringLiteral("Expected integer field: %1").arg(QLatin1String(key)));
    }
    return ApiResult<qint64>::success(static_cast<qint64>(number));
}

ApiResult<QJsonObject> requiredObject(const QJsonObject& object, const char* key)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isObject()) {
        return failure<QJsonObject>(QStringLiteral("invalid_field"), QStringLiteral("Expected object field: %1").arg(QLatin1String(key)));
    }
    return ApiResult<QJsonObject>::success(value.toObject());
}

ApiResult<QJsonArray> requiredArray(const QJsonObject& object, const char* key)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isArray()) {
        return failure<QJsonArray>(QStringLiteral("invalid_field"), QStringLiteral("Expected array field: %1").arg(QLatin1String(key)));
    }
    return ApiResult<QJsonArray>::success(value.toArray());
}

template<typename T>
bool assign(ApiResult<T> result, T* target, ApiError* error)
{
    if (!result.isSuccess()) {
        *error = result.error();
        return false;
    }
    *target = std::move(result.value());
    return true;
}

bool requireApiVersion(const QJsonObject& object, QString* version, ApiError* error)
{
    if (!assign(requiredString(object, "api_version"), version, error)) {
        return false;
    }
    if (*version != QLatin1String(ApiVersion)) {
        *error = protocolError(QStringLiteral("unsupported_api_version"), QStringLiteral("Expected API version v1."));
        return false;
    }
    return true;
}

WireEnum<TimeQuality> timeQualityFromWire(const QString& raw)
{
    WireEnum<TimeQuality> value;
    value.rawValue = raw;
    if (raw == QStringLiteral("native_utc")) {
        value.value = TimeQuality::NativeUtc;
    } else if (raw == QStringLiteral("configured_offset")) {
        value.value = TimeQuality::ConfiguredOffset;
    } else if (raw == QStringLiteral("board_epoch_unverified")) {
        value.value = TimeQuality::BoardEpochUnverified;
    }
    return value;
}

WireEnum<OcrStatus> ocrStatusFromWire(const QString& raw)
{
    WireEnum<OcrStatus> value;
    value.rawValue = raw;
    if (raw == QStringLiteral("queued")) value.value = OcrStatus::Queued;
    else if (raw == QStringLiteral("matched")) value.value = OcrStatus::Matched;
    else if (raw == QStringLiteral("no_plate")) value.value = OcrStatus::NoPlate;
    else if (raw == QStringLiteral("ambiguous")) value.value = OcrStatus::Ambiguous;
    else if (raw == QStringLiteral("no_vehicle")) value.value = OcrStatus::NoVehicle;
    else if (raw == QStringLiteral("failed")) value.value = OcrStatus::Failed;
    else if (raw == QStringLiteral("timed_out")) value.value = OcrStatus::TimedOut;
    else if (raw == QStringLiteral("queue_full")) value.value = OcrStatus::QueueFull;
    return value;
}

WireEnum<FtpPasswordAction> passwordActionFromWire(const QString& raw)
{
    WireEnum<FtpPasswordAction> value;
    value.rawValue = raw;
    if (raw == QStringLiteral("keep")) value.value = FtpPasswordAction::Keep;
    else if (raw == QStringLiteral("replace")) value.value = FtpPasswordAction::Replace;
    else if (raw == QStringLiteral("clear")) value.value = FtpPasswordAction::Clear;
    return value;
}

WireEnum<FtpControlScope> controlScopeFromWire(const QString& raw)
{
    WireEnum<FtpControlScope> value;
    value.rawValue = raw;
    if (raw == QStringLiteral("new_events_only")) value.value = FtpControlScope::NewEventsOnly;
    else if (raw == QStringLiteral("all_existing")) value.value = FtpControlScope::AllExisting;
    else if (raw == QStringLiteral("preserve")) value.value = FtpControlScope::Preserve;
    return value;
}

WireEnum<FtpTaskState> taskStateFromWire(const QString& raw)
{
    WireEnum<FtpTaskState> value;
    value.rawValue = raw;
    if (raw == QStringLiteral("queued")) value.value = FtpTaskState::Queued;
    else if (raw == QStringLiteral("running")) value.value = FtpTaskState::Running;
    else if (raw == QStringLiteral("done")) value.value = FtpTaskState::Done;
    else if (raw == QStringLiteral("failed")) value.value = FtpTaskState::Failed;
    return value;
}

ApiResult<NormalizedTime> parseNormalizedTime(const QJsonObject& object)
{
    ApiError error;
    NormalizedTime value;
    QString quality;
    if (!assign(requiredInteger(object, "epoch_ms"), &value.epochMs, &error)
        || !assign(requiredInteger(object, "source_epoch_ms"), &value.sourceEpochMs, &error)
        || !assign(requiredInteger(object, "offset_applied_ms"), &value.offsetAppliedMs, &error)
        || !assign(requiredString(object, "quality"), &quality, &error)) {
        return ApiResult<NormalizedTime>::failure(error);
    }
    value.quality = timeQualityFromWire(quality);
    return ApiResult<NormalizedTime>::success(std::move(value));
}

ApiResult<QStringList> parseStringList(const QJsonArray& array, const QString& fieldName)
{
    QStringList values;
    values.reserve(array.size());
    for (const QJsonValue& value : array) {
        if (!value.isString()) {
            return failure<QStringList>(QStringLiteral("invalid_field"), QStringLiteral("Expected string values in: %1").arg(fieldName));
        }
        values.append(value.toString());
    }
    return ApiResult<QStringList>::success(std::move(values));
}

ApiResult<EventSummaryDto> parseEventSummary(const QJsonObject& object)
{
    ApiError error;
    EventSummaryDto summary;
    QJsonObject eventTime;
    QString ocrStatus;
    qint64 speedKmh = 0;
    if (!assign(requiredInteger(object, "event_id"), &summary.eventId, &error)
        || !assign(requiredInteger(object, "track_id"), &summary.trackId, &error)
        || !assign(requiredObject(object, "event_time"), &eventTime, &error)
        || !assign(parseNormalizedTime(eventTime), &summary.eventTime, &error)
        || !assign(requiredString(object, "motion_direction"), &summary.motionDirection, &error)
        || !assign(requiredInteger(object, "speed_kmh"), &speedKmh, &error)
        || !assign(requiredBool(object, "speed_valid"), &summary.speedValid, &error)
        || !assign(requiredString(object, "speed_status"), &summary.speedStatus, &error)
        || !assign(requiredString(object, "ocr_status"), &ocrStatus, &error)
        || !assign(requiredString(object, "plate_text"), &summary.plateText, &error)
        || !assign(requiredString(object, "plate_ascii"), &summary.plateAscii, &error)
        || !assign(requiredString(object, "plate_color"), &summary.plateColor, &error)
        || !assign(requiredString(object, "evidence_status"), &summary.evidenceStatus, &error)
        || !assign(requiredBool(object, "evidence_available"), &summary.evidenceAvailable, &error)
        || !assign(requiredString(object, "detail_relative_url"), &summary.detailRelativeUrl, &error)
        || !assign(requiredString(object, "evidence_relative_url"), &summary.evidenceRelativeUrl, &error)) {
        return ApiResult<EventSummaryDto>::failure(error);
    }
    if (speedKmh < std::numeric_limits<int>::min() || speedKmh > std::numeric_limits<int>::max()) {
        return failure<EventSummaryDto>(QStringLiteral("invalid_field"), QStringLiteral("speed_kmh is outside int range."));
    }
    summary.speedKmh = static_cast<int>(speedKmh);
    summary.ocrStatus = ocrStatusFromWire(ocrStatus);
    summary.rawJson = object;
    return ApiResult<EventSummaryDto>::success(std::move(summary));
}

ApiResult<FtpTaskSummaryDto> parseFtpTaskSummary(const QJsonObject& object)
{
    ApiError error;
    FtpTaskSummaryDto summary;
    QString state;
    QJsonArray targetIds;
    if (!assign(requiredString(object, "task_id"), &summary.taskId, &error)
        || !assign(requiredString(object, "state"), &state, &error)
        || !assign(requiredInteger(object, "start_epoch_ms"), &summary.startEpochMs, &error)
        || !assign(requiredInteger(object, "end_epoch_ms"), &summary.endEpochMs, &error)
        || !assign(requiredInteger(object, "created_epoch_ms"), &summary.createdEpochMs, &error)
        || !assign(requiredArray(object, "target_ids"), &targetIds, &error)
        || !assign(parseStringList(targetIds, QStringLiteral("target_ids")), &summary.targetIds, &error)) {
        return ApiResult<FtpTaskSummaryDto>::failure(error);
    }
    summary.state = taskStateFromWire(state);
    summary.rawJson = object;
    return ApiResult<FtpTaskSummaryDto>::success(std::move(summary));
}

bool containsControlCharacter(const QString& text)
{
    for (const QChar character : text) {
        if (character.unicode() < 0x20 || character.unicode() == 0x7f) {
            return true;
        }
    }
    return false;
}

ApiResult<void> validateDisplayText(const QString& value, int maxBytes, const QString& field)
{
    if (value.toUtf8().size() > maxBytes || containsControlCharacter(value)) {
        return ApiResult<void>::failure(validationError(
            QStringLiteral("invalid_evidence_config"),
            QStringLiteral("Invalid display field: %1").arg(field)));
    }
    return ApiResult<void>::success();
}

ApiResult<void> validateSupportedEpoch(qint64 epochMs, const QString& field)
{
    if (epochMs < MinSupportedEpochMs || epochMs > MaxSupportedEpochMs) {
        return ApiResult<void>::failure(validationError(
            QStringLiteral("invalid_time"),
            QStringLiteral("Invalid UTC epoch field: %1").arg(field)));
    }
    return ApiResult<void>::success();
}

QByteArray compactJson(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

ApiError classifiedError(int httpStatus, const QString& code, const QString& message)
{
    ApiError error;
    error.httpStatus = httpStatus;
    error.code = code.isEmpty() ? QStringLiteral("http_error") : code;
    error.message = message;

    if (httpStatus == 401 || code == QStringLiteral("unauthorized")) {
        error.category = ApiErrorCategory::Authentication;
    } else if (httpStatus == 403 || code.endsWith(QStringLiteral("_disabled"))) {
        error.category = ApiErrorCategory::CapabilityDisabled;
    } else if (httpStatus == 409 && code == QStringLiteral("evidence_unavailable")) {
        error.category = ApiErrorCategory::Temporary;
        error.retryable = true;
    } else if (httpStatus == 409 || code == QStringLiteral("config_revision_conflict")) {
        error.category = ApiErrorCategory::Conflict;
    } else if (httpStatus == 404 || code.endsWith(QStringLiteral("_not_found")) || code == QStringLiteral("file_missing")) {
        error.category = ApiErrorCategory::NotFound;
    } else if (httpStatus >= 500) {
        error.category = ApiErrorCategory::Temporary;
        error.retryable = true;
    } else if (httpStatus == 400 || code.startsWith(QStringLiteral("invalid_"))) {
        error.category = ApiErrorCategory::Validation;
    } else {
        error.category = ApiErrorCategory::Unknown;
    }
    return error;
}

bool isStableErrorCode(const QString& code)
{
    if (code.isEmpty() || code.size() > 96) {
        return false;
    }
    for (const QChar character : code) {
        const ushort value = character.unicode();
        const bool isLowercaseLetter = value >= 'a' && value <= 'z';
        const bool isDigit = value >= '0' && value <= '9';
        if (!isLowercaseLetter && !isDigit && character != QLatin1Char('_')
            && character != QLatin1Char('-') && character != QLatin1Char('.')) {
            return false;
        }
    }
    return true;
}

QString safeHttpErrorMessage(int httpStatus)
{
    return QStringLiteral("Board request failed with HTTP status %1.").arg(httpStatus);
}

} // namespace

ApiResult<DiscoveredDeviceDto> BoardApiCodec::parseDiscoveryResponse(
    const QByteArray& payload,
    const QString& expectedNonce) const
{
    const auto parsed = parseObject(payload);
    if (!parsed.isSuccess()) return ApiResult<DiscoveredDeviceDto>::failure(parsed.error());

    const QJsonObject object = parsed.value();
    ApiError error;
    DiscoveredDeviceDto device;
    QString apiUrl;
    QJsonArray capabilities;
    qint64 version = 0;
    if (!assign(requiredString(object, "magic"), &device.magic, &error)
        || !assign(requiredInteger(object, "version"), &version, &error)
        || !assign(requiredString(object, "type"), &device.type, &error)
        || !assign(requiredString(object, "nonce"), &device.nonce, &error)
        || !assign(requiredString(object, "device_id"), &device.deviceId, &error)
        || !assign(requiredString(object, "device_model"), &device.deviceModel, &error)
        || !assign(requiredString(object, "ipv4"), &device.ipv4, &error)
        || !assign(requiredString(object, "api_version"), &device.apiVersion, &error)
        || !assign(requiredString(object, "api_url"), &apiUrl, &error)
        || !assign(requiredString(object, "release_version"), &device.releaseVersion, &error)
        || !assign(requiredBool(object, "auth_required"), &device.authRequired, &error)
        || !assign(requiredArray(object, "capabilities"), &capabilities, &error)
        || !assign(parseStringList(capabilities, QStringLiteral("capabilities")), &device.capabilities, &error)) {
        return ApiResult<DiscoveredDeviceDto>::failure(error);
    }

    device.version = static_cast<int>(version);
    device.apiUrl = QUrl(apiUrl);
    QHostAddress address;
    if (device.magic != QStringLiteral("RV1126B_DISCOVERY") || device.version != 1
        || device.type != QStringLiteral("discover_response") || device.nonce != expectedNonce
        || expectedNonce.isEmpty() || expectedNonce.toUtf8().size() > 64
        || !address.setAddress(device.ipv4) || address.protocol() != QAbstractSocket::IPv4Protocol
        || !device.apiUrl.isValid() || device.apiUrl.scheme() != QStringLiteral("http")
        || device.apiUrl.host().isEmpty() || !device.apiUrl.userInfo().isEmpty()
        || !device.apiUrl.path().startsWith(QStringLiteral("/api/v1"))) {
        return failure<DiscoveredDeviceDto>(QStringLiteral("invalid_discovery_response"), QStringLiteral("Discovery response failed v1 validation."));
    }
    device.rawJson = object;
    return ApiResult<DiscoveredDeviceDto>::success(std::move(device));
}

ApiError BoardApiCodec::parseError(int httpStatus, const QByteArray& payload) const
{
    const auto parsed = parseObject(payload);
    if (!parsed.isSuccess()) {
        return classifiedError(httpStatus, QStringLiteral("invalid_error_response"), QStringLiteral("Board returned an invalid error response."));
    }
    const QJsonValue errorValue = parsed.value().value(QStringLiteral("error"));
    if (!errorValue.isObject()) {
        return classifiedError(httpStatus, QStringLiteral("invalid_error_response"), QStringLiteral("Board returned an invalid error response."));
    }
    const QJsonObject errorObject = errorValue.toObject();
    const QJsonValue code = errorObject.value(QStringLiteral("code"));
    const QJsonValue message = errorObject.value(QStringLiteral("message"));
    if (!code.isString() || !message.isString()) {
        return classifiedError(httpStatus, QStringLiteral("invalid_error_response"), QStringLiteral("Board returned an invalid error response."));
    }
    Q_UNUSED(message);
    const QString errorCode = code.toString();
    return classifiedError(
        httpStatus,
        isStableErrorCode(errorCode) ? errorCode : QStringLiteral("http_error"),
        safeHttpErrorMessage(httpStatus));
}

ApiResult<HealthDto> BoardApiCodec::parseHealth(const QByteArray& payload) const
{
    const auto parsed = parseObject(payload);
    if (!parsed.isSuccess()) return ApiResult<HealthDto>::failure(parsed.error());
    const QJsonObject object = parsed.value();
    ApiError error;
    HealthDto health;
    QJsonObject serverTime;
    QJsonObject applicationApi;
    qint64 httpPort = 0;
    qint64 discoveryPort = 0;
    if (!requireApiVersion(object, &health.apiVersion, &error)
        || !assign(requiredString(object, "device_id"), &health.deviceId, &error)
        || !assign(requiredString(object, "device_model"), &health.deviceModel, &error)
        || !assign(requiredString(object, "release_version"), &health.releaseVersion, &error)
        || !assign(requiredObject(object, "server_time"), &serverTime, &error)
        || !assign(parseNormalizedTime(serverTime), &health.serverTime, &error)
        || !assign(requiredBool(object, "pipeline_health_available"), &health.pipelineHealthAvailable, &error)
        || !assign(requiredObject(object, "pipeline"), &health.pipeline, &error)
        || !assign(requiredObject(object, "application_api"), &applicationApi, &error)
        || !assign(requiredBool(applicationApi, "alive"), &health.applicationApi.alive, &error)
        || !assign(requiredInteger(applicationApi, "http_port"), &httpPort, &error)
        || !assign(requiredInteger(applicationApi, "discovery_port"), &discoveryPort, &error)
        || !assign(requiredBool(applicationApi, "auth_required"), &health.applicationApi.authRequired, &error)) {
        return ApiResult<HealthDto>::failure(error);
    }
    if (httpPort < 1 || httpPort > 65535 || discoveryPort < 1 || discoveryPort > 65535) {
        return failure<HealthDto>(QStringLiteral("invalid_field"), QStringLiteral("Invalid application API port."));
    }
    health.applicationApi.httpPort = static_cast<quint16>(httpPort);
    health.applicationApi.discoveryPort = static_cast<quint16>(discoveryPort);
    health.rawJson = object;
    return ApiResult<HealthDto>::success(std::move(health));
}

ApiResult<EventPageDto> BoardApiCodec::parseEventPage(const QByteArray& payload) const
{
    const auto parsed = parseObject(payload);
    if (!parsed.isSuccess()) return ApiResult<EventPageDto>::failure(parsed.error());
    const QJsonObject object = parsed.value();
    ApiError error;
    EventPageDto page;
    QJsonArray items;
    qint64 count = 0;
    if (!requireApiVersion(object, &page.apiVersion, &error)
        || !assign(requiredArray(object, "items"), &items, &error)
        || !assign(requiredInteger(object, "count"), &count, &error)
        || !assign(requiredBool(object, "has_more"), &page.hasMore, &error)) {
        return ApiResult<EventPageDto>::failure(error);
    }
    const QJsonValue cursor = object.value(QStringLiteral("next_cursor"));
    if (!cursor.isNull() && !cursor.isString()) {
        return failure<EventPageDto>(QStringLiteral("invalid_field"), QStringLiteral("Expected string or null next_cursor."));
    }
    if (cursor.isString()) page.nextCursor = cursor.toString();
    if (count < 0 || count != items.size()) {
        return failure<EventPageDto>(QStringLiteral("invalid_field"), QStringLiteral("Event page count does not match items."));
    }
    page.count = static_cast<int>(count);
    page.items.reserve(items.size());
    for (const QJsonValue& item : items) {
        if (!item.isObject()) return failure<EventPageDto>(QStringLiteral("invalid_field"), QStringLiteral("Event item must be an object."));
        const auto summary = parseEventSummary(item.toObject());
        if (!summary.isSuccess()) return ApiResult<EventPageDto>::failure(summary.error());
        page.items.append(summary.value());
    }
    page.rawJson = object;
    return ApiResult<EventPageDto>::success(std::move(page));
}

ApiResult<EventDetailDto> BoardApiCodec::parseEventDetail(const QByteArray& payload) const
{
    const auto parsed = parseObject(payload);
    if (!parsed.isSuccess()) return ApiResult<EventDetailDto>::failure(parsed.error());
    const QJsonObject object = parsed.value();
    QJsonObject summaryObject = object;
    const QJsonValue evidence = object.value(QStringLiteral("evidence_relative_url"));
    if (evidence.isUndefined() || evidence.isNull()) {
        summaryObject.insert(QStringLiteral("evidence_relative_url"), QString());
    }
    const auto summary = parseEventSummary(summaryObject);
    if (!summary.isSuccess()) return ApiResult<EventDetailDto>::failure(summary.error());
    ApiError error;
    EventDetailDto detail;
    detail.summary = summary.value();
    if (!assign(requiredString(object, "trigger_mode"), &detail.triggerMode, &error)
        || !assign(requiredString(object, "capture_reason"), &detail.captureReason, &error)
        || !assign(requiredObject(object, "vehicle"), &detail.vehicle, &error)
        || !assign(requiredObject(object, "line_region"), &detail.lineRegion, &error)
        || !assign(requiredObject(object, "radar"), &detail.radar, &error)
        || !assign(requiredObject(object, "ocr"), &detail.ocr, &error)
        || !assign(requiredObject(object, "images"), &detail.images, &error)) {
        return ApiResult<EventDetailDto>::failure(error);
    }
    if (!evidence.isUndefined() && !evidence.isNull() && !evidence.isString()) {
        return failure<EventDetailDto>(QStringLiteral("invalid_field"), QStringLiteral("Expected string or null evidence_relative_url."));
    }
    if (evidence.isString()) detail.evidenceRelativeUrl = evidence.toString();
    detail.rawJson = object;
    return ApiResult<EventDetailDto>::success(std::move(detail));
}

ApiResult<EvidenceConfigDto> BoardApiCodec::parseEvidenceConfig(const QByteArray& payload) const
{
    const auto parsed = parseObject(payload);
    if (!parsed.isSuccess()) return ApiResult<EvidenceConfigDto>::failure(parsed.error());
    const QJsonObject object = parsed.value();
    ApiError error;
    EvidenceConfigDto config;
    QJsonObject evidence;
    qint64 speedLimit = 0;
    if (!requireApiVersion(object, &config.apiVersion, &error)
        || !assign(requiredObject(object, "evidence"), &evidence, &error)
        || !assign(requiredString(evidence, "site_name"), &config.evidence.siteName, &error)
        || !assign(requiredString(evidence, "road_direction"), &config.evidence.roadDirection, &error)
        || !assign(requiredInteger(evidence, "speed_limit_kmh"), &speedLimit, &error)
        || !assign(requiredString(evidence, "status_text"), &config.evidence.statusText, &error)
        || !assign(requiredString(evidence, "code_text"), &config.evidence.codeText, &error)
        || !assign(requiredBool(object, "restart_required"), &config.restartRequired, &error)
        || !assign(requiredString(object, "apply_mode"), &config.applyMode, &error)
        || !assign(requiredString(object, "effective_scope"), &config.effectiveScope, &error)
        || !assign(requiredBool(object, "existing_events_unchanged"), &config.existingEventsUnchanged, &error)) {
        return ApiResult<EvidenceConfigDto>::failure(error);
    }
    if (speedLimit < 0 || speedLimit > 300) return failure<EvidenceConfigDto>(QStringLiteral("invalid_field"), QStringLiteral("Invalid speed_limit_kmh."));
    config.evidence.speedLimitKmh = static_cast<int>(speedLimit);
    config.rawJson = object;
    return ApiResult<EvidenceConfigDto>::success(std::move(config));
}

ApiResult<TimeStatusDto> BoardApiCodec::parseTimeStatus(const QByteArray& payload) const
{
    const auto parsed = parseObject(payload);
    if (!parsed.isSuccess()) return ApiResult<TimeStatusDto>::failure(parsed.error());
    const QJsonObject object = parsed.value();
    ApiError error;
    TimeStatusDto status;
    QJsonObject time;
    if (!requireApiVersion(object, &status.apiVersion, &error)
        || !assign(requiredObject(object, "time"), &time, &error)
        || !assign(parseNormalizedTime(time), &status.time, &error)
        || !assign(requiredString(object, "timezone_contract"), &status.timezoneContract, &error)
        || !assign(requiredString(object, "ntp_status"), &status.ntpStatus, &error)
        || !assign(requiredBool(object, "time_set_enabled"), &status.timeSetEnabled, &error)) {
        return ApiResult<TimeStatusDto>::failure(error);
    }
    status.rawJson = object;
    return ApiResult<TimeStatusDto>::success(std::move(status));
}

ApiResult<FtpConfigSnapshotDto> BoardApiCodec::parseFtpConfig(const QByteArray& payload) const
{
    const auto parsed = parseObject(payload);
    if (!parsed.isSuccess()) return ApiResult<FtpConfigSnapshotDto>::failure(parsed.error());
    const QJsonObject object = parsed.value();
    ApiError error;
    FtpConfigSnapshotDto config;
    QJsonArray targets;
    qint64 retryMax = 0, retryInterval = 0, connectTimeout = 0, transferTimeout = 0, scanInterval = 0;
    if (!requireApiVersion(object, &config.apiVersion, &error)
        || !assign(requiredString(object, "revision"), &config.revision, &error)
        || !assign(requiredString(object, "device_id"), &config.deviceId, &error)
        || !assign(requiredInteger(object, "retry_max"), &retryMax, &error)
        || !assign(requiredInteger(object, "retry_interval_sec"), &retryInterval, &error)
        || !assign(requiredInteger(object, "connect_timeout_sec"), &connectTimeout, &error)
        || !assign(requiredInteger(object, "transfer_timeout_sec"), &transferTimeout, &error)
        || !assign(requiredInteger(object, "scan_interval_sec"), &scanInterval, &error)
        || !assign(requiredArray(object, "targets"), &targets, &error)
        || !assign(requiredBool(object, "restart_required"), &config.restartRequired, &error)) {
        return ApiResult<FtpConfigSnapshotDto>::failure(error);
    }
    if (targets.size() > 8 || retryMax < 0 || retryInterval < 0 || connectTimeout < 0 || transferTimeout < 0 || scanInterval < 0) {
        return failure<FtpConfigSnapshotDto>(QStringLiteral("invalid_field"), QStringLiteral("Invalid FTP config limits."));
    }
    config.retryMax = static_cast<int>(retryMax);
    config.retryIntervalSec = static_cast<int>(retryInterval);
    config.connectTimeoutSec = static_cast<int>(connectTimeout);
    config.transferTimeoutSec = static_cast<int>(transferTimeout);
    config.scanIntervalSec = static_cast<int>(scanInterval);
    config.targets.reserve(targets.size());
    for (const QJsonValue& value : targets) {
        if (!value.isObject()) return failure<FtpConfigSnapshotDto>(QStringLiteral("invalid_field"), QStringLiteral("FTP target must be an object."));
        const QJsonObject targetObject = value.toObject();
        FtpTargetSnapshotDto target;
        qint64 port = 0;
        if (!assign(requiredString(targetObject, "id"), &target.id, &error)
            || !assign(requiredBool(targetObject, "enabled"), &target.enabled, &error)
            || !assign(requiredString(targetObject, "host"), &target.host, &error)
            || !assign(requiredInteger(targetObject, "port"), &port, &error)
            || !assign(requiredString(targetObject, "user"), &target.user, &error)
            || !assign(requiredString(targetObject, "remote_dir"), &target.remoteDir, &error)
            || !assign(requiredBool(targetObject, "passive"), &target.passive, &error)
            || !assign(requiredBool(targetObject, "password_configured"), &target.passwordConfigured, &error)) {
            return ApiResult<FtpConfigSnapshotDto>::failure(error);
        }
        if (port < 1 || port > 65535) return failure<FtpConfigSnapshotDto>(QStringLiteral("invalid_field"), QStringLiteral("Invalid FTP target port."));
        target.port = static_cast<quint16>(port);
        config.targets.append(std::move(target));
    }
    config.rawJson = object;
    return ApiResult<FtpConfigSnapshotDto>::success(std::move(config));
}

ApiResult<FtpControlDto> BoardApiCodec::parseFtpControl(const QByteArray& payload) const
{
    const auto parsed = parseObject(payload);
    if (!parsed.isSuccess()) return ApiResult<FtpControlDto>::failure(parsed.error());
    const QJsonObject object = parsed.value();
    ApiError error;
    FtpControlDto control;
    QString scope;
    if (!requireApiVersion(object, &control.apiVersion, &error)
        || !assign(requiredString(object, "revision"), &control.revision, &error)
        || !assign(requiredBool(object, "enabled"), &control.enabled, &error)
        || !assign(requiredString(object, "scope"), &scope, &error)) {
        return ApiResult<FtpControlDto>::failure(error);
    }
    control.scope = controlScopeFromWire(scope);
    control.rawJson = object;
    return ApiResult<FtpControlDto>::success(std::move(control));
}

ApiResult<FtpTaskPageDto> BoardApiCodec::parseFtpTaskPage(const QByteArray& payload) const
{
    const auto parsed = parseObject(payload);
    if (!parsed.isSuccess()) return ApiResult<FtpTaskPageDto>::failure(parsed.error());
    const QJsonObject object = parsed.value();
    ApiError error;
    FtpTaskPageDto page;
    QJsonArray items;
    qint64 count = 0;
    if (!requireApiVersion(object, &page.apiVersion, &error)
        || !assign(requiredArray(object, "items"), &items, &error)
        || !assign(requiredInteger(object, "count"), &count, &error)
        || !assign(requiredBool(object, "has_more"), &page.hasMore, &error)) {
        return ApiResult<FtpTaskPageDto>::failure(error);
    }
    const QJsonValue cursor = object.value(QStringLiteral("next_cursor"));
    if (!cursor.isNull() && !cursor.isString()) return failure<FtpTaskPageDto>(QStringLiteral("invalid_field"), QStringLiteral("Expected string or null next_cursor."));
    if (cursor.isString()) page.nextCursor = cursor.toString();
    if (count < 0 || count != items.size()) return failure<FtpTaskPageDto>(QStringLiteral("invalid_field"), QStringLiteral("FTP task count does not match items."));
    page.count = static_cast<int>(count);
    page.items.reserve(items.size());
    for (const QJsonValue& item : items) {
        if (!item.isObject()) return failure<FtpTaskPageDto>(QStringLiteral("invalid_field"), QStringLiteral("FTP task item must be an object."));
        const auto task = parseFtpTaskSummary(item.toObject());
        if (!task.isSuccess()) return ApiResult<FtpTaskPageDto>::failure(task.error());
        page.items.append(task.value());
    }
    page.rawJson = object;
    return ApiResult<FtpTaskPageDto>::success(std::move(page));
}

ApiResult<FtpTaskDetailDto> BoardApiCodec::parseFtpTaskDetail(const QByteArray& payload) const
{
    const auto parsed = parseObject(payload);
    if (!parsed.isSuccess()) return ApiResult<FtpTaskDetailDto>::failure(parsed.error());
    const QJsonObject object = parsed.value();
    const auto summary = parseFtpTaskSummary(object);
    if (!summary.isSuccess()) return ApiResult<FtpTaskDetailDto>::failure(summary.error());
    ApiError error;
    QJsonArray targets;
    if (!assign(requiredArray(object, "targets"), &targets, &error)) return ApiResult<FtpTaskDetailDto>::failure(error);
    FtpTaskDetailDto detail;
    detail.summary = summary.value();
    detail.targets.reserve(targets.size());
    for (const QJsonValue& value : targets) {
        if (!value.isObject()) return failure<FtpTaskDetailDto>(QStringLiteral("invalid_field"), QStringLiteral("FTP task target must be an object."));
        const QJsonObject targetObject = value.toObject();
        FtpTaskTargetStatusDto target;
        QString state;
        qint64 total = 0, pending = 0, uploading = 0, done = 0, failed = 0, attempts = 0;
        if (!assign(requiredString(targetObject, "target_id"), &target.targetId, &error)
            || !assign(requiredString(targetObject, "state"), &state, &error)
            || !assign(requiredInteger(targetObject, "total"), &total, &error)
            || !assign(requiredInteger(targetObject, "pending"), &pending, &error)
            || !assign(requiredInteger(targetObject, "uploading"), &uploading, &error)
            || !assign(requiredInteger(targetObject, "done"), &done, &error)
            || !assign(requiredInteger(targetObject, "failed"), &failed, &error)
            || !assign(requiredInteger(targetObject, "attempts"), &attempts, &error)
            || !assign(requiredString(targetObject, "last_error"), &target.lastError, &error)) {
            return ApiResult<FtpTaskDetailDto>::failure(error);
        }
        if (total < 0 || pending < 0 || uploading < 0 || done < 0 || failed < 0 || attempts < 0
            || total > std::numeric_limits<int>::max() || pending > std::numeric_limits<int>::max()
            || uploading > std::numeric_limits<int>::max() || done > std::numeric_limits<int>::max()
            || failed > std::numeric_limits<int>::max() || attempts > std::numeric_limits<int>::max()) {
            return failure<FtpTaskDetailDto>(QStringLiteral("invalid_field"), QStringLiteral("Invalid FTP task counters."));
        }
        target.state = taskStateFromWire(state);
        target.total = static_cast<int>(total);
        target.pending = static_cast<int>(pending);
        target.uploading = static_cast<int>(uploading);
        target.done = static_cast<int>(done);
        target.failed = static_cast<int>(failed);
        target.attempts = static_cast<int>(attempts);
        detail.targets.append(std::move(target));
    }
    detail.rawJson = object;
    return ApiResult<FtpTaskDetailDto>::success(std::move(detail));
}

ApiResult<QByteArray> BoardApiCodec::encodeEvidenceConfig(const EvidenceConfigUpdate& update) const
{
    for (const auto& field : {std::pair<QString, int> {update.siteName, 128}, {update.roadDirection, 64}, {update.statusText, 64}, {update.codeText, 128}}) {
        const auto valid = validateDisplayText(field.first, field.second, QStringLiteral("evidence"));
        if (!valid.isSuccess()) return ApiResult<QByteArray>::failure(valid.error());
    }
    if (update.speedLimitKmh < 0 || update.speedLimitKmh > 300) {
        return ApiResult<QByteArray>::failure(validationError(QStringLiteral("invalid_evidence_config"), QStringLiteral("Invalid speed limit.")));
    }
    QJsonObject object;
    object.insert(QStringLiteral("site_name"), update.siteName);
    object.insert(QStringLiteral("road_direction"), update.roadDirection);
    object.insert(QStringLiteral("speed_limit_kmh"), update.speedLimitKmh);
    object.insert(QStringLiteral("status_text"), update.statusText);
    object.insert(QStringLiteral("code_text"), update.codeText);
    return ApiResult<QByteArray>::success(compactJson(object));
}

ApiResult<QByteArray> BoardApiCodec::encodeTimeUpdate(const TimeUpdate& update) const
{
    const auto valid = validateSupportedEpoch(update.utcEpochMs, QStringLiteral("utc_epoch_ms"));
    if (!valid.isSuccess()) return ApiResult<QByteArray>::failure(valid.error());
    return ApiResult<QByteArray>::success(compactJson(QJsonObject {{QStringLiteral("utc_epoch_ms"), update.utcEpochMs}}));
}

ApiResult<QByteArray> BoardApiCodec::encodeFtpConfig(const FtpConfigUpdate& update) const
{
    if (update.expectedRevision.isEmpty() || update.deviceId.isEmpty() || update.targets.size() > 8
        || update.retryMax < 0 || update.retryIntervalSec < 0 || update.connectTimeoutSec < 0
        || update.transferTimeoutSec < 0 || update.scanIntervalSec < 0) {
        return ApiResult<QByteArray>::failure(validationError(QStringLiteral("invalid_ftp_config"), QStringLiteral("Invalid FTP config header.")));
    }
    QSet<QString> ids;
    QJsonArray targets;
    for (const FtpTargetUpdate& target : update.targets) {
        QHostAddress host;
        if (target.id.isEmpty() || ids.contains(target.id) || !host.setAddress(target.host)
            || host.protocol() != QAbstractSocket::IPv4Protocol || target.port == 0
            || target.passwordAction.value == FtpPasswordAction::Unknown
            || (target.passwordAction.value == FtpPasswordAction::Replace && (!target.replacementPassword || target.replacementPassword->isEmpty()))
            || (target.passwordAction.value != FtpPasswordAction::Replace && target.replacementPassword.has_value())) {
            return ApiResult<QByteArray>::failure(validationError(QStringLiteral("invalid_ftp_config"), QStringLiteral("Invalid FTP target.")));
        }
        ids.insert(target.id);
        QJsonObject object;
        object.insert(QStringLiteral("id"), target.id);
        object.insert(QStringLiteral("enabled"), target.enabled);
        object.insert(QStringLiteral("host"), target.host);
        object.insert(QStringLiteral("port"), target.port);
        object.insert(QStringLiteral("user"), target.user);
        object.insert(QStringLiteral("password_action"), target.passwordAction.rawValue);
        if (target.passwordAction.value == FtpPasswordAction::Replace) object.insert(QStringLiteral("password"), *target.replacementPassword);
        object.insert(QStringLiteral("remote_dir"), target.remoteDir);
        object.insert(QStringLiteral("passive"), target.passive);
        targets.append(object);
    }
    QJsonObject object;
    object.insert(QStringLiteral("expected_revision"), update.expectedRevision);
    object.insert(QStringLiteral("device_id"), update.deviceId);
    object.insert(QStringLiteral("retry_max"), update.retryMax);
    object.insert(QStringLiteral("retry_interval_sec"), update.retryIntervalSec);
    object.insert(QStringLiteral("connect_timeout_sec"), update.connectTimeoutSec);
    object.insert(QStringLiteral("transfer_timeout_sec"), update.transferTimeoutSec);
    object.insert(QStringLiteral("scan_interval_sec"), update.scanIntervalSec);
    object.insert(QStringLiteral("targets"), targets);
    return ApiResult<QByteArray>::success(compactJson(object));
}

ApiResult<QByteArray> BoardApiCodec::encodeFtpControl(const FtpControlUpdate& update) const
{
    if (update.expectedRevision.isEmpty() || update.scope.value == FtpControlScope::Unknown
        || (update.enabled && update.scope.value == FtpControlScope::Preserve)
        || (!update.enabled && update.scope.value != FtpControlScope::Preserve)) {
        return ApiResult<QByteArray>::failure(validationError(QStringLiteral("invalid_ftp_control"), QStringLiteral("Invalid FTP control update.")));
    }
    QJsonObject object;
    object.insert(QStringLiteral("expected_revision"), update.expectedRevision);
    object.insert(QStringLiteral("enabled"), update.enabled);
    object.insert(QStringLiteral("scope"), update.scope.rawValue);
    return ApiResult<QByteArray>::success(compactJson(object));
}

ApiResult<QByteArray> BoardApiCodec::encodeFtpTaskCreate(const FtpTaskCreate& request) const
{
    const auto startValid = validateSupportedEpoch(request.startEpochMs, QStringLiteral("start_epoch_ms"));
    const auto endValid = validateSupportedEpoch(request.endEpochMs, QStringLiteral("end_epoch_ms"));
    if (!startValid.isSuccess()) return ApiResult<QByteArray>::failure(startValid.error());
    if (!endValid.isSuccess()) return ApiResult<QByteArray>::failure(endValid.error());
    if (request.endEpochMs <= request.startEpochMs || request.endEpochMs - request.startEpochMs > MaxTaskSpanMs || request.targetIds.isEmpty()) {
        return ApiResult<QByteArray>::failure(validationError(QStringLiteral("invalid_ftp_task"), QStringLiteral("Invalid FTP task range or targets.")));
    }
    QSet<QString> ids;
    QJsonArray targetIds;
    for (const QString& id : request.targetIds) {
        if (id.isEmpty() || ids.contains(id)) return ApiResult<QByteArray>::failure(validationError(QStringLiteral("invalid_ftp_task"), QStringLiteral("Invalid FTP target ids.")));
        ids.insert(id);
        targetIds.append(id);
    }
    QJsonObject object;
    object.insert(QStringLiteral("start_epoch_ms"), request.startEpochMs);
    object.insert(QStringLiteral("end_epoch_ms"), request.endEpochMs);
    object.insert(QStringLiteral("target_ids"), targetIds);
    return ApiResult<QByteArray>::success(compactJson(object));
}

ApiResult<QByteArray> BoardApiCodec::encodeExpectedRevision(const QString& expectedRevision) const
{
    if (expectedRevision.isEmpty()) {
        return ApiResult<QByteArray>::failure(validationError(QStringLiteral("invalid_ftp_config"), QStringLiteral("Expected revision is required.")));
    }
    return ApiResult<QByteArray>::success(compactJson(QJsonObject {{QStringLiteral("expected_revision"), expectedRevision}}));
}

} // namespace rv1126b
