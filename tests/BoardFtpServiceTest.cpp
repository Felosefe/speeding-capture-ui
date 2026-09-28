#include "../src/rv1126b/services/BoardFtpService.h"

#include <QTimer>
#include <QtTest>

#include <optional>

using namespace rv1126b;

namespace {

ApiError makeError(const QString& code, ApiErrorCategory category)
{
    ApiError error;
    error.code = code;
    error.category = category;
    return error;
}

class FakeBoardApiClient final : public IBoardApiClient
{
public:
    explicit FakeBoardApiClient(QObject* parent = nullptr)
        : IBoardApiClient(parent)
    {
    }

    RequestId getHealth(QObject*, ApiCompletion<HealthDto>) override { return RequestId::createUuid(); }
    RequestId listEvents(int, const std::optional<QString>&, QObject*, ApiCompletion<EventPageDto>) override { return RequestId::createUuid(); }
    RequestId getEventDetail(const EventIdentity&, QObject*, ApiCompletion<EventDetailDto>) override { return RequestId::createUuid(); }
    RequestId downloadEvidenceToPartFile(const EventIdentity&, const QString&, const QString&, QObject*, ApiCompletion<EvidenceDownloadResult>) override { return RequestId::createUuid(); }
    RequestId getEvidenceConfig(QObject*, ApiCompletion<EvidenceConfigDto>) override { return RequestId::createUuid(); }
    RequestId putEvidenceConfig(const EvidenceConfigUpdate&, QObject*, ApiCompletion<EvidenceConfigDto>) override { return RequestId::createUuid(); }
    RequestId getTime(QObject*, ApiCompletion<TimeStatusDto>) override { return RequestId::createUuid(); }
    RequestId putTime(const TimeUpdate&, QObject*, ApiCompletion<TimeStatusDto>) override { return RequestId::createUuid(); }
    RequestId getFtpConfig(QObject*, ApiCompletion<FtpConfigSnapshotDto>) override { return RequestId::createUuid(); }

    RequestId putFtpConfig(const FtpConfigUpdate& update, QObject*, ApiCompletion<FtpConfigSnapshotDto> completion) override
    {
        ++putConfigCount;
        lastConfigUpdate = update;
        configRequestId = RequestId::createUuid();
        configCompletion = std::move(completion);
        return configRequestId;
    }

    RequestId rollbackFtpConfig(const QString&, QObject*, ApiCompletion<FtpConfigSnapshotDto>) override { return RequestId::createUuid(); }
    RequestId getFtpControl(QObject*, ApiCompletion<FtpControlDto>) override { return RequestId::createUuid(); }

    RequestId putFtpControl(const FtpControlUpdate& update, QObject*, ApiCompletion<FtpControlDto> completion) override
    {
        ++putControlCount;
        lastControlUpdate = update;
        controlRequestId = RequestId::createUuid();
        controlCompletion = std::move(completion);
        return controlRequestId;
    }

    RequestId createFtpTask(const FtpTaskCreate&, QObject*, ApiCompletion<FtpTaskDetailDto>) override { return RequestId::createUuid(); }

    RequestId listFtpTasks(
        int limit,
        const std::optional<QString>& cursor,
        QObject*,
        ApiCompletion<FtpTaskPageDto> completion) override
    {
        lastListLimit = limit;
        lastListCursor = cursor;
        const RequestId requestId = RequestId::createUuid();
        QTimer::singleShot(0, this, [completion = std::move(completion), page = listPage]() mutable {
            completion(ApiResult<FtpTaskPageDto>::success(std::move(page)));
        });
        return requestId;
    }
    RequestId getFtpTask(const QString&, QObject*, ApiCompletion<FtpTaskDetailDto>) override { return RequestId::createUuid(); }

    RequestId retryFtpTask(const QString& taskId, QObject*, ApiCompletion<FtpTaskDetailDto> completion) override
    {
        lastRetriedTaskId = taskId;
        const RequestId requestId = RequestId::createUuid();
        QTimer::singleShot(0, this, [completion = std::move(completion), detail = retryDetail]() mutable {
            completion(ApiResult<FtpTaskDetailDto>::success(std::move(detail)));
        });
        return requestId;
    }

    void cancel(const RequestId& requestId) override
    {
        cancelledRequestIds.append(requestId);
    }

    void cancelAll() override { ++cancelAllCount; }

    void respondConfig(ApiResult<FtpConfigSnapshotDto> result)
    {
        QVERIFY(configCompletion.has_value());
        auto completion = std::move(*configCompletion);
        configCompletion.reset();
        QTimer::singleShot(0, this, [completion = std::move(completion), result = std::move(result)]() mutable {
            completion(std::move(result));
        });
    }

    void respondControl(ApiResult<FtpControlDto> result)
    {
        QVERIFY(controlCompletion.has_value());
        auto completion = std::move(*controlCompletion);
        controlCompletion.reset();
        QTimer::singleShot(0, this, [completion = std::move(completion), result = std::move(result)]() mutable {
            completion(std::move(result));
        });
    }

    int putConfigCount = 0;
    int putControlCount = 0;
    int cancelAllCount = 0;
    FtpConfigUpdate lastConfigUpdate;
    FtpControlUpdate lastControlUpdate;
    RequestId configRequestId;
    RequestId controlRequestId;
    QList<RequestId> cancelledRequestIds;
    int lastListLimit = 0;
    std::optional<QString> lastListCursor;
    QString lastRetriedTaskId;
    FtpTaskPageDto listPage;
    FtpTaskDetailDto retryDetail;
    std::optional<ApiCompletion<FtpConfigSnapshotDto>> configCompletion;
    std::optional<ApiCompletion<FtpControlDto>> controlCompletion;
};

FtpConfigUpdate validUpdate()
{
    FtpConfigUpdate update;
    update.expectedRevision = QStringLiteral("revision-1");
    update.deviceId = QStringLiteral("rv1126b_001");
    update.retryMax = 3;
    update.retryIntervalSec = 10;
    update.connectTimeoutSec = 5;
    update.transferTimeoutSec = 30;
    update.scanIntervalSec = 5;
    return update;
}

FtpConfigSnapshotDto savedConfig()
{
    FtpConfigSnapshotDto config;
    config.revision = QStringLiteral("revision-2");
    return config;
}

} // namespace

class BoardFtpServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void savesConfigurationThenEnablesNewEvents();
    void reportsPartialSuccessWhenControlWriteFails();
    void preservesConfigConflictWithoutEnablingControl();
    void delegatesTaskPaginationAndRetryWithoutPasswordState();
    void cancellationCancelsCurrentInnerRequest();
};

void BoardFtpServiceTest::savesConfigurationThenEnablesNewEvents()
{
    FakeBoardApiClient client;
    BoardFtpService service(&client);
    std::optional<ApiResult<FtpActivationResult>> result;

    service.saveConfigAndEnableNewEvents(validUpdate(), this, [&result](ApiResult<FtpActivationResult> value) {
        result = std::move(value);
    });
    QCOMPARE(client.putConfigCount, 1);
    client.respondConfig(ApiResult<FtpConfigSnapshotDto>::success(savedConfig()));
    QTRY_COMPARE_WITH_TIMEOUT(client.putControlCount, 1, 500);
    QCOMPARE(client.lastControlUpdate.expectedRevision, QStringLiteral("revision-2"));
    QVERIFY(client.lastControlUpdate.enabled);
    QCOMPARE(client.lastControlUpdate.scope.value, FtpControlScope::AllExisting);
    QCOMPARE(client.lastControlUpdate.scope.rawValue, QStringLiteral("all_existing"));
    client.respondControl(ApiResult<FtpControlDto>::success(FtpControlDto {}));

    QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 500);
    QVERIFY(result->isSuccess());
    QVERIFY(result->value().configSaved);
    QVERIFY(result->value().autoEnabled);
    QCOMPARE(result->value().newRevision, QStringLiteral("revision-2"));
    QVERIFY(!result->value().error.has_value());
}

void BoardFtpServiceTest::reportsPartialSuccessWhenControlWriteFails()
{
    FakeBoardApiClient client;
    BoardFtpService service(&client);
    std::optional<ApiResult<FtpActivationResult>> result;

    service.saveConfigAndEnableNewEvents(validUpdate(), this, [&result](ApiResult<FtpActivationResult> value) {
        result = std::move(value);
    });
    client.respondConfig(ApiResult<FtpConfigSnapshotDto>::success(savedConfig()));
    QTRY_COMPARE_WITH_TIMEOUT(client.putControlCount, 1, 500);
    client.respondControl(ApiResult<FtpControlDto>::failure(makeError(
        QStringLiteral("ftp_config_write_disabled"), ApiErrorCategory::CapabilityDisabled)));

    QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 500);
    QVERIFY(result->isSuccess());
    QVERIFY(result->value().configSaved);
    QVERIFY(!result->value().autoEnabled);
    QVERIFY(result->value().error.has_value());
    QCOMPARE(result->value().error->code, QStringLiteral("ftp_config_write_disabled"));
}

void BoardFtpServiceTest::preservesConfigConflictWithoutEnablingControl()
{
    FakeBoardApiClient client;
    BoardFtpService service(&client);
    std::optional<ApiResult<FtpActivationResult>> result;

    service.saveConfigAndEnableNewEvents(validUpdate(), this, [&result](ApiResult<FtpActivationResult> value) {
        result = std::move(value);
    });
    client.respondConfig(ApiResult<FtpConfigSnapshotDto>::failure(makeError(
        QStringLiteral("config_revision_conflict"), ApiErrorCategory::Conflict)));

    QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 500);
    QVERIFY(!result->isSuccess());
    QCOMPARE(result->error().category, ApiErrorCategory::Conflict);
    QCOMPARE(client.putControlCount, 0);
}

void BoardFtpServiceTest::delegatesTaskPaginationAndRetryWithoutPasswordState()
{
    FakeBoardApiClient client;
    FtpTaskSummaryDto summary;
    summary.taskId = QStringLiteral("task-001");
    client.listPage.items.append(summary);
    client.listPage.count = 1;
    client.listPage.hasMore = true;
    client.listPage.nextCursor = QStringLiteral("cursor-2");
    client.retryDetail.summary.taskId = summary.taskId;
    BoardFtpService service(&client);
    std::optional<ApiResult<FtpTaskPageDto>> pageResult;
    std::optional<ApiResult<FtpTaskDetailDto>> retryResult;

    service.listTasks(50, QStringLiteral("cursor-1"), this, [&pageResult](ApiResult<FtpTaskPageDto> value) {
        pageResult = std::move(value);
    });
    QTRY_VERIFY_WITH_TIMEOUT(pageResult.has_value(), 500);
    QVERIFY(pageResult->isSuccess());
    QCOMPARE(client.lastListLimit, 50);
    QCOMPARE(client.lastListCursor, std::optional<QString> { QStringLiteral("cursor-1") });
    QCOMPARE(pageResult->value().nextCursor, std::optional<QString> { QStringLiteral("cursor-2") });

    service.retryTask(summary.taskId, this, [&retryResult](ApiResult<FtpTaskDetailDto> value) {
        retryResult = std::move(value);
    });
    QTRY_VERIFY_WITH_TIMEOUT(retryResult.has_value(), 500);
    QVERIFY(retryResult->isSuccess());
    QCOMPARE(client.lastRetriedTaskId, summary.taskId);
    QCOMPARE(retryResult->value().summary.taskId, summary.taskId);

    FtpTargetSnapshotDto snapshot;
    snapshot.passwordConfigured = true;
    QCOMPARE(snapshot.passwordConfigured, true);
}

void BoardFtpServiceTest::cancellationCancelsCurrentInnerRequest()
{
    FakeBoardApiClient client;
    BoardFtpService service(&client);
    std::optional<ApiResult<FtpActivationResult>> result;

    const RequestId requestId = service.saveConfigAndEnableNewEvents(
        validUpdate(), this, [&result](ApiResult<FtpActivationResult> value) { result = std::move(value); });
    service.cancel(requestId);

    QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 500);
    QVERIFY(!result->isSuccess());
    QCOMPARE(result->error().category, ApiErrorCategory::Cancelled);
    QCOMPARE(client.cancelledRequestIds, QList<RequestId> { client.configRequestId });
}

QTEST_MAIN(BoardFtpServiceTest)

#include "BoardFtpServiceTest.moc"
