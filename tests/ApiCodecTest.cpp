#include "../src/rv1126b/protocol/BoardApiCodec.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace rv1126b;

class ApiCodecTest final : public QObject
{
    Q_OBJECT

private slots:
    void parsesDiscoveryAndRejectsMismatchedNonce();
    void parsesHealthAndEventPageFixtures();
    void parsesLatestEventUrlsCaptureAndTimeQuality();
    void rejectsMalformedKnownField();
    void parsesEventDetailAndConfigurationResponses();
    void parsesFtpTaskDetailFixture();
    void parsesFtpConfigControlAndPageResponses();
    void classifiesBoardErrors();
    void serverErrorMessageIsNeverExposed();
    void encodesAndValidatesEvidenceAndTime();
    void encodesFtpRequestsAndRejectsInvalidValues();
};

namespace {

QByteArray fixture(const QString& name)
{
    const QString path = QDir(QString::fromUtf8(CAMERA_MANAGER_SOURCE_DIR))
        .filePath(QStringLiteral("tests/fixtures/rv1126b/%1").arg(name));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QTest::qFail(qPrintable(QStringLiteral("Could not open fixture: %1").arg(path)), __FILE__, __LINE__);
        return {};
    }
    return file.readAll();
}

QJsonObject jsonObject(const QByteArray& payload)
{
    return QJsonDocument::fromJson(payload).object();
}

} // namespace

void ApiCodecTest::parsesDiscoveryAndRejectsMismatchedNonce()
{
    BoardApiCodec codec;
    const QByteArray payload = R"({
        "magic":"RV1126B_DISCOVERY",
        "version":1,
        "type":"discover_response",
        "nonce":"scan-nonce",
        "device_id":"rv1126b_001",
        "device_model":"RV1126B",
        "ipv4":"192.168.137.73",
        "api_version":"v1",
        "api_url":"http://192.168.137.73:18080/api/v1",
        "release_version":"development",
        "auth_required":true,
        "capabilities":["health","events"]
    })";

    const auto valid = codec.parseDiscoveryResponse(payload, QStringLiteral("scan-nonce"));
    QVERIFY(valid.isSuccess());
    QCOMPARE(valid.value().deviceId, QStringLiteral("rv1126b_001"));
    QCOMPARE(valid.value().apiUrl.port(), 18080);

    const auto invalid = codec.parseDiscoveryResponse(payload, QStringLiteral("other-nonce"));
    QVERIFY(!invalid.isSuccess());
    QCOMPARE(invalid.error().category, ApiErrorCategory::Protocol);
}

void ApiCodecTest::parsesHealthAndEventPageFixtures()
{
    BoardApiCodec codec;
    const auto health = codec.parseHealth(fixture(QStringLiteral("health.json")));
    QVERIFY(health.isSuccess());
    QCOMPARE(health.value().deviceId, QStringLiteral("rv1126b_001"));
    QCOMPARE(health.value().serverTime.quality.value, TimeQuality::ConfiguredOffset);
    QCOMPARE(health.value().applicationApi.discoveryPort, 18081);

    const auto page = codec.parseEventPage(fixture(QStringLiteral("event_page.json")));
    QVERIFY(page.isSuccess());
    QCOMPARE(page.value().items.size(), 1);
    QCOMPARE(page.value().items.first().eventId, 131);
    QCOMPARE(page.value().items.first().ocrStatus.value, OcrStatus::Unknown);
    QCOMPARE(page.value().items.first().ocrStatus.rawValue, QStringLiteral("future_ocr_state"));
    QVERIFY(!page.value().nextCursor.has_value());
}

void ApiCodecTest::parsesLatestEventUrlsCaptureAndTimeQuality()
{
    BoardApiCodec codec;
    const QByteArray payload = R"({
        "api_version":"v1",
        "items":[{
            "event_id":132,
            "track_id":1995,
            "event_time":{
                "epoch_ms":1784002847389,
                "source_epoch_ms":1784002847389,
                "offset_applied_ms":0,
                "quality":"rtc_restored_current_boot",
                "boot_id":"boot-123"
            },
            "motion_direction":"northbound",
            "speed_kmh":"0.0",
            "speed_valid":false,
            "speed_status":"no_data",
            "capture_status":"failed",
            "capture_error":"snapshot_timeout",
            "ocr_status":"failed",
            "plate_text":"",
            "plate_ascii":"",
            "plate_color":"",
            "evidence_status":"not_generated",
            "evidence_available":false,
            "detail_url":"/api/v1/events/132/1995",
            "evidence_url":null
        }],
        "count":1,
        "has_more":false,
        "next_cursor":null
    })";

    const auto page = codec.parseEventPage(payload);
    QVERIFY(page.isSuccess());
    const EventSummaryDto summary = page.value().items.first();
    QCOMPARE(summary.detailRelativeUrl, QStringLiteral("/api/v1/events/132/1995"));
    QCOMPARE(summary.evidenceRelativeUrl, QString());
    QCOMPARE(summary.captureStatus, QStringLiteral("failed"));
    QCOMPARE(summary.captureError, QStringLiteral("snapshot_timeout"));
    QCOMPARE(summary.speedKmh, 0);
    QVERIFY(!summary.speedValid);
    QCOMPARE(summary.eventTime.quality.value, TimeQuality::RtcRestoredCurrentBoot);
    QCOMPARE(summary.eventTime.bootId, QStringLiteral("boot-123"));
}

void ApiCodecTest::rejectsMalformedKnownField()
{
    BoardApiCodec codec;
    QJsonObject object = jsonObject(fixture(QStringLiteral("health.json")));
    object.insert(QStringLiteral("device_id"), 42);
    const auto result = codec.parseHealth(QJsonDocument(object).toJson(QJsonDocument::Compact));
    QVERIFY(!result.isSuccess());
    QCOMPARE(result.error().category, ApiErrorCategory::Protocol);
}

void ApiCodecTest::parsesEventDetailAndConfigurationResponses()
{
    BoardApiCodec codec;
    QJsonObject event = jsonObject(fixture(QStringLiteral("event_page.json"))).value(QStringLiteral("items")).toArray().first().toObject();
    event.insert(QStringLiteral("trigger_mode"), QStringLiteral("radar"));
    event.insert(QStringLiteral("capture_reason"), QStringLiteral("speeding"));
    event.insert(QStringLiteral("vehicle"), QJsonObject {});
    event.insert(QStringLiteral("line_region"), QJsonObject {});
    event.insert(QStringLiteral("radar"), QJsonObject {});
    event.insert(QStringLiteral("ocr"), QJsonObject {});
    event.insert(QStringLiteral("images"), QJsonObject {});
    const auto detail = codec.parseEventDetail(QJsonDocument(event).toJson(QJsonDocument::Compact));
    QVERIFY(detail.isSuccess());
    QCOMPARE(detail.value().summary.eventId, 131);
    QCOMPARE(detail.value().triggerMode, QStringLiteral("radar"));

    const QByteArray evidenceConfig = R"({
        "api_version":"v1",
        "evidence":{"site_name":"site","road_direction":"northbound","speed_limit_kmh":60,"status_text":"normal","code_text":"TEST-001"},
        "restart_required":true,
        "apply_mode":"next_pipeline_restart",
        "effective_scope":"new_ocr_jobs_only",
        "existing_events_unchanged":true
    })";
    const auto config = codec.parseEvidenceConfig(evidenceConfig);
    QVERIFY(config.isSuccess());
    QVERIFY(config.value().restartRequired);
    QCOMPARE(config.value().evidence.speedLimitKmh, 60);

    const QByteArray time = R"({
        "api_version":"v1",
        "time":{"epoch_ms":1784000000000,"source_epoch_ms":1784028800000,"offset_applied_ms":-28800000,"quality":"configured_offset"},
        "timezone_contract":"UTC epoch milliseconds",
        "ntp_status":"vendor_time_chain_unverified",
        "time_set_enabled":false
    })";
    const auto timeStatus = codec.parseTimeStatus(time);
    QVERIFY(timeStatus.isSuccess());
    QCOMPARE(timeStatus.value().time.epochMs, 1784000000000LL);
    QVERIFY(!timeStatus.value().timeSetEnabled);
}

void ApiCodecTest::parsesFtpTaskDetailFixture()
{
    BoardApiCodec codec;
    const auto detail = codec.parseFtpTaskDetail(fixture(QStringLiteral("ftp_task_detail.json")));
    QVERIFY(detail.isSuccess());
    QCOMPARE(detail.value().summary.taskId, QStringLiteral("ftp_task_001"));
    QCOMPARE(detail.value().targets.size(), 2);
    QCOMPARE(detail.value().targets.last().state.value, FtpTaskState::Failed);
    QCOMPARE(detail.value().targets.last().lastError, QStringLiteral("ftp_timeout"));
}

void ApiCodecTest::parsesFtpConfigControlAndPageResponses()
{
    BoardApiCodec codec;
    const QByteArray configPayload = R"({
        "api_version":"v1","revision":"v1-r1","device_id":"rv1126b_001",
        "retry_max":5,"retry_interval_sec":30,"connect_timeout_sec":10,"transfer_timeout_sec":120,"scan_interval_sec":5,
        "targets":[{"id":"server1","enabled":true,"host":"192.168.137.1","port":21,"user":"upload","remote_dir":"/vehicle_events","passive":true,"password_configured":true}],
        "restart_required":false
    })";
    const auto config = codec.parseFtpConfig(configPayload);
    QVERIFY(config.isSuccess());
    QCOMPARE(config.value().targets.first().id, QStringLiteral("server1"));
    QVERIFY(config.value().targets.first().passwordConfigured);

    const auto control = codec.parseFtpControl(QByteArrayLiteral("{\"api_version\":\"v1\",\"revision\":\"v1-r1\",\"enabled\":true,\"scope\":\"new_events_only\"}"));
    QVERIFY(control.isSuccess());
    QCOMPARE(control.value().scope.value, FtpControlScope::NewEventsOnly);

    const auto liveControl = codec.parseFtpControl(QByteArrayLiteral(
        "{\"api_version\":\"v1\",\"revision\":\"v1-live\",\"enabled\":true,"
        "\"min_event_epoch_ms\":1784549240147,\"write_enabled\":true,"
        "\"restart_required\":false,\"apply_mode\":\"uploader_hot_reload\"}"));
    QVERIFY(liveControl.isSuccess());
    QCOMPARE(liveControl.value().scope.value, FtpControlScope::NewEventsOnly);
    QCOMPARE(liveControl.value().minEventEpochMs, 1784549240147LL);
    QVERIFY(liveControl.value().writeEnabled);
    QCOMPARE(liveControl.value().applyMode, QStringLiteral("uploader_hot_reload"));

    QJsonObject task = jsonObject(fixture(QStringLiteral("ftp_task_detail.json")));
    task.remove(QStringLiteral("targets"));
    task.insert(QStringLiteral("api_version"), QStringLiteral("v1"));
    QJsonObject page;
    page.insert(QStringLiteral("api_version"), QStringLiteral("v1"));
    page.insert(QStringLiteral("items"), QJsonArray {task});
    page.insert(QStringLiteral("count"), 1);
    page.insert(QStringLiteral("has_more"), false);
    page.insert(QStringLiteral("next_cursor"), QJsonValue::Null);
    const auto taskPage = codec.parseFtpTaskPage(QJsonDocument(page).toJson(QJsonDocument::Compact));
    QVERIFY(taskPage.isSuccess());
    QCOMPARE(taskPage.value().items.first().state.value, FtpTaskState::Running);

    const auto emptyTaskPage = codec.parseFtpTaskPage(QByteArrayLiteral(
        "{\"api_version\":\"v1\",\"items\":[],\"has_more\":false,\"next_cursor\":null}"));
    QVERIFY(emptyTaskPage.isSuccess());
    QCOMPARE(emptyTaskPage.value().count, 0);

    QJsonObject wrapped;
    QJsonObject detailTask = jsonObject(fixture(QStringLiteral("ftp_task_detail.json")));
    wrapped.insert(QStringLiteral("api_version"), QStringLiteral("v1"));
    wrapped.insert(QStringLiteral("task"), detailTask);
    const auto wrappedDetail = codec.parseFtpTaskDetail(QJsonDocument(wrapped).toJson(QJsonDocument::Compact));
    QVERIFY(wrappedDetail.isSuccess());
    QCOMPARE(wrappedDetail.value().summary.taskId, QStringLiteral("ftp_task_001"));

    const auto retry = codec.parseFtpTaskDetail(QByteArrayLiteral(
        "{\"api_version\":\"v1\",\"task_id\":\"ftp_task_001\",\"retry_queued\":2}"));
    QVERIFY(retry.isSuccess());
    QCOMPARE(retry.value().summary.taskId, QStringLiteral("ftp_task_001"));
    QCOMPARE(retry.value().retryQueued, 2LL);
}

void ApiCodecTest::classifiesBoardErrors()
{
    BoardApiCodec codec;
    const QByteArray conflict = R"({"api_version":"v1","error":{"code":"config_revision_conflict","message":"revision differs"}})";
    const ApiError conflictError = codec.parseError(409, conflict);
    QCOMPARE(conflictError.category, ApiErrorCategory::Conflict);
    QVERIFY(!conflictError.retryable);

    const QByteArray unavailable = R"({"api_version":"v1","error":{"code":"evidence_unavailable","message":"not ready"}})";
    const ApiError unavailableError = codec.parseError(409, unavailable);
    QCOMPARE(unavailableError.category, ApiErrorCategory::Temporary);
    QVERIFY(unavailableError.retryable);

    const ApiError malformed = codec.parseError(500, QByteArrayLiteral("not-json"));
    QCOMPARE(malformed.category, ApiErrorCategory::Temporary);
    QVERIFY(malformed.retryable);
}

void ApiCodecTest::serverErrorMessageIsNeverExposed()
{
    BoardApiCodec codec;
    const ApiError error = codec.parseError(
        401,
        QByteArrayLiteral(R"({"error":{"code":"invalid_token","message":"Bearer secret-token-must-not-leak"}})"));

    QCOMPARE(error.code, QStringLiteral("invalid_token"));
    QCOMPARE(error.category, ApiErrorCategory::Authentication);
    QVERIFY(!error.message.contains(QStringLiteral("secret-token")));
    QCOMPARE(error.message, QStringLiteral("Board request failed with HTTP status 401."));
}

void ApiCodecTest::encodesAndValidatesEvidenceAndTime()
{
    BoardApiCodec codec;
    EvidenceConfigUpdate update;
    update.siteName = QStringLiteral("site");
    update.roadDirection = QStringLiteral("northbound");
    update.speedLimitKmh = 60;
    update.statusText = QStringLiteral("normal");
    update.codeText = QStringLiteral("TEST-001");
    const auto encoded = codec.encodeEvidenceConfig(update);
    QVERIFY(encoded.isSuccess());
    const QJsonObject object = jsonObject(encoded.value());
    QCOMPARE(object.size(), 5);
    QCOMPARE(object.value(QStringLiteral("speed_limit_kmh")).toInt(), 60);

    update.statusText = QStringLiteral("bad\ntext");
    const auto invalid = codec.encodeEvidenceConfig(update);
    QVERIFY(!invalid.isSuccess());
    QCOMPARE(invalid.error().code, QStringLiteral("invalid_evidence_config"));

    TimeUpdate time;
    time.utcEpochMs = 1784000000000LL;
    const auto encodedTime = codec.encodeTimeUpdate(time);
    QVERIFY(encodedTime.isSuccess());
    QCOMPARE(jsonObject(encodedTime.value()).value(QStringLiteral("utc_epoch_ms")).toInteger(), time.utcEpochMs);

    time.utcEpochMs = 1;
    QVERIFY(!codec.encodeTimeUpdate(time).isSuccess());
}

void ApiCodecTest::encodesFtpRequestsAndRejectsInvalidValues()
{
    BoardApiCodec codec;
    FtpConfigUpdate config;
    config.expectedRevision = QStringLiteral("v1-revision");
    config.deviceId = QStringLiteral("rv1126b_001");
    config.retryMax = 5;
    config.retryIntervalSec = 30;
    config.connectTimeoutSec = 10;
    config.transferTimeoutSec = 120;
    config.scanIntervalSec = 5;
    FtpTargetUpdate target;
    target.id = QStringLiteral("server1");
    target.enabled = true;
    target.host = QStringLiteral("192.168.137.1");
    target.port = 21;
    target.user = QStringLiteral("upload");
    target.passwordAction.value = FtpPasswordAction::Keep;
    target.passwordAction.rawValue = QStringLiteral("keep");
    target.remoteDir = QStringLiteral("/vehicle_events");
    config.targets.append(target);

    const auto encodedConfig = codec.encodeFtpConfig(config);
    QVERIFY(encodedConfig.isSuccess());
    const QJsonObject configObject = jsonObject(encodedConfig.value());
    QCOMPARE(configObject.value(QStringLiteral("targets")).toArray().size(), 1);
    QVERIFY(!configObject.contains(QStringLiteral("password")));
    QCOMPARE(configObject.value(QStringLiteral("targets")).toArray().first().toObject()
                 .value(QStringLiteral("password_action")).toString(),
             QStringLiteral("keep"));

    config.targets[0].passwordAction.value = FtpPasswordAction::Replace;
    config.targets[0].passwordAction.rawValue.clear();
    config.targets[0].replacementPassword = QStringLiteral("secret");
    const auto encodedReplaceConfig = codec.encodeFtpConfig(config);
    QVERIFY(encodedReplaceConfig.isSuccess());
    const QJsonObject replaceTarget = jsonObject(encodedReplaceConfig.value())
                                          .value(QStringLiteral("targets")).toArray().first().toObject();
    QCOMPARE(replaceTarget.value(QStringLiteral("password_action")).toString(),
             QStringLiteral("replace"));
    QCOMPARE(replaceTarget.value(QStringLiteral("password")).toString(), QStringLiteral("secret"));

    FtpControlUpdate control;
    control.expectedRevision = QStringLiteral("v1-revision");
    control.enabled = true;
    control.scope.value = FtpControlScope::NewEventsOnly;
    control.scope.rawValue = QStringLiteral("new_events_only");
    QVERIFY(codec.encodeFtpControl(control).isSuccess());

    FtpTaskCreate task;
    task.startEpochMs = 1783900800000LL;
    task.endEpochMs = 1783987200000LL;
    task.targetIds = {QStringLiteral("server1")};
    QVERIFY(codec.encodeFtpTaskCreate(task).isSuccess());

    task.endEpochMs = task.startEpochMs;
    const auto invalidTask = codec.encodeFtpTaskCreate(task);
    QVERIFY(!invalidTask.isSuccess());
    QCOMPARE(invalidTask.error().code, QStringLiteral("invalid_ftp_task"));

    ClientAckCreate ack;
    ack.clientId = QStringLiteral("qt_primary");
    ack.evidenceSize = 4096;
    const auto encodedAck = codec.encodeClientAck(ack);
    QVERIFY(encodedAck.isSuccess());
    QCOMPARE(jsonObject(encodedAck.value()).value(QStringLiteral("client_id")).toString(), ack.clientId);
    QCOMPARE(jsonObject(encodedAck.value()).value(QStringLiteral("evidence_size")).toInteger(), ack.evidenceSize);

    const auto parsedAck = codec.parseClientAck(QByteArrayLiteral(R"({
        "api_version":"v1",
        "ack":{"schema_version":1,"device_id":"rv1126b_001","client_id":"qt_primary","event_id":1,"track_id":2,"evidence_size":4096,"persisted_epoch_ms":1784002847389}
    })"));
    QVERIFY(parsedAck.isSuccess());
    QCOMPARE(parsedAck.value().clientId, QStringLiteral("qt_primary"));
    QCOMPARE(parsedAck.value().trackId, 2LL);
}

QTEST_MAIN(ApiCodecTest)

#include "ApiCodecTest.moc"
