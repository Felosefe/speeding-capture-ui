#pragma once

#include "../domain/Models.h"

#include <QObject>
#include <QString>
#include <QVector>

namespace rv1126b {

class DeviceFleetService : public QObject
{
    Q_OBJECT

public:
    static constexpr int MaxConcurrentDataSessions = 8;

    explicit DeviceFleetService(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    ~DeviceFleetService() override = default;

    virtual QVector<DeviceSessionSnapshot> sessions() const = 0;
    virtual bool upsertDevice(const DeviceProfile& profile) = 0;
    virtual bool connectDevice(const QString& deviceId) = 0;
    virtual void disconnectDevice(const QString& deviceId) = 0;
    virtual void disconnectAll() = 0;
    virtual bool selectVideoDevice(const QString& deviceId) = 0;
    virtual QString selectedVideoDeviceId() const = 0;

signals:
    void deviceAdded(const rv1126b::DeviceProfile& profile);
    void sessionChanged(const rv1126b::DeviceSessionSnapshot& snapshot);
    void selectedVideoDeviceChanged(const QString& deviceId);
    void fleetError(const rv1126b::ApiError& error);
};

} // namespace rv1126b

