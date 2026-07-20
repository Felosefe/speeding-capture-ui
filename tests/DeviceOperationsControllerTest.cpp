#include "../src/rv1126b/application/DeviceOperationsController.h"

#include <QSignalSpy>
#include <QTest>
#include <QTimeZone>

using namespace rv1126b;

namespace {

template<typename T>
RequestId complete(ApiCompletion<T> completion, ApiResult<T> result)
{
    const RequestId id = RequestId::createUuid();
    completion(std::move(result));
    return id;
}

class FakeBoardApi final : public IBoardApiClient
{
public:
    using IBoardApiClient::IBoardApiClient;

    EvidenceConfigDto evidence;
    TimeStatusDto time;
    bool deferEvidence = false;
    ApiCompletion<EvidenceConfigDto> pendingEvidence;
    int evidencePutCount = 0;
    int timePutCount = 0;
    int cancelAllCount = 0;
    int cancelCount = 0;
    TimeUpdate lastTimeUpdate;

    RequestId getHealth(QObject*, ApiCompletion<HealthDto> c) override
    { return complete(std::move(c), ApiResult<HealthDto>::success({})); }
    RequestId listEvents(int, const std::optional<QString>&, QObject*, ApiCompletion<EventPageDto> c) override
    { return complete(std::move(c), ApiResult<EventPageDto>::success({})); }
    RequestId getEventDetail(const EventIdentity&, QObject*, ApiCompletion<EventDetailDto> c) override
    { return complete(std::move(c), ApiResult<EventDetailDto>::success({})); }
    RequestId downloadEvidenceToPartFile(const EventIdentity&, const QString&, const QString&, QObject*, ApiCompletion<EvidenceDownloadResult> c) override
    { return complete(std::move(c), ApiResult<EvidenceDownloadResult>::success({})); }
    RequestId getEvidenceConfig(QObject*, ApiCompletion<EvidenceConfigDto> c) override
    {
        if (deferEvidence) { pendingEvidence = std::move(c); return RequestId::createUuid(); }
        return complete(std::move(c), ApiResult<EvidenceConfigDto>::success(evidence));
    }
    RequestId putEvidenceConfig(const EvidenceConfigUpdate& update, QObject*, ApiCompletion<EvidenceConfigDto> c) override
    {
        ++evidencePutCount;
        evidence.evidence = update;
        return complete(std::move(c), ApiResult<EvidenceConfigDto>::success(evidence));
    }
    RequestId getTime(QObject*, ApiCompletion<TimeStatusDto> c) override
    { return complete(std::move(c), ApiResult<TimeStatusDto>::success(time)); }
    RequestId putTime(const TimeUpdate& update, QObject*, ApiCompletion<TimeStatusDto> c) override
    {
        ++timePutCount;
        lastTimeUpdate = update;
        time.time.epochMs = update.utcEpochMs;
        return complete(std::move(c), ApiResult<TimeStatusDto>::success(time));
    }
    RequestId getFtpConfig(QObject*, ApiCompletion<FtpConfigSnapshotDto> c) override
    { return complete(std::move(c), ApiResult<FtpConfigSnapshotDto>::success({})); }
    RequestId putFtpConfig(const FtpConfigUpdate&, QObject*, ApiCompletion<FtpConfigSnapshotDto> c) override
    { return complete(std::move(c), ApiResult<FtpConfigSnapshotDto>::success({})); }
    RequestId rollbackFtpConfig(const QString&, QObject*, ApiCompletion<FtpConfigSnapshotDto> c) override
    { return complete(std::move(c), ApiResult<FtpConfigSnapshotDto>::success({})); }
    RequestId getFtpControl(QObject*, ApiCompletion<FtpControlDto> c) override
    { return complete(std::move(c), ApiResult<FtpControlDto>::success({})); }
    RequestId putFtpControl(const FtpControlUpdate&, QObject*, ApiCompletion<FtpControlDto> c) override
    { return complete(std::move(c), ApiResult<FtpControlDto>::success({})); }
    RequestId createFtpTask(const FtpTaskCreate&, QObject*, ApiCompletion<FtpTaskDetailDto> c) override
    { return complete(std::move(c), ApiResult<FtpTaskDetailDto>::success({})); }
    RequestId listFtpTasks(int, const std::optional<QString>&, QObject*, ApiCompletion<FtpTaskPageDto> c) override
    { return complete(std::move(c), ApiResult<FtpTaskPageDto>::success({})); }
    RequestId getFtpTask(const QString&, QObject*, ApiCompletion<FtpTaskDetailDto> c) override
    { return complete(std::move(c), ApiResult<FtpTaskDetailDto>::success({})); }
    RequestId retryFtpTask(const QString&, QObject*, ApiCompletion<FtpTaskDetailDto> c) override
    { return complete(std::move(c), ApiResult<FtpTaskDetailDto>::success({})); }
    void cancel(const RequestId&) override { ++cancelCount; }
    void cancelAll() override { ++cancelAllCount; }
};

class FakeFtpService final : public FtpService
{
public:
    using FtpService::FtpService;

    FtpConfigSnapshotDto config;
    FtpControlDto control;
    FtpActivationResult activation;
    FtpTaskPageDto taskPage;
    FtpTaskDetailDto taskDetail;
    FtpConfigUpdate lastConfigUpdate;
    FtpControlUpdate lastControlUpdate;
    FtpTaskCreate lastTaskCreate;
    QString lastTaskId;
    bool conflictOnSave = false;
    int cancelAllCount = 0;
    int listLimit = 0;
    std::optional<QString> listCursor;

    RequestId loadConfig(QObject*, ApiCompletion<FtpConfigSnapshotDto> c) override
    { return complete(std::move(c), ApiResult<FtpConfigSnapshotDto>::success(config)); }
    RequestId saveConfigAndEnableNewEvents(const FtpConfigUpdate& update, QObject*, ApiCompletion<FtpActivationResult> c) override
    {
        lastConfigUpdate = update;
        if (conflictOnSave) {
            conflictOnSave = false;
            ApiError error;
            error.code = QStringLiteral("config_revision_conflict");
            error.category = ApiErrorCategory::Conflict;
            return complete(std::move(c), ApiResult<FtpActivationResult>::failure(error));
        }
        return complete(std::move(c), ApiResult<FtpActivationResult>::success(activation));
    }
    RequestId rollbackConfig(const QString& revision, QObject*, ApiCompletion<FtpConfigSnapshotDto> c) override
    {
        config.revision = revision + QStringLiteral("-rollback");
        return complete(std::move(c), ApiResult<FtpConfigSnapshotDto>::success(config));
    }
    RequestId loadControl(QObject*, ApiCompletion<FtpControlDto> c) override
    { return complete(std::move(c), ApiResult<FtpControlDto>::success(control)); }
    RequestId updateControl(const FtpControlUpdate& update, QObject*, ApiCompletion<FtpControlDto> c) override
    {
        lastControlUpdate = update;
        control.enabled = update.enabled;
        control.scope = update.scope;
        return complete(std::move(c), ApiResult<FtpControlDto>::success(control));
    }
    RequestId createTask(const FtpTaskCreate& request, QObject*, ApiCompletion<FtpTaskDetailDto> c) override
    {
        lastTaskCreate = request;
        return complete(std::move(c), ApiResult<FtpTaskDetailDto>::success(taskDetail));
    }
    RequestId listTasks(int limit, const std::optional<QString>& cursor, QObject*, ApiCompletion<FtpTaskPageDto> c) override
    {
        listLimit = limit;
        listCursor = cursor;
        return complete(std::move(c), ApiResult<FtpTaskPageDto>::success(taskPage));
    }
    RequestId loadTask(const QString& id, QObject*, ApiCompletion<FtpTaskDetailDto> c) override
    {
        lastTaskId = id;
        return complete(std::move(c), ApiResult<FtpTaskDetailDto>::success(taskDetail));
    }
    RequestId retryTask(const QString& id, QObject*, ApiCompletion<FtpTaskDetailDto> c) override
    {
        lastTaskId = id;
        return complete(std::move(c), ApiResult<FtpTaskDetailDto>::success(taskDetail));
    }
    void cancel(const RequestId&) override {}
    void cancelAll() override { ++cancelAllCount; }
};

FtpConfigSnapshotDto validSnapshot()
{
    FtpConfigSnapshotDto config;
    config.revision = QStringLiteral("rev-1");
    config.deviceId = QStringLiteral("device-a");
    FtpTargetSnapshotDto target;
    target.id = QStringLiteral("server1");
    target.enabled = true;
    target.host = QStringLiteral("192.0.2.20");
    target.user = QStringLiteral("upload");
    target.remoteDir = QStringLiteral("/events");
    config.targets.append(target);
    return config;
}

FtpConfigUpdate validUpdate()
{
    FtpConfigUpdate update;
    update.expectedRevision = QStringLiteral("rev-1");
    update.deviceId = QStringLiteral("device-a");
    FtpTargetUpdate target;
    target.id = QStringLiteral("server1");
    target.enabled = true;
    target.host = QStringLiteral("192.0.2.20");
    target.user = QStringLiteral("upload");
    target.remoteDir = QStringLiteral("/events");
    target.passwordAction.value = FtpPasswordAction::Keep;
    update.targets.append(target);
    return update;
}

} // namespace

class DeviceOperationsControllerTest final : public QObject
{
    Q_OBJECT

private slots:
    void switchingDeviceCancelsAndIgnoresLateCompletion();
    void validatesAndSavesEvidenceAndTime();
    void reportsPartialFtpActivation();
    void reloadsRevisionConflictWithoutRetainingPassword();
    void controlsAndRollsBackFtp();
    void validatesCreatesPagesAndRetriesTasks();
};

void DeviceOperationsControllerTest::switchingDeviceCancelsAndIgnoresLateCompletion()
{
    FakeBoardApi first;
    FakeBoardApi second;
    first.deferEvidence = true;
    DeviceOperationsController controller({
        [&](const QString& id) { return id == QStringLiteral("a") ? &first : &second; }, {}});
    int loaded = 0;
    connect(&controller, &DeviceOperationsController::evidenceConfigLoaded,
            this, [&](const EvidenceConfigDto&) { ++loaded; });
    controller.selectDevice(QStringLiteral("a"));
    controller.loadEvidenceConfig();
    QVERIFY(first.pendingEvidence);
    controller.selectDevice(QStringLiteral("b"));
    QCOMPARE(first.cancelCount, 1);
    QCOMPARE(first.cancelAllCount, 0);
    first.pendingEvidence(ApiResult<EvidenceConfigDto>::success(first.evidence));
    QCOMPARE(loaded, 0);
}

void DeviceOperationsControllerTest::validatesAndSavesEvidenceAndTime()
{
    FakeBoardApi board;
    board.evidence.restartRequired = true;
    DeviceOperationsController controller({[&](const QString&) { return &board; }, {}});
    controller.selectDevice(QStringLiteral("device-a"));
    QSignalSpy errors(&controller, &DeviceOperationsController::userError);
    EvidenceConfigUpdate invalid;
    invalid.siteName = QString(129, QLatin1Char('a'));
    controller.saveEvidenceConfig(invalid);
    QCOMPARE(board.evidencePutCount, 0);
    QCOMPARE(errors.takeFirst().at(0).toString(), QStringLiteral("invalid_site_name"));

    EvidenceConfigUpdate valid;
    valid.siteName = QStringLiteral("测试点位");
    valid.speedLimitKmh = 60;
    int saved = 0;
    connect(&controller, &DeviceOperationsController::evidenceConfigSaved,
            this, [&](const EvidenceConfigDto& dto) { saved += dto.restartRequired ? 1 : 0; });
    controller.saveEvidenceConfig(valid);
    QCOMPARE(board.evidencePutCount, 1);
    QCOMPARE(saved, 1);

    controller.setTimeUtc(1000);
    QCOMPARE(board.timePutCount, 0);
    QCOMPARE(errors.takeLast().at(0).toString(), QStringLiteral("invalid_utc_epoch_ms"));
    controller.setTimeUtc(1784000000000LL);
    QCOMPARE(board.timePutCount, 1);
    QCOMPARE(board.lastTimeUpdate.utcEpochMs, 1784000000000LL);
}

void DeviceOperationsControllerTest::reportsPartialFtpActivation()
{
    FakeFtpService ftp;
    ftp.config = validSnapshot();
    ftp.activation.configSaved = true;
    ftp.activation.autoEnabled = false;
    ftp.activation.newRevision = QStringLiteral("rev-2");
    DeviceOperationsController controller({{}, [&](const QString&) { return &ftp; }});
    controller.selectDevice(QStringLiteral("device-a"));
    FtpActivationResult observed;
    connect(&controller, &DeviceOperationsController::ftpActivationFinished,
            this, [&](const FtpActivationResult& result) { observed = result; });
    controller.saveFtpConfigAndEnableNewEvents(validUpdate());
    QVERIFY(observed.configSaved);
    QVERIFY(!observed.autoEnabled);
    QCOMPARE(observed.newRevision, QStringLiteral("rev-2"));
}

void DeviceOperationsControllerTest::reloadsRevisionConflictWithoutRetainingPassword()
{
    FakeFtpService ftp;
    ftp.config = validSnapshot();
    ftp.config.revision = QStringLiteral("rev-remote");
    ftp.conflictOnSave = true;
    DeviceOperationsController controller({{}, [&](const QString&) { return &ftp; }});
    controller.selectDevice(QStringLiteral("device-a"));
    FtpConfigUpdate update = validUpdate();
    update.targets[0].passwordAction.value = FtpPasswordAction::Replace;
    update.targets[0].replacementPassword = QStringLiteral("top-secret");
    bool conflict = false;
    connect(&controller, &DeviceOperationsController::ftpRevisionConflict, this,
        [&](const FtpConfigSnapshotDto& remote, const FtpConfigUpdate& local, const QStringList& passwords) {
            conflict = true;
            QCOMPARE(remote.revision, QStringLiteral("rev-remote"));
            QCOMPARE(local.expectedRevision, QStringLiteral("rev-remote"));
            QVERIFY(!local.targets[0].replacementPassword.has_value());
            QCOMPARE(passwords, QStringList {QStringLiteral("server1")});
        });
    controller.saveFtpConfigAndEnableNewEvents(update);
    QVERIFY(conflict);
    QCOMPARE(ftp.lastConfigUpdate.targets[0].replacementPassword.value(), QStringLiteral("top-secret"));
}

void DeviceOperationsControllerTest::controlsAndRollsBackFtp()
{
    FakeFtpService ftp;
    ftp.config = validSnapshot();
    ftp.control.revision = QStringLiteral("control-1");
    DeviceOperationsController controller({{}, [&](const QString&) { return &ftp; }});
    controller.selectDevice(QStringLiteral("device-a"));
    controller.loadFtpConfig();
    controller.loadFtpControl();
    controller.updateFtpControl(false, FtpControlScope::AllExisting);
    QVERIFY(!ftp.lastControlUpdate.enabled);
    QCOMPARE(ftp.lastControlUpdate.scope.value, FtpControlScope::Preserve);
    int rolledBack = 0;
    connect(&controller, &DeviceOperationsController::ftpConfigRolledBack,
            this, [&](const FtpConfigSnapshotDto&) { ++rolledBack; });
    controller.rollbackFtpConfig();
    QCOMPARE(rolledBack, 1);
}

void DeviceOperationsControllerTest::validatesCreatesPagesAndRetriesTasks()
{
    FakeFtpService ftp;
    ftp.config = validSnapshot();
    ftp.taskDetail.summary.taskId = QStringLiteral("task-1");
    ftp.taskDetail.summary.state.value = FtpTaskState::Failed;
    DeviceOperationsController controller({{}, [&](const QString&) { return &ftp; }});
    controller.selectDevice(QStringLiteral("device-a"));
    controller.loadFtpConfig();
    QSignalSpy errors(&controller, &DeviceOperationsController::userError);

    FtpTaskCreate request;
    request.startEpochMs = QDateTime(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC).toMSecsSinceEpoch();
    request.endEpochMs = QDateTime(QDate(2027, 1, 3), QTime(0, 0), QTimeZone::UTC).toMSecsSinceEpoch();
    request.targetIds = {QStringLiteral("server1")};
    controller.createFtpTask(request);
    QCOMPARE(errors.takeFirst().at(0).toString(), QStringLiteral("ftp_task_range_too_large"));

    request.endEpochMs = request.startEpochMs + 24LL * 60 * 60 * 1000;
    int created = 0;
    connect(&controller, &DeviceOperationsController::ftpTaskCreated,
            this, [&](const FtpTaskDetailDto&) { ++created; });
    controller.createFtpTask(request);
    QCOMPARE(created, 1);
    QCOMPARE(ftp.lastTaskCreate.targetIds, QStringList {QStringLiteral("server1")});

    controller.listFtpTasks(QStringLiteral("opaque"));
    QCOMPARE(ftp.listLimit, DeviceOperationsController::FtpTaskPageSize);
    QCOMPARE(ftp.listCursor.value(), QStringLiteral("opaque"));
    controller.loadFtpTask(QStringLiteral("task-1"));
    QCOMPARE(ftp.lastTaskId, QStringLiteral("task-1"));
    controller.retryFtpTask(QStringLiteral("task-1"));
    QCOMPARE(ftp.lastTaskId, QStringLiteral("task-1"));
}

QTEST_GUILESS_MAIN(DeviceOperationsControllerTest)
#include "DeviceOperationsControllerTest.moc"
