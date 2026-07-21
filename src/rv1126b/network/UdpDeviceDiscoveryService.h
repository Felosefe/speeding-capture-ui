#pragma once

#include "../protocol/ApiCodec.h"
#include "../services/DeviceDiscoveryService.h"

#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QPointer>
#include <QSet>

#include <functional>
#include <utility>

class QTimer;
class QUdpSocket;

namespace rv1126b {

class UdpDeviceDiscoveryService final : public DeviceDiscoveryService
{
    Q_OBJECT

public:
    using BroadcastAddressProvider = std::function<QList<QHostAddress>()>;

    explicit UdpDeviceDiscoveryService(
        const IApiCodec* codec,
        BroadcastAddressProvider broadcastAddressProvider = {},
        quint16 discoveryPort = DefaultDiscoveryPort,
        QObject* parent = nullptr);

    RequestId startScan(int scanWindowMs = DefaultScanWindowMs) override;
    void cancelScan(const RequestId& scanId) override;
    void cancelAll() override;

private:
    struct PendingScan {
        QPointer<QUdpSocket> socket;
        QPointer<QTimer> timer;
        QString nonce;
        QSet<QString> discoveredDeviceIds;
        QHash<QString, qint64> lastResponseByAddressMs;
    };

    QList<QHostAddress> broadcastAddresses() const;
    QByteArray discoveryRequest(const QString& nonce) const;
    bool isTrustedResponse(const DiscoveredDeviceDto& device, const QHostAddress& sender) const;
    void handleReadyRead(const RequestId& scanId);
    void finishScan(const RequestId& scanId);
    void disposeScan(PendingScan pendingScan);
    ApiError error(const QString& code, const QString& message, ApiErrorCategory category) const;

    const IApiCodec* codec_ = nullptr;
    BroadcastAddressProvider broadcastAddressProvider_;
    quint16 discoveryPort_ = DefaultDiscoveryPort;
    QHash<RequestId, PendingScan> pendingScans_;
};

} // namespace rv1126b
