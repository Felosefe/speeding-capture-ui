#pragma once

#include "../models/CaptureRecord.h"
#include "../models/DeviceConfig.h"
#include "../models/DeviceStatus.h"

#include <QDateTime>
#include <QObject>
#include <QString>

class IDeviceClient : public QObject
{
    Q_OBJECT

public:
    explicit IDeviceClient(QObject* parent = nullptr);
    ~IDeviceClient() override = default;

    virtual bool connectDevice() = 0;
    virtual void disconnectDevice() = 0;
    virtual DeviceStatus readStatus() const = 0;
    virtual DeviceConfig readConfig() const = 0;
    virtual bool writeConfig(const DeviceConfig& config) = 0;
    virtual bool reboot() = 0;
    virtual bool syncTime(const QDateTime& time) = 0;
    virtual bool triggerCapture() = 0;

signals:
    void statusChanged(const DeviceStatus& status);
    void captureGenerated(const CaptureRecord& record);
    void errorOccurred(const QString& message);
};
