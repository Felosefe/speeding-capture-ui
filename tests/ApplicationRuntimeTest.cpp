#include "../src/rv1126b/application/Rv1126bApplicationRuntime.h"
#include "../src/rv1126b/services/BoardDeviceFleetService.h"
#include "../src/rv1126b/storage/SqliteEventRepository.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

using namespace rv1126b;

class ApplicationRuntimeTest final : public QObject
{
    Q_OBJECT

private slots:
    void assemblesProductionServicesAndEnforcesSessionLimit();
};

void ApplicationRuntimeTest::assemblesProductionServicesAndEnforcesSessionLimit()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    SystemSettings settings;
    settings.storage.rootPath = QDir(temporary.path()).filePath(QStringLiteral("evidence-root"));
    Rv1126bApplicationRuntime runtime(
        settings, QDir(temporary.path()).filePath(QStringLiteral("app-data")));

    bool completed = false;
    bool succeeded = false;
    runtime.initialize(&runtime, [&](ApiResult<void> result) {
        completed = true;
        succeeded = result.isSuccess();
    });
    QTRY_VERIFY(completed);
    QVERIFY(succeeded);

    MainWindowDependencies dependencies = runtime.mainWindowDependencies();
    QVERIFY(dependencies.discovery);
    QVERIFY(dependencies.fleet);
    QVERIFY(dependencies.secretStore);
    QVERIFY(dependencies.player);
    QVERIFY(dependencies.directProbe);
    QVERIFY(dependencies.eventRepository);
    QVERIFY(dependencies.evidenceCache);
    QVERIFY(dependencies.evidenceMaintenance);
    QVERIFY(dependencies.boardApiForDevice);
    QVERIFY(dependencies.ftpServiceForDevice);
    QVERIFY(dependencies.ftpTaskSnapshotForDevice);
    QVERIFY(dependencies.eventSyncForDevice);
    QVERIFY(dependencies.forgetDevice);
    QVERIFY(dependencies.switchEvidenceRoot);

    auto* fleet = runtime.fleet();
    QVERIFY(fleet);
    for (int index = 0; index < 9; ++index) {
        DeviceProfile profile;
        profile.deviceId = QStringLiteral("runtime-device-%1").arg(index);
        profile.deviceModel = QStringLiteral("RV1126B");
        profile.endpoint.ipv4 = QStringLiteral("127.0.0.1");
        profile.endpoint.httpPort = static_cast<quint16>(21000 + index);
        profile.endpoint.apiBaseUrl = QUrl(QStringLiteral("http://127.0.0.1:%1/api/v1")
                                               .arg(profile.endpoint.httpPort));
        QVERIFY(fleet->upsertDevice(profile));
        QVERIFY(dependencies.boardApiForDevice(profile.deviceId));
        QVERIFY(dependencies.ftpServiceForDevice(profile.deviceId));
        QVERIFY(dependencies.ftpTaskSnapshotForDevice(profile.deviceId));
        QVERIFY(dependencies.eventSyncForDevice(profile.deviceId));
    }

    for (int index = 0; index < DeviceFleetService::MaxConcurrentDataSessions; ++index)
        QVERIFY(fleet->connectDevice(QStringLiteral("runtime-device-%1").arg(index)));
    QVERIFY(!fleet->connectDevice(QStringLiteral("runtime-device-8")));

    VehicleEvent retainedEvent;
    retainedEvent.identity.deviceId = QStringLiteral("runtime-device-8");
    retainedEvent.identity.eventId = 88;
    retainedEvent.identity.trackId = 880;
    retainedEvent.eventTime.epochMs = 1784002847389;
    bool eventSaved = false;
    runtime.repository()->upsertEvents({retainedEvent}, &runtime,
        [&](ApiResult<void> result) { eventSaved = result.isSuccess(); });
    QTRY_VERIFY(eventSaved);

    bool forgotten = false;
    fleet->forgetDevice(QStringLiteral("runtime-device-8"), &runtime,
        [&](ApiResult<void> result) { forgotten = result.isSuccess(); });
    QTRY_VERIFY(forgotten);
    QVERIFY(!dependencies.boardApiForDevice(QStringLiteral("runtime-device-8")));
    bool retainedLoaded = false;
    runtime.repository()->loadEvent(retainedEvent.identity, &runtime,
        [&](ApiResult<std::optional<VehicleEvent>> result) {
            retainedLoaded = result.isSuccess() && result.value().has_value();
        });
    QTRY_VERIFY(retainedLoaded);

    const QString switchedRoot = QDir(temporary.path()).filePath(QStringLiteral("new-evidence-root"));
    QVERIFY(dependencies.switchEvidenceRoot(switchedRoot));
    QCOMPARE(runtime.evidenceRootPath(), switchedRoot);

    runtime.shutdown();
    QVERIFY(fleet->sessions().isEmpty());
    QVERIFY(!dependencies.boardApiForDevice(QStringLiteral("runtime-device-0")));
}

QTEST_MAIN(ApplicationRuntimeTest)
#include "ApplicationRuntimeTest.moc"
