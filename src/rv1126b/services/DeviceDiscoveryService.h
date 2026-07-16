#pragma once

#include "../core/Result.h"
#include "../protocol/ApiDtos.h"

#include <QObject>

namespace rv1126b {

class DeviceDiscoveryService : public QObject
{
    Q_OBJECT

public:
    static constexpr quint16 DefaultDiscoveryPort = 18081;
    static constexpr int DefaultScanWindowMs = 1500;

    explicit DeviceDiscoveryService(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    ~DeviceDiscoveryService() override = default;

    virtual RequestId startScan(int scanWindowMs = DefaultScanWindowMs) = 0;
    virtual void cancelScan(const RequestId& scanId) = 0;
    virtual void cancelAll() = 0;

signals:
    void deviceFound(const rv1126b::RequestId& scanId, const rv1126b::DiscoveredDeviceDto& device);
    void scanFinished(const rv1126b::RequestId& scanId);
    void scanFailed(const rv1126b::RequestId& scanId, const rv1126b::ApiError& error);
};

} // namespace rv1126b

