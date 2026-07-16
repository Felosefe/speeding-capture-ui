#pragma once

#include "IDeviceClient.h"
#include "../models/Device.h"

#include <QTimer>

// Development-only simulated device. It is intentionally kept separate from
// the production RV1126B transport and session interfaces.
class MockDeviceClient final : public IDeviceClient
{
    Q_OBJECT

public:
    explicit MockDeviceClient(const Device& device, QObject* parent = nullptr);

    bool connectDevice() override;
    void disconnectDevice() override;
    DeviceStatus readStatus() const override;
    DeviceConfig readConfig() const override;
    bool writeConfig(const DeviceConfig& config) override;
    bool reboot() override;
    bool syncTime(const QDateTime& time) override;
    bool triggerCapture() override;

private slots:
    void refreshStatus();
    void generateAutomaticCapture();

private:
    CaptureRecord createCaptureRecord() const;
    QString randomPlateNumber() const;

    Device device_;
    DeviceStatus status_;
    DeviceConfig config_;
    QTimer statusTimer_;
    QTimer captureTimer_;
};
