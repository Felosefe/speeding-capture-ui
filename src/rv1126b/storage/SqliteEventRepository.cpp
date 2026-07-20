#include "SqliteEventRepository.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace rv1126b {
namespace {

constexpr int SchemaVersion = 1;

QString defaultDatabasePath()
{
#ifdef CAMERA_MANAGER_SOURCE_DIR
    QDir root(QString::fromUtf8(CAMERA_MANAGER_SOURCE_DIR));
#else
    QDir root(QCoreApplication::applicationDirPath());
#endif
    return root.filePath(QStringLiteral("data/rv1126b_events.sqlite"));
}

QString compactJson(const QJsonObject& object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QJsonObject objectFromJson(const QString& text)
{
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
    return document.isObject() ? document.object() : QJsonObject {};
}

QString stringListToJson(const QStringList& values)
{
    QJsonArray array;
    for (const QString& value : values) {
        array.append(value);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QStringList stringListFromJson(const QString& text)
{
    QStringList values;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
    if (!document.isArray()) {
        return values;
    }

    const QJsonArray array = document.array();
    values.reserve(array.size());
    for (const QJsonValue& value : array) {
        values.append(value.toString());
    }
    return values;
}

template<typename Enum>
int enumValue(const WireEnum<Enum>& value)
{
    return static_cast<int>(value.value);
}

template<typename Enum>
WireEnum<Enum> wireEnumFromStorage(const QVariant& value, const QVariant& rawValue)
{
    WireEnum<Enum> result;
    result.value = static_cast<Enum>(value.toInt());
    result.rawValue = rawValue.toString();
    return result;
}

QString identityWhereClause()
{
    return QStringLiteral("device_id = :device_id AND event_id = :event_id AND track_id = :track_id");
}

void bindIdentity(QSqlQuery& query, const EventIdentity& identity)
{
    query.bindValue(QStringLiteral(":device_id"), identity.deviceId);
    query.bindValue(QStringLiteral(":event_id"), identity.eventId);
    query.bindValue(QStringLiteral(":track_id"), identity.trackId);
}

bool isNonTerminalOcrStatus(OcrStatus status)
{
    return status == OcrStatus::Queued || status == OcrStatus::Unknown;
}

QString sqlErrorText(const QSqlQuery& query)
{
    return query.lastError().text();
}

QString sqlErrorText(const QSqlDatabase& database)
{
    return database.lastError().text();
}

} // namespace

SqliteEventRepository::SqliteEventRepository(QObject* parent)
    : SqliteEventRepository(defaultDatabasePath(), parent)
{
}

SqliteEventRepository::SqliteEventRepository(const QString& databasePath, QObject* parent)
    : IEventRepository(parent)
    , databasePath_(databasePath)
    , connectionName_(QStringLiteral("rv1126b-events-%1").arg(reinterpret_cast<quintptr>(this)))
{
}

SqliteEventRepository::~SqliteEventRepository()
{
    if (database_.isValid()) {
        database_.close();
    }
    database_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName_);
}

RequestId SqliteEventRepository::initialize(QObject* context, ApiCompletion<void> completion)
{
    QString errorMessage;
    if (!openDatabase(&errorMessage) || !ensureSchema(&errorMessage)) {
        return finish<void>(context, std::move(completion), ApiResult<void>::failure(storageError(errorMessage)));
    }
    return finish<void>(context, std::move(completion), ApiResult<void>::success());
}

RequestId SqliteEventRepository::upsertDevice(
    const DeviceProfile& profile,
    QObject* context,
    ApiCompletion<void> completion)
{
    QString errorMessage;
    if (!openDatabase(&errorMessage)
        || !beginTransaction(&errorMessage)
        || !upsertDeviceInternal(profile, &errorMessage)
        || !commitTransaction(&errorMessage)) {
        rollbackTransaction();
        return finish<void>(context, std::move(completion), ApiResult<void>::failure(storageError(errorMessage)));
    }
    return finish<void>(context, std::move(completion), ApiResult<void>::success());
}

RequestId SqliteEventRepository::loadDeviceProfiles(
    QObject* context,
    ApiCompletion<QVector<DeviceProfile>> completion)
{
    QString errorMessage;
    if (!openDatabase(&errorMessage)) {
        return finish<QVector<DeviceProfile>>(
            context,
            std::move(completion),
            ApiResult<QVector<DeviceProfile>>::failure(storageError(errorMessage)));
    }

    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "SELECT device_id, device_model, release_version, ipv4, api_base_url, http_port, "
            "discovery_port, credential_ref, advertised_capabilities_json, last_online_epoch_ms "
            "FROM rv_devices ORDER BY device_id ASC"))) {
        return finish<QVector<DeviceProfile>>(
            context,
            std::move(completion),
            ApiResult<QVector<DeviceProfile>>::failure(storageError(sqlErrorText(query))));
    }

    QVector<DeviceProfile> profiles;
    while (query.next()) {
        profiles.append(deviceProfileFromQuery(query));
    }

    return finish<QVector<DeviceProfile>>(
        context,
        std::move(completion),
        ApiResult<QVector<DeviceProfile>>::success(std::move(profiles)));
}

RequestId SqliteEventRepository::upsertEvents(
    const QVector<VehicleEvent>& events,
    QObject* context,
    ApiCompletion<void> completion)
{
    QString errorMessage;
    if (!openDatabase(&errorMessage) || !beginTransaction(&errorMessage)) {
        rollbackTransaction();
        return finish<void>(context, std::move(completion), ApiResult<void>::failure(storageError(errorMessage)));
    }

    for (const VehicleEvent& event : events) {
        if (!upsertEventInternal(event, &errorMessage)) {
            rollbackTransaction();
            return finish<void>(context, std::move(completion), ApiResult<void>::failure(storageError(errorMessage)));
        }
    }

    if (!commitTransaction(&errorMessage)) {
        rollbackTransaction();
        return finish<void>(context, std::move(completion), ApiResult<void>::failure(storageError(errorMessage)));
    }

    return finish<void>(context, std::move(completion), ApiResult<void>::success());
}

RequestId SqliteEventRepository::saveDetail(
    const EventDetailSnapshot& detail,
    QObject* context,
    ApiCompletion<void> completion)
{
    QString errorMessage;
    if (!openDatabase(&errorMessage)) {
        return finish<void>(context, std::move(completion), ApiResult<void>::failure(storageError(errorMessage)));
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO rv_event_details ("
        "device_id, event_id, track_id, trigger_mode, capture_reason, vehicle_json, line_region_json, "
        "radar_json, ocr_json, images_json, raw_json, fetched_epoch_ms"
        ") VALUES ("
        ":device_id, :event_id, :track_id, :trigger_mode, :capture_reason, :vehicle_json, :line_region_json, "
        ":radar_json, :ocr_json, :images_json, :raw_json, :fetched_epoch_ms"
        ") ON CONFLICT(device_id, event_id, track_id) DO UPDATE SET "
        "trigger_mode = excluded.trigger_mode, capture_reason = excluded.capture_reason, "
        "vehicle_json = excluded.vehicle_json, line_region_json = excluded.line_region_json, "
        "radar_json = excluded.radar_json, ocr_json = excluded.ocr_json, images_json = excluded.images_json, "
        "raw_json = excluded.raw_json, fetched_epoch_ms = excluded.fetched_epoch_ms"));
    bindIdentity(query, detail.identity);
    query.bindValue(QStringLiteral(":trigger_mode"), detail.triggerMode);
    query.bindValue(QStringLiteral(":capture_reason"), detail.captureReason);
    query.bindValue(QStringLiteral(":vehicle_json"), compactJson(detail.vehicle));
    query.bindValue(QStringLiteral(":line_region_json"), compactJson(detail.lineRegion));
    query.bindValue(QStringLiteral(":radar_json"), compactJson(detail.radar));
    query.bindValue(QStringLiteral(":ocr_json"), compactJson(detail.ocr));
    query.bindValue(QStringLiteral(":images_json"), compactJson(detail.images));
    query.bindValue(QStringLiteral(":raw_json"), compactJson(detail.rawJson));
    query.bindValue(QStringLiteral(":fetched_epoch_ms"), detail.fetchedEpochMs);

    if (!query.exec()) {
        return finish<void>(context, std::move(completion), ApiResult<void>::failure(storageError(sqlErrorText(query))));
    }

    return finish<void>(context, std::move(completion), ApiResult<void>::success());
}

RequestId SqliteEventRepository::queryEvents(
    const EventQuery& eventQuery,
    QObject* context,
    ApiCompletion<QVector<VehicleEvent>> completion)
{
    QString errorMessage;
    if (!openDatabase(&errorMessage)) {
        return finish<QVector<VehicleEvent>>(
            context,
            std::move(completion),
            ApiResult<QVector<VehicleEvent>>::failure(storageError(errorMessage)));
    }

    QStringList where;
    if (eventQuery.deviceId) {
        where.append(QStringLiteral("device_id = :query_device_id"));
    }
    if (eventQuery.plateText) {
        where.append(QStringLiteral("plate_text LIKE :plate_text"));
    }
    if (eventQuery.startEpochMs) {
        where.append(QStringLiteral("event_epoch_ms >= :start_epoch_ms"));
    }
    if (eventQuery.endEpochMs) {
        where.append(QStringLiteral("event_epoch_ms < :end_epoch_ms"));
    }

    const QString orderDirection = eventQuery.newestFirst ? QStringLiteral("DESC") : QStringLiteral("ASC");
    QString sql = QStringLiteral(
        "SELECT device_id, event_id, track_id, event_epoch_ms, source_epoch_ms, offset_applied_ms, "
        "time_quality_value, time_quality_raw, motion_direction, speed_kmh, speed_valid, speed_status, "
        "ocr_status_value, ocr_status_raw, plate_text, plate_ascii, plate_color, evidence_status, "
        "evidence_available, detail_relative_url, evidence_relative_url, first_seen_epoch_ms, last_updated_epoch_ms "
        "FROM rv_events");
    if (!where.isEmpty()) {
        sql += QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND "));
    }
    sql += QStringLiteral(" ORDER BY event_epoch_ms %1, event_id %1, track_id %1 LIMIT :limit OFFSET :offset")
               .arg(orderDirection);

    QSqlQuery query(database_);
    query.prepare(sql);
    if (eventQuery.deviceId) {
        query.bindValue(QStringLiteral(":query_device_id"), *eventQuery.deviceId);
    }
    if (eventQuery.plateText) {
        query.bindValue(QStringLiteral(":plate_text"), QStringLiteral("%%1%").arg(*eventQuery.plateText));
    }
    if (eventQuery.startEpochMs) {
        query.bindValue(QStringLiteral(":start_epoch_ms"), *eventQuery.startEpochMs);
    }
    if (eventQuery.endEpochMs) {
        query.bindValue(QStringLiteral(":end_epoch_ms"), *eventQuery.endEpochMs);
    }
    query.bindValue(QStringLiteral(":limit"), eventQuery.limit);
    query.bindValue(QStringLiteral(":offset"), eventQuery.offset);

    if (!query.exec()) {
        return finish<QVector<VehicleEvent>>(
            context,
            std::move(completion),
            ApiResult<QVector<VehicleEvent>>::failure(storageError(sqlErrorText(query))));
    }

    QVector<VehicleEvent> events;
    while (query.next()) {
        events.append(eventFromQuery(query));
    }

    return finish<QVector<VehicleEvent>>(
        context,
        std::move(completion),
        ApiResult<QVector<VehicleEvent>>::success(std::move(events)));
}

RequestId SqliteEventRepository::loadEvent(
    const EventIdentity& identity,
    QObject* context,
    ApiCompletion<std::optional<VehicleEvent>> completion)
{
    QString errorMessage;
    if (!openDatabase(&errorMessage)) {
        return finish<std::optional<VehicleEvent>>(
            context,
            std::move(completion),
            ApiResult<std::optional<VehicleEvent>>::failure(storageError(errorMessage)));
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT device_id, event_id, track_id, event_epoch_ms, source_epoch_ms, offset_applied_ms, "
        "time_quality_value, time_quality_raw, motion_direction, speed_kmh, speed_valid, speed_status, "
        "ocr_status_value, ocr_status_raw, plate_text, plate_ascii, plate_color, evidence_status, "
        "evidence_available, detail_relative_url, evidence_relative_url, first_seen_epoch_ms, last_updated_epoch_ms "
        "FROM rv_events WHERE %1").arg(identityWhereClause()));
    bindIdentity(query, identity);

    if (!query.exec()) {
        return finish<std::optional<VehicleEvent>>(
            context,
            std::move(completion),
            ApiResult<std::optional<VehicleEvent>>::failure(storageError(sqlErrorText(query))));
    }

    std::optional<VehicleEvent> event;
    if (query.next()) {
        event = eventFromQuery(query);
    }
    return finish<std::optional<VehicleEvent>>(
        context,
        std::move(completion),
        ApiResult<std::optional<VehicleEvent>>::success(std::move(event)));
}

RequestId SqliteEventRepository::deleteEvent(
    const EventIdentity& identity,
    QObject* context,
    ApiCompletion<void> completion)
{
    QString errorMessage;
    if (!openDatabase(&errorMessage) || !beginTransaction(&errorMessage)) {
        return finish<void>(context, std::move(completion), ApiResult<void>::failure(storageError(errorMessage)));
    }

    const QStringList tables {
        QStringLiteral("rv_event_details"),
        QStringLiteral("rv_evidence_cache"),
        QStringLiteral("rv_events")
    };
    for (const QString& table : tables) {
        QSqlQuery query(database_);
        query.prepare(QStringLiteral("DELETE FROM %1 WHERE %2").arg(table, identityWhereClause()));
        bindIdentity(query, identity);
        if (!query.exec()) {
            errorMessage = sqlErrorText(query);
            rollbackTransaction();
            return finish<void>(
                context,
                std::move(completion),
                ApiResult<void>::failure(storageError(errorMessage)));
        }
    }

    if (!commitTransaction(&errorMessage)) {
        rollbackTransaction();
        return finish<void>(context, std::move(completion), ApiResult<void>::failure(storageError(errorMessage)));
    }
    return finish<void>(context, std::move(completion), ApiResult<void>::success());
}

RequestId SqliteEventRepository::loadNonTerminalEvents(
    const QString& deviceId,
    QObject* context,
    ApiCompletion<QVector<VehicleEvent>> completion)
{
    QString errorMessage;
    if (!openDatabase(&errorMessage)) {
        return finish<QVector<VehicleEvent>>(
            context,
            std::move(completion),
            ApiResult<QVector<VehicleEvent>>::failure(storageError(errorMessage)));
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT device_id, event_id, track_id, event_epoch_ms, source_epoch_ms, offset_applied_ms, "
        "time_quality_value, time_quality_raw, motion_direction, speed_kmh, speed_valid, speed_status, "
        "ocr_status_value, ocr_status_raw, plate_text, plate_ascii, plate_color, evidence_status, "
        "evidence_available, detail_relative_url, evidence_relative_url, first_seen_epoch_ms, last_updated_epoch_ms "
        "FROM rv_events WHERE device_id = :device_id AND ocr_status_value IN (:queued, :unknown) "
        "ORDER BY event_epoch_ms ASC, event_id ASC, track_id ASC"));
    query.bindValue(QStringLiteral(":device_id"), deviceId);
    query.bindValue(QStringLiteral(":queued"), static_cast<int>(OcrStatus::Queued));
    query.bindValue(QStringLiteral(":unknown"), static_cast<int>(OcrStatus::Unknown));

    if (!query.exec()) {
        return finish<QVector<VehicleEvent>>(
            context,
            std::move(completion),
            ApiResult<QVector<VehicleEvent>>::failure(storageError(sqlErrorText(query))));
    }

    QVector<VehicleEvent> events;
    while (query.next()) {
        const VehicleEvent event = eventFromQuery(query);
        if (isNonTerminalOcrStatus(event.ocrStatus.value)) {
            events.append(event);
        }
    }

    return finish<QVector<VehicleEvent>>(
        context,
        std::move(completion),
        ApiResult<QVector<VehicleEvent>>::success(std::move(events)));
}

RequestId SqliteEventRepository::saveEvidenceState(
    const EvidenceCacheEntry& evidence,
    QObject* context,
    ApiCompletion<void> completion)
{
    QString errorMessage;
    if (!openDatabase(&errorMessage) || !saveEvidenceStateInternal(evidence, &errorMessage)) {
        return finish<void>(context, std::move(completion), ApiResult<void>::failure(storageError(errorMessage)));
    }
    return finish<void>(context, std::move(completion), ApiResult<void>::success());
}

RequestId SqliteEventRepository::loadEvidenceState(
    const EventIdentity& identity,
    const QString& role,
    QObject* context,
    ApiCompletion<std::optional<EvidenceCacheEntry>> completion)
{
    QString errorMessage;
    if (!openDatabase(&errorMessage)) {
        return finish<std::optional<EvidenceCacheEntry>>(
            context,
            std::move(completion),
            ApiResult<std::optional<EvidenceCacheEntry>>::failure(storageError(errorMessage)));
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT device_id, event_id, track_id, role, remote_relative_url, local_file_path, content_length, "
        "status_value, failure_code, updated_epoch_ms FROM rv_evidence_cache WHERE ")
        + identityWhereClause()
        + QStringLiteral(" AND role = :role"));
    bindIdentity(query, identity);
    query.bindValue(QStringLiteral(":role"), role);

    if (!query.exec()) {
        return finish<std::optional<EvidenceCacheEntry>>(
            context,
            std::move(completion),
            ApiResult<std::optional<EvidenceCacheEntry>>::failure(storageError(sqlErrorText(query))));
    }

    std::optional<EvidenceCacheEntry> entry;
    if (query.next()) {
        entry = evidenceFromQuery(query);
    }

    return finish<std::optional<EvidenceCacheEntry>>(
        context,
        std::move(completion),
        ApiResult<std::optional<EvidenceCacheEntry>>::success(std::move(entry)));
}

RequestId SqliteEventRepository::loadSyncAnchor(
    const QString& deviceId,
    QObject* context,
    ApiCompletion<std::optional<SyncAnchor>> completion)
{
    QString errorMessage;
    if (!openDatabase(&errorMessage)) {
        return finish<std::optional<SyncAnchor>>(
            context,
            std::move(completion),
            ApiResult<std::optional<SyncAnchor>>::failure(storageError(errorMessage)));
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT device_id, previous_source_epoch_ms, previous_event_id, previous_track_id, saved_epoch_ms "
        "FROM rv_sync_anchors WHERE device_id = :device_id"));
    query.bindValue(QStringLiteral(":device_id"), deviceId);

    if (!query.exec()) {
        return finish<std::optional<SyncAnchor>>(
            context,
            std::move(completion),
            ApiResult<std::optional<SyncAnchor>>::failure(storageError(sqlErrorText(query))));
    }

    std::optional<SyncAnchor> anchor;
    if (query.next()) {
        SyncAnchor value;
        value.deviceId = query.value(0).toString();
        value.previousHead.sourceEpochMs = query.value(1).toLongLong();
        value.previousHead.eventId = query.value(2).toLongLong();
        value.previousHead.trackId = query.value(3).toLongLong();
        value.savedEpochMs = query.value(4).toLongLong();
        anchor = value;
    }

    return finish<std::optional<SyncAnchor>>(
        context,
        std::move(completion),
        ApiResult<std::optional<SyncAnchor>>::success(std::move(anchor)));
}

RequestId SqliteEventRepository::saveSyncAnchor(
    const SyncAnchor& anchor,
    QObject* context,
    ApiCompletion<void> completion)
{
    QString errorMessage;
    if (!openDatabase(&errorMessage)) {
        return finish<void>(context, std::move(completion), ApiResult<void>::failure(storageError(errorMessage)));
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO rv_sync_anchors (device_id, previous_source_epoch_ms, previous_event_id, previous_track_id, saved_epoch_ms) "
        "VALUES (:device_id, :previous_source_epoch_ms, :previous_event_id, :previous_track_id, :saved_epoch_ms) "
        "ON CONFLICT(device_id) DO UPDATE SET "
        "previous_source_epoch_ms = excluded.previous_source_epoch_ms, "
        "previous_event_id = excluded.previous_event_id, "
        "previous_track_id = excluded.previous_track_id, "
        "saved_epoch_ms = excluded.saved_epoch_ms"));
    query.bindValue(QStringLiteral(":device_id"), anchor.deviceId);
    query.bindValue(QStringLiteral(":previous_source_epoch_ms"), anchor.previousHead.sourceEpochMs);
    query.bindValue(QStringLiteral(":previous_event_id"), anchor.previousHead.eventId);
    query.bindValue(QStringLiteral(":previous_track_id"), anchor.previousHead.trackId);
    query.bindValue(QStringLiteral(":saved_epoch_ms"), anchor.savedEpochMs);

    if (!query.exec()) {
        return finish<void>(context, std::move(completion), ApiResult<void>::failure(storageError(sqlErrorText(query))));
    }

    return finish<void>(context, std::move(completion), ApiResult<void>::success());
}

RequestId SqliteEventRepository::saveFtpTaskSnapshot(
    const StoredFtpTask& task,
    QObject* context,
    ApiCompletion<void> completion)
{
    QString errorMessage;
    if (!openDatabase(&errorMessage)
        || !beginTransaction(&errorMessage)
        || !saveFtpTaskSnapshotInternal(task, &errorMessage)
        || !commitTransaction(&errorMessage)) {
        rollbackTransaction();
        return finish<void>(context, std::move(completion), ApiResult<void>::failure(storageError(errorMessage)));
    }
    return finish<void>(context, std::move(completion), ApiResult<void>::success());
}

RequestId SqliteEventRepository::loadFtpTaskSnapshots(
    const FtpTaskQuery& taskQuery,
    QObject* context,
    ApiCompletion<QVector<StoredFtpTask>> completion)
{
    QString errorMessage;
    if (!openDatabase(&errorMessage)) {
        return finish<QVector<StoredFtpTask>>(
            context,
            std::move(completion),
            ApiResult<QVector<StoredFtpTask>>::failure(storageError(errorMessage)));
    }

    QStringList where;
    if (taskQuery.deviceId) {
        where.append(QStringLiteral("device_id = :query_device_id"));
    }
    if (taskQuery.startEpochMs) {
        where.append(QStringLiteral("end_epoch_ms > :start_epoch_ms"));
    }
    if (taskQuery.endEpochMs) {
        where.append(QStringLiteral("start_epoch_ms < :end_epoch_ms"));
    }

    const QString orderDirection = taskQuery.newestFirst ? QStringLiteral("DESC") : QStringLiteral("ASC");
    QString sql = QStringLiteral(
        "SELECT device_id, task_id, start_epoch_ms, end_epoch_ms, state_value, state_raw, "
        "created_epoch_ms, refreshed_epoch_ms FROM rv_ftp_tasks");
    if (!where.isEmpty()) {
        sql += QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND "));
    }
    sql += QStringLiteral(" ORDER BY created_epoch_ms %1, task_id %1 LIMIT :limit OFFSET :offset")
               .arg(orderDirection);

    QSqlQuery query(database_);
    query.prepare(sql);
    if (taskQuery.deviceId) {
        query.bindValue(QStringLiteral(":query_device_id"), *taskQuery.deviceId);
    }
    if (taskQuery.startEpochMs) {
        query.bindValue(QStringLiteral(":start_epoch_ms"), *taskQuery.startEpochMs);
    }
    if (taskQuery.endEpochMs) {
        query.bindValue(QStringLiteral(":end_epoch_ms"), *taskQuery.endEpochMs);
    }
    query.bindValue(QStringLiteral(":limit"), taskQuery.limit);
    query.bindValue(QStringLiteral(":offset"), taskQuery.offset);

    if (!query.exec()) {
        return finish<QVector<StoredFtpTask>>(
            context,
            std::move(completion),
            ApiResult<QVector<StoredFtpTask>>::failure(storageError(sqlErrorText(query))));
    }

    QVector<StoredFtpTask> tasks;
    while (query.next()) {
        StoredFtpTask task = ftpTaskFromQuery(query);
        if (!loadFtpTaskTargets(&task, &errorMessage)) {
            return finish<QVector<StoredFtpTask>>(
                context,
                std::move(completion),
                ApiResult<QVector<StoredFtpTask>>::failure(storageError(errorMessage)));
        }
        tasks.append(std::move(task));
    }

    return finish<QVector<StoredFtpTask>>(
        context,
        std::move(completion),
        ApiResult<QVector<StoredFtpTask>>::success(std::move(tasks)));
}

void SqliteEventRepository::cancel(const RequestId& requestId)
{
    Q_UNUSED(requestId)
}

void SqliteEventRepository::cancelAll()
{
}

bool SqliteEventRepository::openDatabase(QString* errorMessage)
{
    if (database_.isOpen()) {
        return true;
    }

    QDir().mkpath(QFileInfo(databasePath_).absolutePath());
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("SQLite driver is not available");
        }
        return false;
    }

    if (!database_.isValid()) {
        database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
        database_.setDatabaseName(databasePath_);
    }

    if (!database_.open()) {
        if (errorMessage) {
            *errorMessage = sqlErrorText(database_);
        }
        return false;
    }
    return true;
}

bool SqliteEventRepository::ensureSchema(QString* errorMessage)
{
    const QStringList statements {
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS rv_devices ("
            "device_id TEXT PRIMARY KEY,"
            "device_model TEXT NOT NULL,"
            "release_version TEXT NOT NULL,"
            "ipv4 TEXT NOT NULL,"
            "api_base_url TEXT NOT NULL,"
            "http_port INTEGER NOT NULL,"
            "discovery_port INTEGER NOT NULL,"
            "credential_ref TEXT NOT NULL,"
            "advertised_capabilities_json TEXT NOT NULL,"
            "last_online_epoch_ms INTEGER NOT NULL"
            ")"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS rv_events ("
            "device_id TEXT NOT NULL,"
            "event_id INTEGER NOT NULL,"
            "track_id INTEGER NOT NULL,"
            "event_epoch_ms INTEGER NOT NULL,"
            "source_epoch_ms INTEGER NOT NULL,"
            "offset_applied_ms INTEGER NOT NULL,"
            "time_quality_value INTEGER NOT NULL,"
            "time_quality_raw TEXT,"
            "motion_direction TEXT,"
            "speed_kmh INTEGER NOT NULL,"
            "speed_valid INTEGER NOT NULL,"
            "speed_status TEXT,"
            "ocr_status_value INTEGER NOT NULL,"
            "ocr_status_raw TEXT,"
            "plate_text TEXT,"
            "plate_ascii TEXT,"
            "plate_color TEXT,"
            "evidence_status TEXT,"
            "evidence_available INTEGER NOT NULL,"
            "detail_relative_url TEXT,"
            "evidence_relative_url TEXT,"
            "first_seen_epoch_ms INTEGER NOT NULL,"
            "last_updated_epoch_ms INTEGER NOT NULL,"
            "PRIMARY KEY (device_id, event_id, track_id)"
            ")"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS rv_event_details ("
            "device_id TEXT NOT NULL,"
            "event_id INTEGER NOT NULL,"
            "track_id INTEGER NOT NULL,"
            "trigger_mode TEXT NOT NULL,"
            "capture_reason TEXT NOT NULL,"
            "vehicle_json TEXT NOT NULL,"
            "line_region_json TEXT NOT NULL,"
            "radar_json TEXT NOT NULL,"
            "ocr_json TEXT NOT NULL,"
            "images_json TEXT NOT NULL,"
            "raw_json TEXT NOT NULL,"
            "fetched_epoch_ms INTEGER NOT NULL,"
            "PRIMARY KEY (device_id, event_id, track_id)"
            ")"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS rv_evidence_cache ("
            "device_id TEXT NOT NULL,"
            "event_id INTEGER NOT NULL,"
            "track_id INTEGER NOT NULL,"
            "role TEXT NOT NULL,"
            "remote_relative_url TEXT NOT NULL,"
            "local_file_path TEXT,"
            "content_length INTEGER NOT NULL,"
            "status_value INTEGER NOT NULL,"
            "failure_code TEXT,"
            "updated_epoch_ms INTEGER NOT NULL,"
            "PRIMARY KEY (device_id, event_id, track_id, role)"
            ")"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS rv_sync_anchors ("
            "device_id TEXT PRIMARY KEY,"
            "previous_source_epoch_ms INTEGER NOT NULL,"
            "previous_event_id INTEGER NOT NULL,"
            "previous_track_id INTEGER NOT NULL,"
            "saved_epoch_ms INTEGER NOT NULL"
            ")"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS rv_ftp_tasks ("
            "device_id TEXT NOT NULL,"
            "task_id TEXT NOT NULL,"
            "start_epoch_ms INTEGER NOT NULL,"
            "end_epoch_ms INTEGER NOT NULL,"
            "state_value INTEGER NOT NULL,"
            "state_raw TEXT NOT NULL,"
            "created_epoch_ms INTEGER NOT NULL,"
            "refreshed_epoch_ms INTEGER NOT NULL,"
            "PRIMARY KEY (device_id, task_id)"
            ")"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS rv_ftp_task_targets ("
            "device_id TEXT NOT NULL,"
            "task_id TEXT NOT NULL,"
            "target_id TEXT NOT NULL,"
            "state_value INTEGER NOT NULL,"
            "state_raw TEXT NOT NULL,"
            "total INTEGER NOT NULL,"
            "pending INTEGER NOT NULL,"
            "uploading INTEGER NOT NULL,"
            "done INTEGER NOT NULL,"
            "failed INTEGER NOT NULL,"
            "attempts INTEGER NOT NULL,"
            "last_error TEXT,"
            "PRIMARY KEY (device_id, task_id, target_id)"
            ")"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_rv_events_device_time ON rv_events(device_id, event_epoch_ms DESC)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_rv_events_plate ON rv_events(plate_text)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_rv_events_ocr ON rv_events(device_id, ocr_status_value)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_rv_evidence_status ON rv_evidence_cache(status_value, updated_epoch_ms)"),
        QStringLiteral("PRAGMA user_version = 1")
    };

    if (!beginTransaction(errorMessage)) {
        return false;
    }

    for (const QString& statement : statements) {
        if (!execSql(statement, errorMessage)) {
            rollbackTransaction();
            return false;
        }
    }

    if (!migrateEvidenceCacheSchema(errorMessage)) {
        rollbackTransaction();
        return false;
    }

    if (!execSql(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_rv_evidence_status "
            "ON rv_evidence_cache(status_value, updated_epoch_ms)"), errorMessage)) {
        rollbackTransaction();
        return false;
    }

    if (!commitTransaction(errorMessage)) {
        rollbackTransaction();
        return false;
    }
    return true;
}

bool SqliteEventRepository::migrateEvidenceCacheSchema(QString* errorMessage)
{
    QSqlQuery infoQuery(database_);
    if (!infoQuery.exec(QStringLiteral("PRAGMA table_info(rv_evidence_cache)"))) {
        if (errorMessage) {
            *errorMessage = sqlErrorText(infoQuery);
        }
        return false;
    }

    bool localPathNotNull = false;
    while (infoQuery.next()) {
        if (infoQuery.value(1).toString() == QStringLiteral("local_file_path")) {
            localPathNotNull = infoQuery.value(3).toInt() != 0;
            break;
        }
    }
    infoQuery.finish();

    if (!localPathNotNull) {
        return true;
    }

    const QStringList migrationStatements {
        QStringLiteral("ALTER TABLE rv_evidence_cache RENAME TO rv_evidence_cache_old"),
        QStringLiteral(
            "CREATE TABLE rv_evidence_cache ("
            "device_id TEXT NOT NULL,"
            "event_id INTEGER NOT NULL,"
            "track_id INTEGER NOT NULL,"
            "role TEXT NOT NULL,"
            "remote_relative_url TEXT NOT NULL,"
            "local_file_path TEXT,"
            "content_length INTEGER NOT NULL,"
            "status_value INTEGER NOT NULL,"
            "failure_code TEXT,"
            "updated_epoch_ms INTEGER NOT NULL,"
            "PRIMARY KEY (device_id, event_id, track_id, role)"
            ")"),
        QStringLiteral(
            "INSERT INTO rv_evidence_cache ("
            "device_id, event_id, track_id, role, remote_relative_url, local_file_path, content_length, "
            "status_value, failure_code, updated_epoch_ms"
            ") SELECT "
            "device_id, event_id, track_id, role, remote_relative_url, local_file_path, content_length, "
            "status_value, failure_code, updated_epoch_ms "
            "FROM rv_evidence_cache_old"),
        QStringLiteral("DROP TABLE rv_evidence_cache_old")
    };

    for (const QString& statement : migrationStatements) {
        if (!execSql(statement, errorMessage)) {
            return false;
        }
    }

    return true;
}

bool SqliteEventRepository::execSql(const QString& sql, QString* errorMessage)
{
    QSqlQuery query(database_);
    if (!query.exec(sql)) {
        if (errorMessage) {
            *errorMessage = sqlErrorText(query);
        }
        return false;
    }
    return true;
}

bool SqliteEventRepository::beginTransaction(QString* errorMessage)
{
    if (!database_.transaction()) {
        if (errorMessage) {
            *errorMessage = sqlErrorText(database_);
        }
        return false;
    }
    return true;
}

bool SqliteEventRepository::commitTransaction(QString* errorMessage)
{
    if (!database_.commit()) {
        if (errorMessage) {
            *errorMessage = sqlErrorText(database_);
        }
        return false;
    }
    return true;
}

void SqliteEventRepository::rollbackTransaction()
{
    if (database_.isOpen()) {
        database_.rollback();
    }
}

ApiError SqliteEventRepository::storageError(const QString& message) const
{
    ApiError error;
    error.code = QStringLiteral("storage_error");
    error.message = message;
    error.category = ApiErrorCategory::Storage;
    error.retryable = false;
    return error;
}

bool SqliteEventRepository::upsertDeviceInternal(const DeviceProfile& profile, QString* errorMessage)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO rv_devices ("
        "device_id, device_model, release_version, ipv4, api_base_url, http_port, discovery_port, "
        "credential_ref, advertised_capabilities_json, last_online_epoch_ms"
        ") VALUES ("
        ":device_id, :device_model, :release_version, :ipv4, :api_base_url, :http_port, :discovery_port, "
        ":credential_ref, :advertised_capabilities_json, :last_online_epoch_ms"
        ") ON CONFLICT(device_id) DO UPDATE SET "
        "device_model = excluded.device_model, release_version = excluded.release_version, ipv4 = excluded.ipv4, "
        "api_base_url = excluded.api_base_url, http_port = excluded.http_port, discovery_port = excluded.discovery_port, "
        "credential_ref = excluded.credential_ref, advertised_capabilities_json = excluded.advertised_capabilities_json, "
        "last_online_epoch_ms = excluded.last_online_epoch_ms"));
    query.bindValue(QStringLiteral(":device_id"), profile.deviceId);
    query.bindValue(QStringLiteral(":device_model"), profile.deviceModel);
    query.bindValue(QStringLiteral(":release_version"), profile.releaseVersion);
    query.bindValue(QStringLiteral(":ipv4"), profile.endpoint.ipv4);
    query.bindValue(QStringLiteral(":api_base_url"), profile.endpoint.apiBaseUrl.toString());
    query.bindValue(QStringLiteral(":http_port"), profile.endpoint.httpPort);
    query.bindValue(QStringLiteral(":discovery_port"), profile.endpoint.discoveryPort);
    query.bindValue(QStringLiteral(":credential_ref"), profile.credentialRef);
    query.bindValue(QStringLiteral(":advertised_capabilities_json"), stringListToJson(profile.advertisedCapabilities));
    query.bindValue(QStringLiteral(":last_online_epoch_ms"), profile.lastOnlineEpochMs);

    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = sqlErrorText(query);
        }
        return false;
    }
    return true;
}

bool SqliteEventRepository::upsertEventInternal(const VehicleEvent& event, QString* errorMessage)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO rv_events ("
        "device_id, event_id, track_id, event_epoch_ms, source_epoch_ms, offset_applied_ms, "
        "time_quality_value, time_quality_raw, motion_direction, speed_kmh, speed_valid, speed_status, "
        "ocr_status_value, ocr_status_raw, plate_text, plate_ascii, plate_color, evidence_status, "
        "evidence_available, detail_relative_url, evidence_relative_url, first_seen_epoch_ms, last_updated_epoch_ms"
        ") VALUES ("
        ":device_id, :event_id, :track_id, :event_epoch_ms, :source_epoch_ms, :offset_applied_ms, "
        ":time_quality_value, :time_quality_raw, :motion_direction, :speed_kmh, :speed_valid, :speed_status, "
        ":ocr_status_value, :ocr_status_raw, :plate_text, :plate_ascii, :plate_color, :evidence_status, "
        ":evidence_available, :detail_relative_url, :evidence_relative_url, :first_seen_epoch_ms, :last_updated_epoch_ms"
        ") ON CONFLICT(device_id, event_id, track_id) DO UPDATE SET "
        "event_epoch_ms = excluded.event_epoch_ms, source_epoch_ms = excluded.source_epoch_ms, "
        "offset_applied_ms = excluded.offset_applied_ms, time_quality_value = excluded.time_quality_value, "
        "time_quality_raw = excluded.time_quality_raw, motion_direction = excluded.motion_direction, "
        "speed_kmh = excluded.speed_kmh, speed_valid = excluded.speed_valid, speed_status = excluded.speed_status, "
        "ocr_status_value = excluded.ocr_status_value, ocr_status_raw = excluded.ocr_status_raw, "
        "plate_text = excluded.plate_text, plate_ascii = excluded.plate_ascii, plate_color = excluded.plate_color, "
        "evidence_status = excluded.evidence_status, evidence_available = excluded.evidence_available, "
        "detail_relative_url = excluded.detail_relative_url, evidence_relative_url = excluded.evidence_relative_url, "
        "last_updated_epoch_ms = excluded.last_updated_epoch_ms"));
    bindIdentity(query, event.identity);
    query.bindValue(QStringLiteral(":event_epoch_ms"), event.eventTime.epochMs);
    query.bindValue(QStringLiteral(":source_epoch_ms"), event.eventTime.sourceEpochMs);
    query.bindValue(QStringLiteral(":offset_applied_ms"), event.eventTime.offsetAppliedMs);
    query.bindValue(QStringLiteral(":time_quality_value"), enumValue(event.eventTime.quality));
    query.bindValue(QStringLiteral(":time_quality_raw"), event.eventTime.quality.rawValue);
    query.bindValue(QStringLiteral(":motion_direction"), event.motionDirection);
    query.bindValue(QStringLiteral(":speed_kmh"), event.speedKmh);
    query.bindValue(QStringLiteral(":speed_valid"), event.speedValid ? 1 : 0);
    query.bindValue(QStringLiteral(":speed_status"), event.speedStatus);
    query.bindValue(QStringLiteral(":ocr_status_value"), enumValue(event.ocrStatus));
    query.bindValue(QStringLiteral(":ocr_status_raw"), event.ocrStatus.rawValue);
    query.bindValue(QStringLiteral(":plate_text"), event.plateText);
    query.bindValue(QStringLiteral(":plate_ascii"), event.plateAscii);
    query.bindValue(QStringLiteral(":plate_color"), event.plateColor);
    query.bindValue(QStringLiteral(":evidence_status"), event.evidenceStatus);
    query.bindValue(QStringLiteral(":evidence_available"), event.evidenceAvailable ? 1 : 0);
    query.bindValue(QStringLiteral(":detail_relative_url"), event.detailRelativeUrl);
    query.bindValue(QStringLiteral(":evidence_relative_url"), event.evidenceRelativeUrl);
    query.bindValue(QStringLiteral(":first_seen_epoch_ms"), event.firstSeenEpochMs);
    query.bindValue(QStringLiteral(":last_updated_epoch_ms"), event.lastUpdatedEpochMs);

    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = sqlErrorText(query);
        }
        return false;
    }
    return true;
}

bool SqliteEventRepository::saveEvidenceStateInternal(const EvidenceCacheEntry& evidence, QString* errorMessage)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO rv_evidence_cache ("
        "device_id, event_id, track_id, role, remote_relative_url, local_file_path, content_length, "
        "status_value, failure_code, updated_epoch_ms"
        ") VALUES ("
        ":device_id, :event_id, :track_id, :role, :remote_relative_url, :local_file_path, :content_length, "
        ":status_value, :failure_code, :updated_epoch_ms"
        ") ON CONFLICT(device_id, event_id, track_id, role) DO UPDATE SET "
        "remote_relative_url = excluded.remote_relative_url, local_file_path = excluded.local_file_path, "
        "content_length = excluded.content_length, status_value = excluded.status_value, "
        "failure_code = excluded.failure_code, updated_epoch_ms = excluded.updated_epoch_ms"));
    bindIdentity(query, evidence.identity);
    query.bindValue(QStringLiteral(":role"), evidence.role);
    query.bindValue(QStringLiteral(":remote_relative_url"), evidence.remoteRelativeUrl);
    query.bindValue(QStringLiteral(":local_file_path"), evidence.localFilePath);
    query.bindValue(QStringLiteral(":content_length"), evidence.contentLength);
    query.bindValue(QStringLiteral(":status_value"), static_cast<int>(evidence.status));
    query.bindValue(QStringLiteral(":failure_code"), evidence.failureCode);
    query.bindValue(QStringLiteral(":updated_epoch_ms"), evidence.updatedEpochMs);

    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = sqlErrorText(query);
        }
        return false;
    }
    return true;
}

bool SqliteEventRepository::saveFtpTaskSnapshotInternal(const StoredFtpTask& task, QString* errorMessage)
{
    QSqlQuery taskQuery(database_);
    taskQuery.prepare(QStringLiteral(
        "INSERT INTO rv_ftp_tasks ("
        "device_id, task_id, start_epoch_ms, end_epoch_ms, state_value, state_raw, created_epoch_ms, refreshed_epoch_ms"
        ") VALUES ("
        ":device_id, :task_id, :start_epoch_ms, :end_epoch_ms, :state_value, :state_raw, :created_epoch_ms, :refreshed_epoch_ms"
        ") ON CONFLICT(device_id, task_id) DO UPDATE SET "
        "start_epoch_ms = excluded.start_epoch_ms, end_epoch_ms = excluded.end_epoch_ms, "
        "state_value = excluded.state_value, state_raw = excluded.state_raw, "
        "created_epoch_ms = excluded.created_epoch_ms, refreshed_epoch_ms = excluded.refreshed_epoch_ms"));
    taskQuery.bindValue(QStringLiteral(":device_id"), task.deviceId);
    taskQuery.bindValue(QStringLiteral(":task_id"), task.taskId);
    taskQuery.bindValue(QStringLiteral(":start_epoch_ms"), task.startEpochMs);
    taskQuery.bindValue(QStringLiteral(":end_epoch_ms"), task.endEpochMs);
    taskQuery.bindValue(QStringLiteral(":state_value"), enumValue(task.state));
    taskQuery.bindValue(QStringLiteral(":state_raw"), task.state.rawValue);
    taskQuery.bindValue(QStringLiteral(":created_epoch_ms"), task.createdEpochMs);
    taskQuery.bindValue(QStringLiteral(":refreshed_epoch_ms"), task.refreshedEpochMs);

    if (!taskQuery.exec()) {
        if (errorMessage) {
            *errorMessage = sqlErrorText(taskQuery);
        }
        return false;
    }

    QSqlQuery deleteTargets(database_);
    deleteTargets.prepare(QStringLiteral("DELETE FROM rv_ftp_task_targets WHERE device_id = :device_id AND task_id = :task_id"));
    deleteTargets.bindValue(QStringLiteral(":device_id"), task.deviceId);
    deleteTargets.bindValue(QStringLiteral(":task_id"), task.taskId);
    if (!deleteTargets.exec()) {
        if (errorMessage) {
            *errorMessage = sqlErrorText(deleteTargets);
        }
        return false;
    }

    for (const StoredFtpTargetStatus& target : task.targets) {
        QSqlQuery targetQuery(database_);
        targetQuery.prepare(QStringLiteral(
            "INSERT INTO rv_ftp_task_targets ("
            "device_id, task_id, target_id, state_value, state_raw, total, pending, uploading, done, failed, attempts, last_error"
            ") VALUES ("
            ":device_id, :task_id, :target_id, :state_value, :state_raw, :total, :pending, :uploading, :done, :failed, :attempts, :last_error"
            ")"));
        targetQuery.bindValue(QStringLiteral(":device_id"), task.deviceId);
        targetQuery.bindValue(QStringLiteral(":task_id"), task.taskId);
        targetQuery.bindValue(QStringLiteral(":target_id"), target.targetId);
        targetQuery.bindValue(QStringLiteral(":state_value"), enumValue(target.state));
        targetQuery.bindValue(QStringLiteral(":state_raw"), target.state.rawValue);
        targetQuery.bindValue(QStringLiteral(":total"), target.total);
        targetQuery.bindValue(QStringLiteral(":pending"), target.pending);
        targetQuery.bindValue(QStringLiteral(":uploading"), target.uploading);
        targetQuery.bindValue(QStringLiteral(":done"), target.done);
        targetQuery.bindValue(QStringLiteral(":failed"), target.failed);
        targetQuery.bindValue(QStringLiteral(":attempts"), target.attempts);
        targetQuery.bindValue(QStringLiteral(":last_error"), target.lastError);

        if (!targetQuery.exec()) {
            if (errorMessage) {
                *errorMessage = sqlErrorText(targetQuery);
            }
            return false;
        }
    }
    return true;
}

bool SqliteEventRepository::loadFtpTaskTargets(StoredFtpTask* task, QString* errorMessage) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT target_id, state_value, state_raw, total, pending, uploading, done, failed, attempts, last_error "
        "FROM rv_ftp_task_targets WHERE device_id = :device_id AND task_id = :task_id ORDER BY target_id ASC"));
    query.bindValue(QStringLiteral(":device_id"), task->deviceId);
    query.bindValue(QStringLiteral(":task_id"), task->taskId);

    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = sqlErrorText(query);
        }
        return false;
    }

    task->targets.clear();
    while (query.next()) {
        task->targets.append(ftpTargetFromQuery(query));
    }
    return true;
}

DeviceProfile SqliteEventRepository::deviceProfileFromQuery(const QSqlQuery& query) const
{
    DeviceProfile profile;
    profile.deviceId = query.value(0).toString();
    profile.deviceModel = query.value(1).toString();
    profile.releaseVersion = query.value(2).toString();
    profile.endpoint.ipv4 = query.value(3).toString();
    profile.endpoint.apiBaseUrl = QUrl(query.value(4).toString());
    profile.endpoint.httpPort = static_cast<quint16>(query.value(5).toUInt());
    profile.endpoint.discoveryPort = static_cast<quint16>(query.value(6).toUInt());
    profile.credentialRef = query.value(7).toString();
    profile.advertisedCapabilities = stringListFromJson(query.value(8).toString());
    profile.lastOnlineEpochMs = query.value(9).toLongLong();
    return profile;
}

VehicleEvent SqliteEventRepository::eventFromQuery(const QSqlQuery& query) const
{
    VehicleEvent event;
    event.identity.deviceId = query.value(0).toString();
    event.identity.eventId = query.value(1).toLongLong();
    event.identity.trackId = query.value(2).toLongLong();
    event.eventTime.epochMs = query.value(3).toLongLong();
    event.eventTime.sourceEpochMs = query.value(4).toLongLong();
    event.eventTime.offsetAppliedMs = query.value(5).toLongLong();
    event.eventTime.quality = wireEnumFromStorage<TimeQuality>(query.value(6), query.value(7));
    event.motionDirection = query.value(8).toString();
    event.speedKmh = query.value(9).toInt();
    event.speedValid = query.value(10).toInt() != 0;
    event.speedStatus = query.value(11).toString();
    event.ocrStatus = wireEnumFromStorage<OcrStatus>(query.value(12), query.value(13));
    event.plateText = query.value(14).toString();
    event.plateAscii = query.value(15).toString();
    event.plateColor = query.value(16).toString();
    event.evidenceStatus = query.value(17).toString();
    event.evidenceAvailable = query.value(18).toInt() != 0;
    event.detailRelativeUrl = query.value(19).toString();
    event.evidenceRelativeUrl = query.value(20).toString();
    event.firstSeenEpochMs = query.value(21).toLongLong();
    event.lastUpdatedEpochMs = query.value(22).toLongLong();
    return event;
}

EvidenceCacheEntry SqliteEventRepository::evidenceFromQuery(const QSqlQuery& query) const
{
    EvidenceCacheEntry evidence;
    evidence.identity.deviceId = query.value(0).toString();
    evidence.identity.eventId = query.value(1).toLongLong();
    evidence.identity.trackId = query.value(2).toLongLong();
    evidence.role = query.value(3).toString();
    evidence.remoteRelativeUrl = query.value(4).toString();
    evidence.localFilePath = query.value(5).toString();
    evidence.contentLength = query.value(6).toLongLong();
    evidence.status = static_cast<EvidenceCacheStatus>(query.value(7).toInt());
    evidence.failureCode = query.value(8).toString();
    evidence.updatedEpochMs = query.value(9).toLongLong();
    return evidence;
}

StoredFtpTask SqliteEventRepository::ftpTaskFromQuery(const QSqlQuery& query) const
{
    StoredFtpTask task;
    task.deviceId = query.value(0).toString();
    task.taskId = query.value(1).toString();
    task.startEpochMs = query.value(2).toLongLong();
    task.endEpochMs = query.value(3).toLongLong();
    task.state = wireEnumFromStorage<FtpTaskState>(query.value(4), query.value(5));
    task.createdEpochMs = query.value(6).toLongLong();
    task.refreshedEpochMs = query.value(7).toLongLong();
    return task;
}

StoredFtpTargetStatus SqliteEventRepository::ftpTargetFromQuery(const QSqlQuery& query) const
{
    StoredFtpTargetStatus target;
    target.targetId = query.value(0).toString();
    target.state = wireEnumFromStorage<FtpTaskState>(query.value(1), query.value(2));
    target.total = query.value(3).toInt();
    target.pending = query.value(4).toInt();
    target.uploading = query.value(5).toInt();
    target.done = query.value(6).toInt();
    target.failed = query.value(7).toInt();
    target.attempts = query.value(8).toInt();
    target.lastError = query.value(9).toString();
    return target;
}

} // namespace rv1126b
