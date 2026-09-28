#pragma once

#include "../domain/Models.h"
#include "../protocol/ApiDtos.h"

#include <QObject>

namespace rv1126b {

class DeviceSession : public QObject
{
    Q_OBJECT

public:
    explicit DeviceSession(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    ~DeviceSession() override = default;

    virtual DeviceProfile profile() const = 0;
    virtual DeviceSessionSnapshot snapshot() const = 0;
    virtual void connectSession() = 0;
    virtual void disconnectSession() = 0;

signals:
    void stateChanged(const rv1126b::DeviceSessionSnapshot& snapshot);
    void healthUpdated(const QString& deviceId, const rv1126b::HealthDto& health);
    void sessionError(const QString& deviceId, const rv1126b::ApiError& error);
};

} // namespace rv1126b

