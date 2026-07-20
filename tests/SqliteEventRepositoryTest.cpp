#include "../src/rv1126b/storage/SqliteEventRepository.h"

#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include <optional>

using namespace rv1126b;

class SqliteEventRepositoryTest final : public QObject
{
    Q_OBJECT

private slots:
    void initializesSchemaAndPersistsDevices();
    void upsertsEventsByCompositeIdentity();
    void storesEvidenceAnchorsAndFtpSnapshots();
    void queriesFtpTaskSnapshotsWithTargets();
    void migratesEvidenceCacheLocalPathToNullable();
};

namespace {

DeviceProfile makeProfile()
{
    DeviceProfile profile;
    profile.deviceId = QStringLiteral("rv-device-001");
    profile.deviceModel = QStringLiteral("RV1126B");
    profile.releaseVersion = QStringLiteral("2026.07.test");
    profile.endpoint.ipv4 = QStringLiteral("192.168.10.20");
    profile.endpoint.apiBaseUrl = QUrl(QStringLiteral("http://192.168.10.20:18080"));
    profile.endpoint.httpPort = 18080;
    profile.endpoint.discoveryPort = 18081;
    profile.credentialRef = QStringLiteral("credential/device-001");
    profile.advertisedCapabilities = {QStringLiteral("events"), QStringLiteral("evidence")};
    profile.lastOnlineEpochMs = 1784280000000;
    return profile;
}

VehicleEvent makeEvent(OcrStatus ocrStatus, qint64 updatedEpochMs)
{
    VehicleEvent event;
    event.identity.deviceId = QStringLiteral("rv-device-001");
    event.identity.eventId = 101;
    event.identity.trackId = 7;
    event.eventTime.epochMs = 1784280000123;
    event.eventTime.sourceEpochMs = 1784280000123;
    event.eventTime.offsetAppliedMs = 0;
    event.eventTime.quality.value = TimeQuality::NativeUtc;
    event.eventTime.quality.rawValue = QStringLiteral("native_utc");
    event.motionDirection = QStringLiteral("north_to_south");
    event.speedKmh = 72;
    event.speedValid = true;
    event.speedStatus = QStringLiteral("overspeed");
    event.ocrStatus.value = ocrStatus;
    event.ocrStatus.rawValue = ocrStatus == OcrStatus::Queued ? QStringLiteral("queued") : QStringLiteral("matched");
    event.plateText = ocrStatus == OcrStatus::Queued ? QString() : QStringLiteral("粤B12345");
    event.plateAscii = ocrStatus == OcrStatus::Queued ? QString() : QStringLiteral("YUEB12345");
    event.plateColor = ocrStatus == OcrStatus::Queued ? QString() : QStringLiteral("blue");
    event.evidenceStatus = QStringLiteral("available");
    event.evidenceAvailable = true;
    event.detailRelativeUrl = QStringLiteral("/api/v1/events/101/7");
    event.evidenceRelativeUrl = QStringLiteral("/api/v1/events/101/7/evidence.jpg");
    event.firstSeenEpochMs = 1784280001000;
    event.lastUpdatedEpochMs = updatedEpochMs;
    return event;
}

template<typename T>
std::optional<ApiResult<T>> callResult(std::function<RequestId(ApiCompletion<T>)> invoker)
{
    std::optional<ApiResult<T>> result;
    invoker([&result](ApiResult<T> value) {
        result.emplace(std::move(value));
    });
    return result;
}

QString databasePath(QTemporaryDir& tempDir)
{
    return QDir(tempDir.path()).filePath(QStringLiteral("rv1126b.sqlite"));
}

StoredFtpTargetStatus makeFtpTarget(
    const QString& targetId,
    FtpTaskState state,
    int done,
    int failed,
    const QString& lastError = QString())
{
    StoredFtpTargetStatus target;
    target.targetId = targetId;
    target.state.value = state;
    target.state.rawValue = state == FtpTaskState::Running ? QStringLiteral("running") : QStringLiteral("failed");
    target.total = 10;
    target.pending = 10 - done - failed;
    target.done = done;
    target.failed = failed;
    target.attempts = done + failed;
    target.lastError = lastError;
    return target;
}

StoredFtpTask makeFtpTask(
    const QString& taskId,
    qint64 startEpochMs,
    qint64 endEpochMs,
    FtpTaskState state,
    qint64 createdEpochMs)
{
    StoredFtpTask task;
    task.deviceId = QStringLiteral("rv-device-001");
    task.taskId = taskId;
    task.startEpochMs = startEpochMs;
    task.endEpochMs = endEpochMs;
    task.state.value = state;
    task.state.rawValue = state == FtpTaskState::Running ? QStringLiteral("running") : QStringLiteral("done");
    task.createdEpochMs = createdEpochMs;
    task.refreshedEpochMs = createdEpochMs + 1000;
    return task;
}

} // namespace

void SqliteEventRepositoryTest::initializesSchemaAndPersistsDevices()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    SqliteEventRepository repository(databasePath(tempDir));

    auto init = callResult<void>([&](ApiCompletion<void> completion) {
        return repository.initialize(this, std::move(completion));
    });
    QVERIFY(init.has_value());
    if (!init->isSuccess()) {
        QFAIL(qPrintable(init->error().message));
    }

    auto secondInit = callResult<void>([&](ApiCompletion<void> completion) {
        return repository.initialize(this, std::move(completion));
    });
    QVERIFY(secondInit.has_value());
    QVERIFY(secondInit->isSuccess());

    const DeviceProfile profile = makeProfile();
    auto saved = callResult<void>([&](ApiCompletion<void> completion) {
        return repository.upsertDevice(profile, this, std::move(completion));
    });
    QVERIFY(saved.has_value());
    QVERIFY(saved->isSuccess());

    auto loaded = callResult<QVector<DeviceProfile>>([&](ApiCompletion<QVector<DeviceProfile>> completion) {
        return repository.loadDeviceProfiles(this, std::move(completion));
    });
    QVERIFY(loaded.has_value());
    QVERIFY(loaded->isSuccess());
    QCOMPARE(loaded->value().size(), 1);
    QCOMPARE(loaded->value().first().deviceId, profile.deviceId);
    QCOMPARE(loaded->value().first().endpoint.apiBaseUrl, profile.endpoint.apiBaseUrl);
    QCOMPARE(loaded->value().first().advertisedCapabilities, profile.advertisedCapabilities);
}

void SqliteEventRepositoryTest::upsertsEventsByCompositeIdentity()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    SqliteEventRepository repository(databasePath(tempDir));
    auto init = callResult<void>([&](ApiCompletion<void> completion) {
        return repository.initialize(this, std::move(completion));
    });
    QVERIFY(init.has_value());
    if (!init->isSuccess()) {
        QFAIL(qPrintable(init->error().message));
    }

    auto queuedSave = callResult<void>([&](ApiCompletion<void> completion) {
        return repository.upsertEvents({makeEvent(OcrStatus::Queued, 1784280002000)}, this, std::move(completion));
    });
    QVERIFY(queuedSave.has_value());
    if (!queuedSave->isSuccess()) {
        QFAIL(qPrintable(queuedSave->error().message));
    }

    auto nonTerminal = callResult<QVector<VehicleEvent>>([&](ApiCompletion<QVector<VehicleEvent>> completion) {
        return repository.loadNonTerminalEvents(QStringLiteral("rv-device-001"), this, std::move(completion));
    });
    QVERIFY(nonTerminal.has_value());
    QVERIFY(nonTerminal->isSuccess());
    QCOMPARE(nonTerminal->value().size(), 1);
    QCOMPARE(nonTerminal->value().first().ocrStatus.value, OcrStatus::Queued);

    auto matchedSave = callResult<void>([&](ApiCompletion<void> completion) {
        return repository.upsertEvents({makeEvent(OcrStatus::Matched, 1784280003000)}, this, std::move(completion));
    });
    QVERIFY(matchedSave.has_value());
    QVERIFY(matchedSave->isSuccess());

    EventQuery query;
    query.deviceId = QStringLiteral("rv-device-001");
    query.limit = 20;
    query.newestFirst = true;
    auto rows = callResult<QVector<VehicleEvent>>([&](ApiCompletion<QVector<VehicleEvent>> completion) {
        return repository.queryEvents(query, this, std::move(completion));
    });
    QVERIFY(rows.has_value());
    QVERIFY(rows->isSuccess());
    QCOMPARE(rows->value().size(), 1);
    QCOMPARE(rows->value().first().identity.eventId, 101);
    QCOMPARE(rows->value().first().identity.trackId, 7);
    QCOMPARE(rows->value().first().ocrStatus.value, OcrStatus::Matched);
    QCOMPARE(rows->value().first().plateText, QStringLiteral("粤B12345"));
    QCOMPARE(rows->value().first().lastUpdatedEpochMs, 1784280003000);

    auto afterTerminal = callResult<QVector<VehicleEvent>>([&](ApiCompletion<QVector<VehicleEvent>> completion) {
        return repository.loadNonTerminalEvents(QStringLiteral("rv-device-001"), this, std::move(completion));
    });
    QVERIFY(afterTerminal.has_value());
    QVERIFY(afterTerminal->isSuccess());
    QCOMPARE(afterTerminal->value().size(), 0);
}

void SqliteEventRepositoryTest::storesEvidenceAnchorsAndFtpSnapshots()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = databasePath(tempDir);
    SqliteEventRepository repository(dbPath);
    auto init = callResult<void>([&](ApiCompletion<void> completion) {
        return repository.initialize(this, std::move(completion));
    });
    QVERIFY(init.has_value());
    QVERIFY(init->isSuccess());

    const EventIdentity identity {QStringLiteral("rv-device-001"), 101, 7};

    EvidenceCacheEntry evidence;
    evidence.identity = identity;
    evidence.role = QStringLiteral("evidence");
    evidence.remoteRelativeUrl = QStringLiteral("/api/v1/events/101/7/evidence.jpg");
    evidence.localFilePath = QDir(tempDir.path()).filePath(QStringLiteral("evidence.jpg"));
    evidence.contentLength = 4096;
    evidence.status = EvidenceCacheStatus::Available;
    evidence.updatedEpochMs = 1784280004000;

    auto savedEvidence = callResult<void>([&](ApiCompletion<void> completion) {
        return repository.saveEvidenceState(evidence, this, std::move(completion));
    });
    QVERIFY(savedEvidence.has_value());
    if (!savedEvidence->isSuccess()) {
        QFAIL(qPrintable(savedEvidence->error().message));
    }

    auto loadedEvidence = callResult<std::optional<EvidenceCacheEntry>>(
        [&](ApiCompletion<std::optional<EvidenceCacheEntry>> completion) {
            return repository.loadEvidenceState(identity, QStringLiteral("evidence"), this, std::move(completion));
        });
    QVERIFY(loadedEvidence.has_value());
    QVERIFY(loadedEvidence->isSuccess());
    QVERIFY(loadedEvidence->value().has_value());
    QCOMPARE(loadedEvidence->value()->status, EvidenceCacheStatus::Available);
    QCOMPARE(loadedEvidence->value()->contentLength, 4096);

    SyncAnchor anchor;
    anchor.deviceId = QStringLiteral("rv-device-001");
    anchor.previousHead.sourceEpochMs = 1784280000123;
    anchor.previousHead.eventId = 101;
    anchor.previousHead.trackId = 7;
    anchor.savedEpochMs = 1784280005000;

    auto savedAnchor = callResult<void>([&](ApiCompletion<void> completion) {
        return repository.saveSyncAnchor(anchor, this, std::move(completion));
    });
    QVERIFY(savedAnchor.has_value());
    QVERIFY(savedAnchor->isSuccess());

    auto loadedAnchor = callResult<std::optional<SyncAnchor>>(
        [&](ApiCompletion<std::optional<SyncAnchor>> completion) {
            return repository.loadSyncAnchor(QStringLiteral("rv-device-001"), this, std::move(completion));
        });
    QVERIFY(loadedAnchor.has_value());
    QVERIFY(loadedAnchor->isSuccess());
    QVERIFY(loadedAnchor->value().has_value());
    QCOMPARE(loadedAnchor->value()->previousHead.eventId, 101);
    QCOMPARE(loadedAnchor->value()->savedEpochMs, 1784280005000);

    StoredFtpTask task = makeFtpTask(
        QStringLiteral("ftp-task-001"),
        1784270000000,
        1784280000000,
        FtpTaskState::Running,
        1784280006000);
    task.targets.append(makeFtpTarget(QStringLiteral("target-a"), FtpTaskState::Running, 3, 0));

    auto savedTask = callResult<void>([&](ApiCompletion<void> completion) {
        return repository.saveFtpTaskSnapshot(task, this, std::move(completion));
    });
    QVERIFY(savedTask.has_value());
    QVERIFY(savedTask->isSuccess());

    FtpTaskQuery ftpQuery;
    ftpQuery.deviceId = QStringLiteral("rv-device-001");
    auto loadedTasks = callResult<QVector<StoredFtpTask>>(
        [&](ApiCompletion<QVector<StoredFtpTask>> completion) {
            return repository.loadFtpTaskSnapshots(ftpQuery, this, std::move(completion));
        });
    QVERIFY(loadedTasks.has_value());
    QVERIFY(loadedTasks->isSuccess());
    QCOMPARE(loadedTasks->value().size(), 1);
    QCOMPARE(loadedTasks->value().first().taskId, QStringLiteral("ftp-task-001"));
    QCOMPARE(loadedTasks->value().first().targets.size(), 1);
    QCOMPARE(loadedTasks->value().first().targets.first().targetId, QStringLiteral("target-a"));
    QCOMPARE(loadedTasks->value().first().targets.first().done, 3);
}

void SqliteEventRepositoryTest::queriesFtpTaskSnapshotsWithTargets()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    SqliteEventRepository repository(databasePath(tempDir));
    auto init = callResult<void>([&](ApiCompletion<void> completion) {
        return repository.initialize(this, std::move(completion));
    });
    QVERIFY(init.has_value());
    QVERIFY(init->isSuccess());

    StoredFtpTask older = makeFtpTask(
        QStringLiteral("ftp-task-old"),
        1784260000000,
        1784261000000,
        FtpTaskState::Done,
        1784262000000);
    older.targets.append(makeFtpTarget(QStringLiteral("target-a"), FtpTaskState::Running, 10, 0));

    StoredFtpTask newer = makeFtpTask(
        QStringLiteral("ftp-task-new"),
        1784270000000,
        1784280000000,
        FtpTaskState::Running,
        1784280006000);
    newer.targets.append(makeFtpTarget(QStringLiteral("target-a"), FtpTaskState::Running, 4, 0));
    newer.targets.append(makeFtpTarget(QStringLiteral("target-b"), FtpTaskState::Failed, 2, 1, QStringLiteral("ftp_timeout")));

    for (const StoredFtpTask& task : {older, newer}) {
        auto saved = callResult<void>([&](ApiCompletion<void> completion) {
            return repository.saveFtpTaskSnapshot(task, this, std::move(completion));
        });
        QVERIFY(saved.has_value());
        if (!saved->isSuccess()) {
            QFAIL(qPrintable(saved->error().message));
        }
    }

    newer.state.value = FtpTaskState::Done;
    newer.state.rawValue = QStringLiteral("done");
    newer.refreshedEpochMs = 1784280010000;
    newer.targets.clear();
    newer.targets.append(makeFtpTarget(QStringLiteral("target-a"), FtpTaskState::Running, 10, 0));
    auto updated = callResult<void>([&](ApiCompletion<void> completion) {
        return repository.saveFtpTaskSnapshot(newer, this, std::move(completion));
    });
    QVERIFY(updated.has_value());
    QVERIFY(updated->isSuccess());

    FtpTaskQuery query;
    query.deviceId = QStringLiteral("rv-device-001");
    query.startEpochMs = 1784265000000;
    query.endEpochMs = 1784285000000;
    query.limit = 10;
    query.newestFirst = true;

    auto loaded = callResult<QVector<StoredFtpTask>>([&](ApiCompletion<QVector<StoredFtpTask>> completion) {
        return repository.loadFtpTaskSnapshots(query, this, std::move(completion));
    });
    QVERIFY(loaded.has_value());
    QVERIFY(loaded->isSuccess());
    QCOMPARE(loaded->value().size(), 1);
    QCOMPARE(loaded->value().first().taskId, QStringLiteral("ftp-task-new"));
    QCOMPARE(loaded->value().first().state.value, FtpTaskState::Done);
    QCOMPARE(loaded->value().first().targets.size(), 1);
    QCOMPARE(loaded->value().first().targets.first().targetId, QStringLiteral("target-a"));
    QCOMPARE(loaded->value().first().targets.first().done, 10);
}

void SqliteEventRepositoryTest::migratesEvidenceCacheLocalPathToNullable()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = databasePath(tempDir);
    const QString setupConnection = QStringLiteral("setup-old-evidence-%1").arg(reinterpret_cast<quintptr>(this));
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), setupConnection);
        database.setDatabaseName(dbPath);
        QVERIFY(database.open());

        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE rv_evidence_cache ("
            "device_id TEXT NOT NULL,"
            "event_id INTEGER NOT NULL,"
            "track_id INTEGER NOT NULL,"
            "role TEXT NOT NULL,"
            "remote_relative_url TEXT NOT NULL,"
            "local_file_path TEXT NOT NULL,"
            "content_length INTEGER NOT NULL,"
            "status_value INTEGER NOT NULL,"
            "failure_code TEXT,"
            "updated_epoch_ms INTEGER NOT NULL,"
            "PRIMARY KEY (device_id, event_id, track_id, role)"
            ")")));
        database.close();
    }
    QSqlDatabase::removeDatabase(setupConnection);

    SqliteEventRepository repository(dbPath);
    auto init = callResult<void>([&](ApiCompletion<void> completion) {
        return repository.initialize(this, std::move(completion));
    });
    QVERIFY(init.has_value());
    if (!init->isSuccess()) {
        QFAIL(qPrintable(init->error().message));
    }

    EvidenceCacheEntry failedEvidence;
    failedEvidence.identity = {QStringLiteral("rv-device-001"), 404, 9};
    failedEvidence.role = QStringLiteral("evidence");
    failedEvidence.remoteRelativeUrl = QStringLiteral("/api/v1/events/404/9/evidence.jpg");
    failedEvidence.status = EvidenceCacheStatus::Failed;
    failedEvidence.failureCode = QStringLiteral("rv1126b.evidence.invalid_jpeg");
    failedEvidence.updatedEpochMs = 1784280010000;

    auto saved = callResult<void>([&](ApiCompletion<void> completion) {
        return repository.saveEvidenceState(failedEvidence, this, std::move(completion));
    });
    QVERIFY(saved.has_value());
    if (!saved->isSuccess()) {
        QFAIL(qPrintable(saved->error().message));
    }

    auto loaded = callResult<std::optional<EvidenceCacheEntry>>(
        [&](ApiCompletion<std::optional<EvidenceCacheEntry>> completion) {
            return repository.loadEvidenceState(
                failedEvidence.identity,
                QStringLiteral("evidence"),
                this,
                std::move(completion));
        });
    QVERIFY(loaded.has_value());
    QVERIFY(loaded->isSuccess());
    QVERIFY(loaded->value().has_value());
    QCOMPARE(loaded->value()->status, EvidenceCacheStatus::Failed);
    QCOMPARE(loaded->value()->localFilePath, QString());
}

QTEST_MAIN(SqliteEventRepositoryTest)

#include "SqliteEventRepositoryTest.moc"
