#include "../src/models/SystemSettings.h"
#include "../src/services/SystemSettingsService.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

class SystemSettingsServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void loadsDefaultSettingsWhenFileDoesNotExist();
    void persistsSettingsAndClampsInvalidNumericValues();
};

void SystemSettingsServiceTest::loadsDefaultSettingsWhenFileDoesNotExist()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("system.ini"));
    SystemSettingsService service(path);
    const SystemSettings settings = service.load();

    QCOMPARE(settings.ui.fontPointSize, 10);
    QCOMPARE(settings.ui.previewFrameRate, 12);
    QCOMPARE(settings.ui.captureListMaxRows, 1000);
    QVERIFY(settings.ui.overlaySpeed);
    QVERIFY(settings.storage.savePlateCloseup);
    QCOMPARE(settings.storage.indexDigits, 6);
    QCOMPARE(settings.maintenance.dailySyncTime, QTime(3, 0));
}

void SystemSettingsServiceTest::persistsSettingsAndClampsInvalidNumericValues()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("config/system.ini"));
    SystemSettingsService service(path);

    SystemSettings settings = SystemSettings::defaults();
    settings.ui.startMaximized = true;
    settings.ui.autoConnectOnStart = true;
    settings.ui.lastSelectedVideoDeviceId = QStringLiteral("rv1126b-last-video");
    settings.ui.fontFamily = QStringLiteral("Arial");
    settings.ui.fontPointSize = 200;
    settings.ui.previewFrameRate = 0;
    settings.ui.captureListMaxRows = -20;
    settings.storage.rootPath = QDir(tempDir.path()).filePath(QStringLiteral("captures"));
    settings.storage.indexDigits = 1;
    settings.storage.maxVideoSegmentMb = 99999;
    settings.maintenance.minFreeSpaceGb = -5;

    QVERIFY2(service.save(settings), qPrintable(service.lastError()));

    SystemSettingsService reloaded(path);
    const SystemSettings loaded = reloaded.load();

    QVERIFY(loaded.ui.startMaximized);
    QVERIFY(loaded.ui.autoConnectOnStart);
    QCOMPARE(loaded.ui.lastSelectedVideoDeviceId, QStringLiteral("rv1126b-last-video"));
    QCOMPARE(loaded.ui.fontFamily, QStringLiteral("Arial"));
    QCOMPARE(loaded.ui.fontPointSize, 48);
    QCOMPARE(loaded.ui.previewFrameRate, 1);
    QCOMPARE(loaded.ui.captureListMaxRows, 1);
    QCOMPARE(loaded.storage.indexDigits, 2);
    QCOMPARE(loaded.storage.maxVideoSegmentMb, 8192);
    QCOMPARE(loaded.maintenance.minFreeSpaceGb, 1);
}

QTEST_MAIN(SystemSettingsServiceTest)

#include "SystemSettingsServiceTest.moc"
