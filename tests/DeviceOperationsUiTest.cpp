#include "../src/ui/Rv1126bDeviceManagementDialog.h"
#include "../src/rv1126b/services/BoardFtpTaskSnapshotService.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTest>

using namespace rv1126b;

namespace {

template<typename T>
RequestId done(ApiCompletion<T> completion, ApiResult<T> result)
{
    const RequestId id = RequestId::createUuid();
    completion(std::move(result));
    return id;
}

class UiBoard final : public IBoardApiClient
{
public:
    EvidenceConfigDto evidence;
    TimeStatusDto time;

    RequestId getHealth(QObject*, ApiCompletion<HealthDto> c) override { return done(std::move(c), ApiResult<HealthDto>::success({})); }
    RequestId listEvents(int, const std::optional<QString>&, QObject*, ApiCompletion<EventPageDto> c) override { return done(std::move(c), ApiResult<EventPageDto>::success({})); }
    RequestId getEventDetail(const EventIdentity&, QObject*, ApiCompletion<EventDetailDto> c) override { return done(std::move(c), ApiResult<EventDetailDto>::success({})); }
    RequestId downloadEvidenceToPartFile(const EventIdentity&, const QString&, const QString&, QObject*, ApiCompletion<EvidenceDownloadResult> c) override { return done(std::move(c), ApiResult<EvidenceDownloadResult>::success({})); }
    RequestId getEvidenceConfig(QObject*, ApiCompletion<EvidenceConfigDto> c) override { return done(std::move(c), ApiResult<EvidenceConfigDto>::success(evidence)); }
    RequestId putEvidenceConfig(const EvidenceConfigUpdate& u, QObject*, ApiCompletion<EvidenceConfigDto> c) override { evidence.evidence = u; return done(std::move(c), ApiResult<EvidenceConfigDto>::success(evidence)); }
    RequestId getTime(QObject*, ApiCompletion<TimeStatusDto> c) override { return done(std::move(c), ApiResult<TimeStatusDto>::success(time)); }
    RequestId putTime(const TimeUpdate& u, QObject*, ApiCompletion<TimeStatusDto> c) override { time.time.epochMs = u.utcEpochMs; return done(std::move(c), ApiResult<TimeStatusDto>::success(time)); }
    RequestId getIspConfig(QObject*, ApiCompletion<QJsonObject> c) override { return done(std::move(c), ApiResult<QJsonObject>::success({})); }
    RequestId saveCurrentIspConfig(QObject*, ApiCompletion<QJsonObject> c) override { return done(std::move(c), ApiResult<QJsonObject>::success({})); }
    RequestId clearIspConfig(QObject*, ApiCompletion<QJsonObject> c) override { return done(std::move(c), ApiResult<QJsonObject>::success({})); }
    RequestId getFtpConfig(QObject*, ApiCompletion<FtpConfigSnapshotDto> c) override { return done(std::move(c), ApiResult<FtpConfigSnapshotDto>::success({})); }
    RequestId putFtpConfig(const FtpConfigUpdate&, QObject*, ApiCompletion<FtpConfigSnapshotDto> c) override { return done(std::move(c), ApiResult<FtpConfigSnapshotDto>::success({})); }
    RequestId rollbackFtpConfig(const QString&, QObject*, ApiCompletion<FtpConfigSnapshotDto> c) override { return done(std::move(c), ApiResult<FtpConfigSnapshotDto>::success({})); }
    RequestId getFtpControl(QObject*, ApiCompletion<FtpControlDto> c) override { return done(std::move(c), ApiResult<FtpControlDto>::success({})); }
    RequestId putFtpControl(const FtpControlUpdate&, QObject*, ApiCompletion<FtpControlDto> c) override { return done(std::move(c), ApiResult<FtpControlDto>::success({})); }
    RequestId createFtpTask(const FtpTaskCreate&, QObject*, ApiCompletion<FtpTaskDetailDto> c) override { return done(std::move(c), ApiResult<FtpTaskDetailDto>::success({})); }
    RequestId listFtpTasks(int, const std::optional<QString>&, QObject*, ApiCompletion<FtpTaskPageDto> c) override { return done(std::move(c), ApiResult<FtpTaskPageDto>::success({})); }
    RequestId getFtpTask(const QString&, QObject*, ApiCompletion<FtpTaskDetailDto> c) override { return done(std::move(c), ApiResult<FtpTaskDetailDto>::success({})); }
    RequestId retryFtpTask(const QString&, QObject*, ApiCompletion<FtpTaskDetailDto> c) override { return done(std::move(c), ApiResult<FtpTaskDetailDto>::success({})); }
    void cancel(const RequestId&) override {}
    void cancelAll() override {}
};

/*
 * The FTP service still answers with real payloads: DeviceOperationsController::loadAll()
 * drives it, so the dialog receives ftpConfigLoaded / ftpControlLoaded / ftpTasksLoaded
 * while it is alive.  Those callbacks used to write into the FTP-config / FTP-tasks
 * widgets; if any of them is ever bound again to a widget that no longer exists the
 * dialog below crashes, which is exactly what these tests guard against.
 */
class UiFtp final : public FtpService
{
public:
    FtpConfigSnapshotDto config;
    FtpControlDto control;
    FtpTaskPageDto page;
    FtpTaskDetailDto detail;
    int saveCount = 0;
    int taskCreateCount = 0;

    RequestId loadConfig(QObject*, ApiCompletion<FtpConfigSnapshotDto> c) override { return done(std::move(c), ApiResult<FtpConfigSnapshotDto>::success(config)); }
    RequestId saveConfigAndEnableNewEvents(const FtpConfigUpdate&, QObject*, ApiCompletion<FtpActivationResult> c) override { ++saveCount; return done(std::move(c), ApiResult<FtpActivationResult>::success({})); }
    RequestId rollbackConfig(const QString&, QObject*, ApiCompletion<FtpConfigSnapshotDto> c) override { return done(std::move(c), ApiResult<FtpConfigSnapshotDto>::success(config)); }
    RequestId loadControl(QObject*, ApiCompletion<FtpControlDto> c) override { return done(std::move(c), ApiResult<FtpControlDto>::success(control)); }
    RequestId updateControl(const FtpControlUpdate& u, QObject*, ApiCompletion<FtpControlDto> c) override { control.enabled = u.enabled; control.scope = u.scope; return done(std::move(c), ApiResult<FtpControlDto>::success(control)); }
    RequestId createTask(const FtpTaskCreate&, QObject*, ApiCompletion<FtpTaskDetailDto> c) override { ++taskCreateCount; return done(std::move(c), ApiResult<FtpTaskDetailDto>::success(detail)); }
    RequestId listTasks(int, const std::optional<QString>&, QObject*, ApiCompletion<FtpTaskPageDto> c) override { return done(std::move(c), ApiResult<FtpTaskPageDto>::success(page)); }
    RequestId loadTask(const QString&, QObject*, ApiCompletion<FtpTaskDetailDto> c) override { return done(std::move(c), ApiResult<FtpTaskDetailDto>::success(detail)); }
    RequestId retryTask(const QString&, QObject*, ApiCompletion<FtpTaskDetailDto> c) override { return done(std::move(c), ApiResult<FtpTaskDetailDto>::success(detail)); }
    void cancel(const RequestId&) override {}
    void cancelAll() override {}
};

/*
 * Minimal repository, only so that BoardFtpTaskSnapshotService can really answer
 * and DeviceOperationsController emits localFtpTaskSnapshotsLoaded — the last
 * callback that used to write into the removed FTP-task widgets.
 */
class UiRepository final : public IEventRepository
{
public:
    RequestId initialize(QObject*, ApiCompletion<void> c) override { return done(std::move(c), ApiResult<void>::success()); }
    RequestId upsertDevice(const DeviceProfile&, QObject*, ApiCompletion<void> c) override { return done(std::move(c), ApiResult<void>::success()); }
    RequestId loadDeviceProfiles(QObject*, ApiCompletion<QVector<DeviceProfile>> c) override { return done(std::move(c), ApiResult<QVector<DeviceProfile>>::success({})); }
    RequestId deleteDeviceProfile(const QString&, QObject*, ApiCompletion<void> c) override { return done(std::move(c), ApiResult<void>::success()); }
    RequestId upsertEvents(const QVector<VehicleEvent>&, QObject*, ApiCompletion<void> c) override { return done(std::move(c), ApiResult<void>::success()); }
    RequestId saveDetail(const EventDetailSnapshot&, QObject*, ApiCompletion<void> c) override { return done(std::move(c), ApiResult<void>::success()); }
    RequestId queryEvents(const EventQuery&, QObject*, ApiCompletion<QVector<VehicleEvent>> c) override { return done(std::move(c), ApiResult<QVector<VehicleEvent>>::success({})); }
    RequestId loadEvent(const EventIdentity&, QObject*, ApiCompletion<std::optional<VehicleEvent>> c) override { return done(std::move(c), ApiResult<std::optional<VehicleEvent>>::success(std::nullopt)); }
    RequestId loadDetail(const EventIdentity&, QObject*, ApiCompletion<std::optional<EventDetailSnapshot>> c) override { return done(std::move(c), ApiResult<std::optional<EventDetailSnapshot>>::success(std::nullopt)); }
    RequestId deleteEvent(const EventIdentity&, QObject*, ApiCompletion<void> c) override { return done(std::move(c), ApiResult<void>::success()); }
    RequestId loadNonTerminalEvents(const QString&, QObject*, ApiCompletion<QVector<VehicleEvent>> c) override { return done(std::move(c), ApiResult<QVector<VehicleEvent>>::success({})); }
    RequestId saveEvidenceState(const EvidenceCacheEntry&, QObject*, ApiCompletion<void> c) override { return done(std::move(c), ApiResult<void>::success()); }
    RequestId loadEvidenceState(const EventIdentity&, const QString&, QObject*, ApiCompletion<std::optional<EvidenceCacheEntry>> c) override { return done(std::move(c), ApiResult<std::optional<EvidenceCacheEntry>>::success(std::nullopt)); }
    RequestId loadSyncAnchor(const QString&, QObject*, ApiCompletion<std::optional<SyncAnchor>> c) override { return done(std::move(c), ApiResult<std::optional<SyncAnchor>>::success(std::nullopt)); }
    RequestId saveSyncAnchor(const SyncAnchor&, QObject*, ApiCompletion<void> c) override { return done(std::move(c), ApiResult<void>::success()); }
    RequestId saveFtpTaskSnapshot(const StoredFtpTask&, QObject*, ApiCompletion<void> c) override { return done(std::move(c), ApiResult<void>::success()); }
    RequestId loadFtpTaskSnapshots(const FtpTaskQuery&, QObject*, ApiCompletion<QVector<StoredFtpTask>> c) override { ++snapshotQueries; return done(std::move(c), ApiResult<QVector<StoredFtpTask>>::success(localSnapshots)); }
    void cancel(const RequestId&) override {}
    void cancelAll() override {}

    QVector<StoredFtpTask> localSnapshots;
    int snapshotQueries = 0;
};

FtpTargetSnapshotDto target(int index)
{
    FtpTargetSnapshotDto value;
    value.id = QStringLiteral("server%1").arg(index + 1);
    value.enabled = true;
    value.host = QStringLiteral("192.0.2.%1").arg(index + 10);
    value.port = 21;
    value.user = QStringLiteral("upload");
    value.remoteDir = QStringLiteral("/events");
    value.passwordConfigured = true;
    return value;
}

void configure(UiBoard& board, UiFtp& ftp, int targetCount = 1)
{
    board.evidence.evidence.siteName = QStringLiteral("测试点位");
    board.evidence.evidence.speedLimitKmh = 60;
    board.time.time.epochMs = 1784000000000LL;
    board.time.time.sourceEpochMs = 1783971200000LL;
    board.time.time.offsetAppliedMs = 28800000;
    board.time.time.quality.value = TimeQuality::BoardEpochUnverified;
    board.time.ntpStatus = QStringLiteral("vendor_time_chain_unverified");
    board.time.timeSetEnabled = false;
    ftp.config.revision = QStringLiteral("rev-1");
    ftp.config.deviceId = QStringLiteral("device-a");
    for (int i = 0; i < targetCount; ++i) ftp.config.targets.append(target(i));
    ftp.control.revision = QStringLiteral("control-1");
    ftp.control.enabled = true;
    ftp.control.scope.value = FtpControlScope::NewEventsOnly;

    FtpTaskSummaryDto summary;
    summary.taskId = QStringLiteral("task-1");
    summary.state.value = FtpTaskState::Failed;
    summary.targetIds = {QStringLiteral("server1"), QStringLiteral("server2")};
    ftp.page.items.append(summary);
    ftp.page.count = 1;
    ftp.detail.summary = summary;
    FtpTaskTargetStatusDto success;
    success.targetId = QStringLiteral("server1");
    success.state.value = FtpTaskState::Done;
    success.total = success.done = 4;
    FtpTaskTargetStatusDto failed;
    failed.targetId = QStringLiteral("server2");
    failed.state.value = FtpTaskState::Failed;
    failed.total = failed.failed = 4;
    failed.lastError = QStringLiteral("connection refused");
    ftp.detail.targets = {success, failed};
}

QTabWidget* tabsOf(Rv1126bDeviceManagementDialog& dialog)
{
    return dialog.findChild<QTabWidget*>(QStringLiteral("deviceOperationsTabs"));
}

} // namespace

class DeviceOperationsUiTest final : public QObject
{
    Q_OBJECT

private slots:
    void showsOnlyTheThreeSurvivingPages();
    void ftpCallbacksAndTabSwitchingDoNotTouchRemovedPages();
    void offlineDialogOpensWithoutRemovedPages();
};

void DeviceOperationsUiTest::showsOnlyTheThreeSurvivingPages()
{
    UiBoard board;
    UiFtp ftp;
    configure(board, ftp, 2);
    DeviceOperationsController controller({[&](const QString&) { return &board; },
                                           [&](const QString&) { return &ftp; }});
    Rv1126bDeviceManagementDialog dialog(QStringLiteral("device-a"), &controller);
    auto* tabs = tabsOf(dialog);
    QVERIFY(tabs);
    QCOMPARE(tabs->count(), 3);
    QCOMPARE(tabs->tabText(0), QStringLiteral("展示配置"));
    QCOMPARE(tabs->tabText(1), QStringLiteral("时间"));
    QCOMPARE(tabs->tabText(2), QStringLiteral("事件同步"));
    QCOMPARE(dialog.findChild<QLineEdit*>(QStringLiteral("siteNameEdit"))->text(), QStringLiteral("测试点位"));
    auto* quality = dialog.findChild<QLabel*>(QStringLiteral("timeQualityLabel"));
    QVERIFY(quality->text().contains(QStringLiteral("不可作为可靠 UTC")));
    QVERIFY(quality->styleSheet().contains(QStringLiteral("b00020")));
    QVERIFY(!dialog.findChild<QPushButton*>(QStringLiteral("syncUtcButton"))->isEnabled());
}

void DeviceOperationsUiTest::ftpCallbacksAndTabSwitchingDoNotTouchRemovedPages()
{
    UiBoard board;
    UiFtp ftp;
    UiRepository repository;
    configure(board, ftp, 8);
    StoredFtpTask localSnapshot;
    localSnapshot.taskId = QStringLiteral("local-task-1");
    repository.localSnapshots = {localSnapshot};
    BoardFtpTaskSnapshotService snapshots(QStringLiteral("device-a"), &repository);
    DeviceOperationsController controller({[&](const QString&) { return &board; },
                                           [&](const QString&) { return &ftp; },
                                           [&](const QString&) { return &snapshots; }});
    Rv1126bDeviceManagementDialog dialog(QStringLiteral("device-a"), &controller);
    dialog.show();
    QCoreApplication::processEvents();

    /*
     * Drive every controller operation whose completion signal used to be bound to a
     * widget of the removed pages (ftpConfigLoaded / ftpControlLoaded / ftpTasksLoaded /
     * ftpTaskLoaded / ftpTaskRetried / ftpActivationFinished / ftpConfigRolledBack /
     * ftpTaskCreated / ftpControlSaved / localFtpTaskSnapshotsLoaded) plus the
     * operationBusyChanged handler that used to poke saveFtpButton_ / createTaskButton_ /
     * retryTaskButton_ / previousTasksButton_ / nextTasksButton_.  These run while the
     * dialog is alive, so re-introducing a stale connect onto a deleted widget crashes
     * right here — the widget-absence checks below cannot catch that.
     */
    controller.loadFtpConfig();
    controller.loadFtpControl();
    controller.listFtpTasks();
    controller.loadFtpTask(QStringLiteral("task-1"));
    controller.retryFtpTask(QStringLiteral("task-1"));
    controller.rollbackFtpConfig();
    controller.updateFtpControl(true, FtpControlScope::AllExisting);
    controller.loadLocalFtpTaskSnapshots();
    FtpConfigUpdate update;
    update.deviceId = QStringLiteral("device-a");
    update.expectedRevision = ftp.config.revision;
    update.retryMax = 3;
    FtpTargetUpdate uploaded;
    uploaded.id = QStringLiteral("server1");
    uploaded.enabled = true;
    uploaded.host = QStringLiteral("192.0.2.10");
    uploaded.port = 21;
    uploaded.user = QStringLiteral("upload");
    uploaded.remoteDir = QStringLiteral("/events");
    uploaded.passwordAction.value = FtpPasswordAction::Keep;
    update.targets = {uploaded};
    controller.saveFtpConfigAndEnableNewEvents(update);
    FtpTaskCreate request;
    request.startEpochMs = 1784000000000LL - 3600000LL;
    request.endEpochMs = 1784000000000LL;
    request.targetIds = {QStringLiteral("server1")};
    controller.createFtpTask(request);
    QCoreApplication::processEvents();
    /* 证明这些调用真的走通了服务层（因而控制器确实 emit 了那些回调），不是空转。 */
    QCOMPARE(ftp.saveCount, 1);
    QCOMPARE(ftp.taskCreateCount, 1);
    QCOMPARE(repository.snapshotQueries, 1);

    auto* tabs = tabsOf(dialog);
    QVERIFY(tabs);
    for (int round = 0; round < 3; ++round) {
        for (int index = 0; index < tabs->count(); ++index) {
            tabs->setCurrentIndex(index);
            QCoreApplication::processEvents();
        }
    }
    QVERIFY(dialog.isVisible());

    /*
     * The ISP / FTP-config / FTP-tasks pages must be gone completely, not merely
     * hidden: no leftover widget may answer to their object names.
     */
    QVERIFY(dialog.findChildren<QTableWidget*>().isEmpty());
    const QStringList goneNames {
        QStringLiteral("ftpRevisionLabel"), QStringLiteral("localFtpReceiverGroup"),
        QStringLiteral("localFtpRootEdit"), QStringLiteral("localFtpHostEdit"),
        QStringLiteral("localFtpTargetIdEdit"), QStringLiteral("localFtpPasswordEdit"),
        QStringLiteral("localFtpStartButton"), QStringLiteral("localFtpSaveTargetButton"),
        QStringLiteral("ftpTargetsTable"), QStringLiteral("addFtpTargetButton"),
        QStringLiteral("saveAndEnableFtpButton"), QStringLiteral("ftpStatusLabel"),
        QStringLiteral("ftpTaskTargets"), QStringLiteral("createFtpTaskButton"),
        QStringLiteral("ftpTaskTable"), QStringLiteral("ftpTaskDetailTable"),
        QStringLiteral("retryFtpTaskButton"),
    };
    for (const QString& name : goneNames) {
        QVERIFY2(dialog.findChild<QWidget*>(name) == nullptr, qPrintable(name));
    }
}

void DeviceOperationsUiTest::offlineDialogOpensWithoutRemovedPages()
{
    UiBoard board;
    UiFtp ftp;
    configure(board, ftp);
    DeviceOperationsController controller({[&](const QString&) { return &board; },
                                           [&](const QString&) { return &ftp; }});
    Rv1126bDeviceManagementDialog dialog(
        QStringLiteral("device-a"), &controller,
        Rv1126bDeviceManagementDialog::InitialPage::Evidence, nullptr, false);
    dialog.show();
    QCoreApplication::processEvents();
    auto* tabs = tabsOf(dialog);
    QVERIFY(tabs);
    QCOMPARE(tabs->count(), 3);
    QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("deviceOperationsMessage"))->text()
                .contains(QStringLiteral("设备离线")));
    QVERIFY(!tabs->isTabEnabled(0));
    QVERIFY(!tabs->isTabEnabled(1));
    /*
     * 页签 2（事件同步）只取决于是否装配了 EventSyncService，不看设备是否在线
     * （见构造函数），所以离线时它仍然是开着的：那一页上还有「目标电脑（UNC）」和
     * 「存储根目录」，离线也能设置。这里不断言它被禁用，只断言对话框本身没问题。
     */
}

QTEST_MAIN(DeviceOperationsUiTest)
#include "DeviceOperationsUiTest.moc"
