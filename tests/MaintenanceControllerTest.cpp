#include "../src/models/SystemSettings.h"
#include "../src/services/MaintenanceController.h"

#include <QTest>

class MaintenanceControllerTest final : public QObject
{
    Q_OBJECT

private slots:
    void syncsDeviceTimeOnlyOncePerConfiguredMinute();
    void doesNotRunShutdownCommandWhenDisabled();
};

void MaintenanceControllerTest::syncsDeviceTimeOnlyOncePerConfiguredMinute()
{
    SystemSettings settings = SystemSettings::defaults();
    settings.maintenance.enableDailyDeviceTimeSync = true;
    settings.maintenance.dailySyncTime = QTime(2, 30);

    int syncCount = 0;
    MaintenanceController controller;
    controller.setSettings(settings.maintenance);
    controller.setSyncDeviceTimesCallback([&syncCount]() { ++syncCount; });

    controller.evaluate(QDateTime(QDate(2026, 7, 9), QTime(2, 30, 0)));
    controller.evaluate(QDateTime(QDate(2026, 7, 9), QTime(2, 30, 30)));
    controller.evaluate(QDateTime(QDate(2026, 7, 10), QTime(2, 30, 0)));

    QCOMPARE(syncCount, 2);
}

void MaintenanceControllerTest::doesNotRunShutdownCommandWhenDisabled()
{
    SystemSettings settings = SystemSettings::defaults();
    settings.maintenance.enableScheduledShutdown = false;
    settings.maintenance.shutdownTime = QTime(23, 0);

    int shutdownCount = 0;
    MaintenanceController controller;
    controller.setSettings(settings.maintenance);
    controller.setShutdownCallback([&shutdownCount]() { ++shutdownCount; });

    controller.evaluate(QDateTime(QDate(2026, 7, 9), QTime(23, 0, 0)));

    QCOMPARE(shutdownCount, 0);
}

QTEST_MAIN(MaintenanceControllerTest)

#include "MaintenanceControllerTest.moc"
