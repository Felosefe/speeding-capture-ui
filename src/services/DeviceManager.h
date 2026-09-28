#pragma once

#include "../device/IDeviceClient.h"
#include "../models/Device.h"

#include <QObject>
#include <QVector>

class DeviceManager final : public QObject
{
    Q_OBJECT

public:
    explicit DeviceManager(QObject* parent = nullptr);

    void addMockDevice();
    void seedMockDevices(int count);
    bool connectDevice(int row);
    int connectAllDevices();
    void disconnectDevice(int row);
    int disconnectAllDevices();
    bool triggerCapture(int row);
    bool rebootDevice(int row);
    bool syncDeviceTime(int row);
    int syncAllDeviceTimes();
    bool writeDeviceConfig(int row, const DeviceConfig& config);

    int deviceCount() const;
    const Device* deviceAt(int row) const;

signals:
    void deviceAdded(const Device& device);
    void deviceStatusChanged(int row, const DeviceStatus& status);
    void deviceConfigChanged(int row, const DeviceConfig& config);
    void captureGenerated(int row, const CaptureRecord& record);
    void errorOccurred(int row, const QString& message);

private:
    struct ManagedDevice {
        Device device;
        IDeviceClient* client = nullptr;
    };

    ManagedDevice* managedAt(int row);
    const ManagedDevice* managedAt(int row) const;
    Device createMockDevice(int number) const;
    void loadStoredConfig(Device& device) const;
    void saveStoredConfig(const Device& device) const;
    QString settingsFilePath() const;

    QVector<ManagedDevice> devices_;
};
