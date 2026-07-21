#include "UdpDeviceDiscoveryService.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QNetworkDatagram>
#include <QTimer>
#include <QUdpSocket>

#include <utility>

namespace rv1126b {
namespace {

constexpr int MaxDatagramBytes = 1023;
constexpr int MaxScanWindowMs = 10000;
constexpr int PerSourceMinimumIntervalMs = 500;

} // namespace

UdpDeviceDiscoveryService::UdpDeviceDiscoveryService(
    const IApiCodec* codec,
    BroadcastAddressProvider broadcastAddressProvider,
    quint16 discoveryPort,
    QObject* parent)
    : DeviceDiscoveryService(parent)
    , codec_(codec)
    , broadcastAddressProvider_(std::move(broadcastAddressProvider))
    , discoveryPort_(discoveryPort)
{
}

RequestId UdpDeviceDiscoveryService::startScan(int scanWindowMs)
{
    const RequestId scanId = RequestId::createUuid();
    if (!codec_ || discoveryPort_ == 0) {
        QTimer::singleShot(0, this, [this, scanId] {
            emit scanFailed(scanId, error(
                QStringLiteral("rv1126b.discovery.invalid_dependencies"),
                QStringLiteral("Missing API codec or discovery port."),
                ApiErrorCategory::Validation));
        });
        return scanId;
    }
    if (scanWindowMs < 1 || scanWindowMs > MaxScanWindowMs) {
        QTimer::singleShot(0, this, [this, scanId] {
            emit scanFailed(scanId, error(
                QStringLiteral("rv1126b.discovery.invalid_scan_window"),
                QStringLiteral("Discovery scan window is out of range."),
                ApiErrorCategory::Validation));
        });
        return scanId;
    }

    auto* socket = new QUdpSocket(this);
    if (!socket->bind(QHostAddress::AnyIPv4, 0, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        socket->deleteLater();
        QTimer::singleShot(0, this, [this, scanId] {
            emit scanFailed(scanId, error(
                QStringLiteral("rv1126b.discovery.bind_failed"),
                QStringLiteral("Could not bind the UDP discovery socket."),
                ApiErrorCategory::Network));
        });
        return scanId;
    }

    PendingScan pendingScan;
    pendingScan.socket = socket;
    pendingScan.nonce = RequestId::createUuid().toString(QUuid::WithoutBraces);
    auto* timer = new QTimer(socket);
    timer->setSingleShot(true);
    pendingScan.timer = timer;
    pendingScans_.insert(scanId, std::move(pendingScan));

    connect(socket, &QUdpSocket::readyRead, this, [this, scanId] { handleReadyRead(scanId); });
    connect(timer, &QTimer::timeout, this, [this, scanId] { finishScan(scanId); });

    const QByteArray request = discoveryRequest(pendingScans_.value(scanId).nonce);
    bool sent = false;
    for (const QHostAddress& address : broadcastAddresses()) {
        if (socket->writeDatagram(request, address, discoveryPort_) == request.size()) {
            sent = true;
        }
    }
    if (!sent) {
        PendingScan failedScan = std::move(pendingScans_[scanId]);
        pendingScans_.remove(scanId);
        disposeScan(std::move(failedScan));
        QTimer::singleShot(0, this, [this, scanId] {
            emit scanFailed(scanId, error(
                QStringLiteral("rv1126b.discovery.send_failed"),
                QStringLiteral("No discovery datagram could be sent."),
                ApiErrorCategory::Network));
        });
        return scanId;
    }
    timer->start(scanWindowMs);
    return scanId;
}

void UdpDeviceDiscoveryService::cancelScan(const RequestId& scanId)
{
    const auto it = pendingScans_.find(scanId);
    if (it == pendingScans_.end()) {
        return;
    }
    PendingScan pendingScan = std::move(it.value());
    pendingScans_.erase(it);
    disposeScan(std::move(pendingScan));
}

void UdpDeviceDiscoveryService::cancelAll()
{
    const QList<RequestId> scanIds = pendingScans_.keys();
    for (const RequestId& scanId : scanIds) {
        cancelScan(scanId);
    }
}

QList<QHostAddress> UdpDeviceDiscoveryService::broadcastAddresses() const
{
    if (broadcastAddressProvider_) {
        return broadcastAddressProvider_();
    }

    QSet<QHostAddress> uniqueAddresses;
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& interface : interfaces) {
        const auto flags = interface.flags();
        if (!(flags & QNetworkInterface::IsUp)
            || !(flags & QNetworkInterface::IsRunning)
            || (flags & QNetworkInterface::IsLoopBack)) {
            continue;
        }
        for (const QNetworkAddressEntry& entry : interface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol
                && !entry.broadcast().isNull()
                && entry.broadcast() != QHostAddress::LocalHost) {
                uniqueAddresses.insert(entry.broadcast());
            }
        }
    }
    return uniqueAddresses.values();
}

QByteArray UdpDeviceDiscoveryService::discoveryRequest(const QString& nonce) const
{
    const QJsonObject request {
        { QStringLiteral("magic"), QStringLiteral("RV1126B_DISCOVERY") },
        { QStringLiteral("version"), 1 },
        { QStringLiteral("type"), QStringLiteral("discover") },
        { QStringLiteral("nonce"), nonce },
    };
    return QJsonDocument(request).toJson(QJsonDocument::Compact);
}

bool UdpDeviceDiscoveryService::isTrustedResponse(
    const DiscoveredDeviceDto& device,
    const QHostAddress& sender) const
{
    if (sender.protocol() != QAbstractSocket::IPv4Protocol
        || device.ipv4 != sender.toString()
        || device.apiUrl.scheme() != QStringLiteral("http")
        || device.apiUrl.host().compare(device.ipv4, Qt::CaseInsensitive) != 0
        || device.apiUrl.port(80) < 1
        || !device.apiUrl.path().startsWith(QStringLiteral("/api/v1"))) {
        return false;
    }
    return true;
}

void UdpDeviceDiscoveryService::handleReadyRead(const RequestId& scanId)
{
    const auto scanIt = pendingScans_.find(scanId);
    if (scanIt == pendingScans_.end() || !scanIt->socket) {
        return;
    }

    QUdpSocket* socket = scanIt->socket;
    while (socket->hasPendingDatagrams()) {
        const QNetworkDatagram datagram = socket->receiveDatagram(MaxDatagramBytes + 1);
        if (!datagram.isValid() || datagram.data().size() > MaxDatagramBytes) {
            continue;
        }
        const QString sourceAddress = datagram.senderAddress().toString();
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const auto lastResponse = scanIt->lastResponseByAddressMs.constFind(sourceAddress);
        if (lastResponse != scanIt->lastResponseByAddressMs.constEnd()
            && nowMs - lastResponse.value() < PerSourceMinimumIntervalMs) {
            continue;
        }
        scanIt->lastResponseByAddressMs.insert(sourceAddress, nowMs);

        const auto parsed = codec_->parseDiscoveryResponse(datagram.data(), scanIt->nonce);
        if (!parsed.isSuccess() || !isTrustedResponse(parsed.value(), datagram.senderAddress())) {
            continue;
        }
        if (scanIt->discoveredDeviceIds.contains(parsed.value().deviceId)) {
            continue;
        }
        scanIt->discoveredDeviceIds.insert(parsed.value().deviceId);
        emit deviceFound(scanId, parsed.value());
    }
}

void UdpDeviceDiscoveryService::finishScan(const RequestId& scanId)
{
    const auto it = pendingScans_.find(scanId);
    if (it == pendingScans_.end()) {
        return;
    }
    PendingScan pendingScan = std::move(it.value());
    pendingScans_.erase(it);
    disposeScan(std::move(pendingScan));
    emit scanFinished(scanId);
}

void UdpDeviceDiscoveryService::disposeScan(PendingScan pendingScan)
{
    if (pendingScan.timer) {
        pendingScan.timer->stop();
    }
    if (pendingScan.socket) {
        pendingScan.socket->close();
        pendingScan.socket->deleteLater();
    }
}

ApiError UdpDeviceDiscoveryService::error(
    const QString& code,
    const QString& message,
    ApiErrorCategory category) const
{
    ApiError result;
    result.code = code;
    result.message = message;
    result.category = category;
    return result;
}

} // namespace rv1126b
