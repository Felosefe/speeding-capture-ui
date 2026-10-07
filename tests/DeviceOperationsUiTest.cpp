#include "../src/ui/Rv1126bDeviceManagementDialog.h"

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

    RequestId loadConfig(QObject*, ApiCompletion<FtpConfigSnapshotDto> c) override { return done(std::move(c), ApiResult<FtpConfigSnapshotDto>::success(config)); }
    RequestId saveConfigAndEnableNewEvents(const FtpConfigUpdate&, QObject*, ApiCompletion<FtpActivationResult> c) override { return done(std::move(c), ApiResult<FtpActivationResult>::success({})); }
    RequestId rollbackConfig(const QString&, QObject*, ApiCompletion<FtpConfigSnapshotDto> c) override { return done(std::move(c), ApiResult<FtpConfigSnapshotDto>::success(config)); }
    RequestId loadControl(QObject*, ApiCompletion<FtpControlDto> c) override { return done(std::move(c), ApiResult<FtpControlDto>::success(control)); }
    RequestId updateControl(const FtpControlUpdate& u, QObject*, ApiCompletion<FtpControlDto> c) override { control.enabled = u.enabled; control.scope = u.scope; return done(std::move(c), ApiResult<FtpControlDto>::success(control)); }
    RequestId createTask(const FtpTaskCreate&, QObject*, ApiCompletion<FtpTaskDetailDto> c) override { return done(std::move(c), ApiResult<FtpTaskDetailDto>::success(detail)); }
    RequestId listTasks(int, const std::optional<QString>&, QObject*, ApiCompletion<FtpTaskPageDto> c) override { return done(std::move(c), ApiResult<FtpTaskPageDto>::success(page)); }
    RequestId loadTask(const QString&, QObject*, ApiCompletion<FtpTaskDetailDto> c) override { return done(std::move(c), ApiResult<FtpTaskDetailDto>::success(detail)); }
    RequestId retryTask(const QString&, QObject*, ApiCompletion<FtpTaskDetailDto> c) override { return done(std::move(c), ApiResult<FtpTaskDetailDto>::success(detail)); }
    void cancel(const RequestId&) override {}
    void cancelAll() override {}
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
    configure(board, ftp, 8);
    DeviceOperationsController controller({[&](const QString&) { return &board; },
                                           [&](const QString&) { return &ftp; }});
    Rv1126bDeviceManagementDialog dialog(QStringLiteral("device-a"), &controller);
    dialog.show();
    QCoreApplication::processEvents();

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
    QVERIFY(!tabs->isTabEnabled(2));
}

QTEST_MAIN(DeviceOperationsUiTest)
#include "DeviceOperationsUiTest.moc"
