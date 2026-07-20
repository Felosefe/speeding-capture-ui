#include "../src/rv1126b/services/BoardEvidenceCache.h"
#include "../src/rv1126b/storage/SqliteEventRepository.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <functional>
#include <optional>

using namespace rv1126b;

class EvidenceCacheTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void downloadsValidJpegPromotesPartFileAndPersistsAvailable();
    void rejectsInvalidJpegRemovesPartAndPersistsFailed();
    void limitsDownloadsToOnePerDevice();
    void retriesConflictAndPromotesImage();
    void cancelPreventsScheduledRetry();
    void cancelAllRemovesActivePartFile();
};

namespace {

ApiError makeError(const QString& code, ApiErrorCategory category = ApiErrorCategory::Temporary, bool retryable = true)
{
    ApiError error;
    error.code = code;
    error.message = QStringLiteral("test error");
    error.category = category;
    error.retryable = retryable;
    return error;
}

VehicleEvent makeEvent(qint64 eventId, const QString& deviceId = QStringLiteral("rv-device-001"))
{
    VehicleEvent event;
    event.identity.deviceId = deviceId;
    event.identity.eventId = eventId;
    event.identity.trackId = 7;
    event.evidenceAvailable = true;
    event.evidenceStatus = QStringLiteral("available");
    event.evidenceRelativeUrl = QStringLiteral("/api/v1/events/%1/7/evidence.jpg").arg(eventId);
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
    return QDir(tempDir.path()).filePath(QStringLiteral("rv1126b-evidence.sqlite"));
}

bool writeJpeg(const QString& path)
{
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(qRgb(20, 120, 200));
    return image.save(path, "JPEG");
}

bool writeInvalidFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write("not a jpeg");
    return true;
}

class FakeBoardApiClient final : public IBoardApiClient
{
public:
    enum class DownloadMode {
        ValidJpeg,
        InvalidJpeg,
        Failure,
        Hold
    };

    struct HeldDownload {
        EventIdentity identity;
        QString partFilePath;
        ApiCompletion<EvidenceDownloadResult> completion;
    };

    DownloadMode mode = DownloadMode::ValidJpeg;
    QVector<EventIdentity> requestedIdentities;
    QVector<QString> requestedPartPaths;
    QVector<HeldDownload> heldDownloads;
    int cancelCalls = 0;
    int failuresRemaining = 0;
    int failureHttpStatus = 0;

    RequestId getHealth(QObject*, ApiCompletion<HealthDto> completion) override
    {
        completion(ApiResult<HealthDto>::failure(makeError(QStringLiteral("not_implemented"))));
        return RequestId::createUuid();
    }

    RequestId listEvents(int, const std::optional<QString>&, QObject*, ApiCompletion<EventPageDto> completion) override
    {
        completion(ApiResult<EventPageDto>::failure(makeError(QStringLiteral("not_implemented"))));
        return RequestId::createUuid();
    }

    RequestId getEventDetail(const EventIdentity&, QObject*, ApiCompletion<EventDetailDto> completion) override
    {
        completion(ApiResult<EventDetailDto>::failure(makeError(QStringLiteral("not_implemented"))));
        return RequestId::createUuid();
    }

    RequestId downloadEvidenceToPartFile(
        const EventIdentity& identity,
        const QString&,
        const QString& partFilePath,
        QObject*,
        ApiCompletion<EvidenceDownloadResult> completion) override
    {
        requestedIdentities.append(identity);
        requestedPartPaths.append(partFilePath);
        if (mode == DownloadMode::Hold) {
            heldDownloads.append({identity, partFilePath, std::move(completion)});
            return RequestId::createUuid();
        }

        const bool shouldFail = failuresRemaining > 0;
        if (shouldFail) {
            --failuresRemaining;
        }
        const DownloadMode requestedMode = shouldFail ? DownloadMode::Failure : mode;
        const int requestedFailureStatus = failureHttpStatus;
        QTimer::singleShot(0, [partFilePath, completion = std::move(completion), requestedMode, requestedFailureStatus]() mutable {
            if (requestedMode == DownloadMode::Failure) {
                ApiError error = makeError(QStringLiteral("network_retry"), ApiErrorCategory::Temporary, true);
                error.httpStatus = requestedFailureStatus;
                completion(ApiResult<EvidenceDownloadResult>::failure(std::move(error)));
                return;
            }

            const bool wrote = requestedMode == DownloadMode::ValidJpeg
                ? writeJpeg(partFilePath)
                : writeInvalidFile(partFilePath);
            if (!wrote) {
                completion(ApiResult<EvidenceDownloadResult>::failure(
                    makeError(QStringLiteral("write_failed"), ApiErrorCategory::Storage, false)));
                return;
            }

            EvidenceDownloadResult result;
            result.partFilePath = partFilePath;
            result.contentType = QStringLiteral("image/jpeg");
            result.expectedContentLength = QFileInfo(partFilePath).size();
            result.receivedBytes = QFileInfo(partFilePath).size();
            completion(ApiResult<EvidenceDownloadResult>::success(result));
        });
        return RequestId::createUuid();
    }

    void completeHeldFirst()
    {
        QVERIFY(!heldDownloads.isEmpty());
        HeldDownload held = std::move(heldDownloads.first());
        heldDownloads.removeFirst();
        QVERIFY(writeJpeg(held.partFilePath));

        EvidenceDownloadResult result;
        result.partFilePath = held.partFilePath;
        result.contentType = QStringLiteral("image/jpeg");
        result.expectedContentLength = QFileInfo(held.partFilePath).size();
        result.receivedBytes = QFileInfo(held.partFilePath).size();
        held.completion(ApiResult<EvidenceDownloadResult>::success(result));
    }

    RequestId getEvidenceConfig(QObject*, ApiCompletion<EvidenceConfigDto> completion) override
    {
        completion(ApiResult<EvidenceConfigDto>::failure(makeError(QStringLiteral("not_implemented"))));
        return RequestId::createUuid();
    }

    RequestId putEvidenceConfig(const EvidenceConfigUpdate&, QObject*, ApiCompletion<EvidenceConfigDto> completion) override
    {
        completion(ApiResult<EvidenceConfigDto>::failure(makeError(QStringLiteral("not_implemented"))));
        return RequestId::createUuid();
    }

    RequestId getTime(QObject*, ApiCompletion<TimeStatusDto> completion) override
    {
        completion(ApiResult<TimeStatusDto>::failure(makeError(QStringLiteral("not_implemented"))));
        return RequestId::createUuid();
    }

    RequestId putTime(const TimeUpdate&, QObject*, ApiCompletion<TimeStatusDto> completion) override
    {
        completion(ApiResult<TimeStatusDto>::failure(makeError(QStringLiteral("not_implemented"))));
        return RequestId::createUuid();
    }

    RequestId getFtpConfig(QObject*, ApiCompletion<FtpConfigSnapshotDto> completion) override
    {
        completion(ApiResult<FtpConfigSnapshotDto>::failure(makeError(QStringLiteral("not_implemented"))));
        return RequestId::createUuid();
    }

    RequestId putFtpConfig(const FtpConfigUpdate&, QObject*, ApiCompletion<FtpConfigSnapshotDto> completion) override
    {
        completion(ApiResult<FtpConfigSnapshotDto>::failure(makeError(QStringLiteral("not_implemented"))));
        return RequestId::createUuid();
    }

    RequestId rollbackFtpConfig(const QString&, QObject*, ApiCompletion<FtpConfigSnapshotDto> completion) override
    {
        completion(ApiResult<FtpConfigSnapshotDto>::failure(makeError(QStringLiteral("not_implemented"))));
        return RequestId::createUuid();
    }

    RequestId getFtpControl(QObject*, ApiCompletion<FtpControlDto> completion) override
    {
        completion(ApiResult<FtpControlDto>::failure(makeError(QStringLiteral("not_implemented"))));
        return RequestId::createUuid();
    }

    RequestId putFtpControl(const FtpControlUpdate&, QObject*, ApiCompletion<FtpControlDto> completion) override
    {
        completion(ApiResult<FtpControlDto>::failure(makeError(QStringLiteral("not_implemented"))));
        return RequestId::createUuid();
    }

    RequestId createFtpTask(const FtpTaskCreate&, QObject*, ApiCompletion<FtpTaskDetailDto> completion) override
    {
        completion(ApiResult<FtpTaskDetailDto>::failure(makeError(QStringLiteral("not_implemented"))));
        return RequestId::createUuid();
    }

    RequestId listFtpTasks(int, const std::optional<QString>&, QObject*, ApiCompletion<FtpTaskPageDto> completion) override
    {
        completion(ApiResult<FtpTaskPageDto>::failure(makeError(QStringLiteral("not_implemented"))));
        return RequestId::createUuid();
    }

    RequestId getFtpTask(const QString&, QObject*, ApiCompletion<FtpTaskDetailDto> completion) override
    {
        completion(ApiResult<FtpTaskDetailDto>::failure(makeError(QStringLiteral("not_implemented"))));
        return RequestId::createUuid();
    }

    RequestId retryFtpTask(const QString&, QObject*, ApiCompletion<FtpTaskDetailDto> completion) override
    {
        completion(ApiResult<FtpTaskDetailDto>::failure(makeError(QStringLiteral("not_implemented"))));
        return RequestId::createUuid();
    }

    void cancel(const RequestId&) override
    {
        ++cancelCalls;
    }

    void cancelAll() override
    {
        ++cancelCalls;
    }
};

void initializeRepository(SqliteEventRepository& repository, QObject* context)
{
    auto init = callResult<void>([&](ApiCompletion<void> completion) {
        return repository.initialize(context, std::move(completion));
    });
    QVERIFY(init.has_value());
    if (!init->isSuccess()) {
        QFAIL(qPrintable(init->error().message));
    }
}

EvidenceCacheEntry loadEvidence(
    SqliteEventRepository& repository,
    QObject* context,
    const EventIdentity& identity)
{
    auto loaded = callResult<std::optional<EvidenceCacheEntry>>(
        [&](ApiCompletion<std::optional<EvidenceCacheEntry>> completion) {
            return repository.loadEvidenceState(identity, QStringLiteral("evidence"), context, std::move(completion));
        });
    if (!loaded || !loaded->isSuccess() || !loaded->value().has_value()) {
        QTest::qFail("Failed to load evidence state", __FILE__, __LINE__);
        return {};
    }
    return *loaded->value();
}

} // namespace

void EvidenceCacheTest::initTestCase()
{
    qRegisterMetaType<rv1126b::ApiError>();
    qRegisterMetaType<rv1126b::EvidenceCacheEntry>();
}

void EvidenceCacheTest::downloadsValidJpegPromotesPartFileAndPersistsAvailable()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    SqliteEventRepository repository(databasePath(tempDir));
    initializeRepository(repository, this);

    FakeBoardApiClient client;
    const VehicleEvent event = makeEvent(501);
    BoardEvidenceCache cache(&client, &repository, QDir(tempDir.path()).filePath(QStringLiteral("cache")));
    QSignalSpy stateSpy(&cache, &EvidenceCache::stateChanged);
    QSignalSpy errorSpy(&cache, &EvidenceCache::cacheError);

    cache.enqueue(event);

    QTRY_VERIFY(QFileInfo::exists(cache.finalPathFor(event)));
    QVERIFY(stateSpy.size() >= 1);
    QCOMPARE(errorSpy.size(), 0);

    const QString finalPath = cache.finalPathFor(event);
    QVERIFY(QFileInfo::exists(finalPath));
    QVERIFY(!QFileInfo::exists(finalPath + QStringLiteral(".part")));

    const EvidenceCacheEntry entry = loadEvidence(repository, this, event.identity);
    QCOMPARE(entry.status, EvidenceCacheStatus::Available);
    QCOMPARE(entry.localFilePath, finalPath);
    QVERIFY(entry.contentLength > 0);
}

void EvidenceCacheTest::rejectsInvalidJpegRemovesPartAndPersistsFailed()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    SqliteEventRepository repository(databasePath(tempDir));
    initializeRepository(repository, this);

    FakeBoardApiClient client;
    client.mode = FakeBoardApiClient::DownloadMode::InvalidJpeg;
    const VehicleEvent event = makeEvent(502);
    BoardEvidenceCache cache(&client, &repository, QDir(tempDir.path()).filePath(QStringLiteral("cache")));
    QSignalSpy stateSpy(&cache, &EvidenceCache::stateChanged);
    QSignalSpy errorSpy(&cache, &EvidenceCache::cacheError);

    cache.enqueue(event);

    QTRY_VERIFY(errorSpy.size() >= 1);
    QVERIFY(stateSpy.size() >= 1);

    const QString finalPath = cache.finalPathFor(event);
    QVERIFY(!QFileInfo::exists(finalPath));
    QVERIFY(!QFileInfo::exists(finalPath + QStringLiteral(".part")));

    const EvidenceCacheEntry entry = loadEvidence(repository, this, event.identity);
    QCOMPARE(entry.status, EvidenceCacheStatus::Failed);
    QCOMPARE(entry.failureCode, QStringLiteral("rv1126b.evidence.invalid_jpeg"));
}

void EvidenceCacheTest::limitsDownloadsToOnePerDevice()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    SqliteEventRepository repository(databasePath(tempDir));
    initializeRepository(repository, this);

    FakeBoardApiClient client;
    client.mode = FakeBoardApiClient::DownloadMode::Hold;
    BoardEvidenceCache cache(&client, &repository, QDir(tempDir.path()).filePath(QStringLiteral("cache")));
    QSignalSpy stateSpy(&cache, &EvidenceCache::stateChanged);

    const VehicleEvent first = makeEvent(601);
    const VehicleEvent second = makeEvent(602);
    cache.enqueue(first);
    cache.enqueue(second);

    QTRY_COMPARE(client.requestedIdentities.size(), 1);
    QCOMPARE(client.requestedIdentities.first(), first.identity);

    client.completeHeldFirst();
    QTRY_COMPARE(client.requestedIdentities.size(), 2);
    QCOMPARE(client.requestedIdentities.at(1), second.identity);
    QVERIFY(stateSpy.size() >= 1);
}

void EvidenceCacheTest::retriesConflictAndPromotesImage()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    SqliteEventRepository repository(databasePath(tempDir));
    initializeRepository(repository, this);

    FakeBoardApiClient client;
    client.failuresRemaining = 1;
    client.failureHttpStatus = 409;
    const VehicleEvent event = makeEvent(701);
    BoardEvidenceCache cache(&client, &repository, QDir(tempDir.path()).filePath(QStringLiteral("cache")));

    cache.enqueue(event);

    QTRY_COMPARE_WITH_TIMEOUT(client.requestedIdentities.size(), 2, 2500);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(cache.finalPathFor(event)), 2500);
    const EvidenceCacheEntry entry = loadEvidence(repository, this, event.identity);
    QCOMPARE(entry.status, EvidenceCacheStatus::Available);
}

void EvidenceCacheTest::cancelPreventsScheduledRetry()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    SqliteEventRepository repository(databasePath(tempDir));
    initializeRepository(repository, this);

    FakeBoardApiClient client;
    client.mode = FakeBoardApiClient::DownloadMode::Failure;
    const VehicleEvent event = makeEvent(702);
    BoardEvidenceCache cache(&client, &repository, QDir(tempDir.path()).filePath(QStringLiteral("cache")));

    cache.enqueue(event);
    QTRY_COMPARE(client.requestedIdentities.size(), 1);
    QTest::qWait(50);
    cache.cancel(event.identity);

    QTest::qWait(1200);
    QCOMPARE(client.requestedIdentities.size(), 1);
    const EvidenceCacheEntry entry = loadEvidence(repository, this, event.identity);
    QCOMPARE(entry.status, EvidenceCacheStatus::RetryWait);
}

void EvidenceCacheTest::cancelAllRemovesActivePartFile()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    SqliteEventRepository repository(databasePath(tempDir));
    initializeRepository(repository, this);

    FakeBoardApiClient client;
    client.mode = FakeBoardApiClient::DownloadMode::Hold;
    const VehicleEvent event = makeEvent(703);
    BoardEvidenceCache cache(&client, &repository, QDir(tempDir.path()).filePath(QStringLiteral("cache")));

    cache.enqueue(event);
    QTRY_COMPARE(client.requestedPartPaths.size(), 1);
    const QString partPath = client.requestedPartPaths.first();
    QVERIFY(writeInvalidFile(partPath));
    QVERIFY(QFileInfo::exists(partPath));

    cache.cancelAll();
    QCOMPARE(client.cancelCalls, 1);
    QVERIFY(!QFileInfo::exists(partPath));
}

QTEST_MAIN(EvidenceCacheTest)

#include "EvidenceCacheTest.moc"
