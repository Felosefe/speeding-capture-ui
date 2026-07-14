#include "../src/models/DeviceStatus.h"
#include "../src/services/DeviceManager.h"

#include <QSignalSpy>
#include <QTest>

class DeviceManagerTest final : public QObject
{
    Q_OBJECT

private slots:
    void connectsAndSyncsAllSeededDevices();
};

void DeviceManagerTest::connectsAndSyncsAllSeededDevices()
{
    DeviceManager manager;
    manager.seedMockDevices(2);

    QSignalSpy statusSpy(&manager, &DeviceManager::deviceStatusChanged);

    QCOMPARE(manager.connectAllDevices(), 2);
    QCOMPARE(statusSpy.size(), 2);
    QCOMPARE(manager.deviceAt(0)->status.connectionState, DeviceConnectionState::Online);
    QCOMPARE(manager.deviceAt(1)->status.connectionState, DeviceConnectionState::Online);

    QCOMPARE(manager.syncAllDeviceTimes(), 2);
}

QTEST_MAIN(DeviceManagerTest)

#include "DeviceManagerTest.moc"
