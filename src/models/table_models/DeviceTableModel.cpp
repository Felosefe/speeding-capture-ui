#include "DeviceTableModel.h"

#include <QBrush>
#include <QColor>

DeviceTableModel::DeviceTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int DeviceTableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : devices_.size();
}

int DeviceTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant DeviceTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= devices_.size()) {
        return {};
    }

    const Device& device = devices_.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case NameColumn:
            return device.name;
        case IpColumn:
            return device.ipAddress;
        case PortColumn:
            return device.port;
        case StatusColumn:
            return connectionStateText(device.status.connectionState);
        case DirectionColumn:
            return device.config.direction;
        case SpeedLimitColumn:
            return QStringLiteral("%1 km/h").arg(device.config.speedLimitKmh);
        case CaptureCountColumn:
            return device.status.captureCount;
        default:
            return {};
        }
    }

    if (role == Qt::ForegroundRole && index.column() == StatusColumn) {
        switch (device.status.connectionState) {
        case DeviceConnectionState::Online:
            return QBrush(QColor(25, 128, 64));
        case DeviceConnectionState::Connecting:
        case DeviceConnectionState::Disconnecting:
            return QBrush(QColor(34, 102, 170));
        case DeviceConnectionState::Degraded:
            return QBrush(QColor(190, 125, 20));
        case DeviceConnectionState::AuthenticationFailed:
        case DeviceConnectionState::Fault:
            return QBrush(QColor(180, 72, 40));
        case DeviceConnectionState::Offline:
        default:
            return QBrush(QColor(110, 110, 110));
        }
    }

    return {};
}

QVariant DeviceTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }

    switch (section) {
    case NameColumn:
        return QStringLiteral("名称");
    case IpColumn:
        return QStringLiteral("IP");
    case PortColumn:
        return QStringLiteral("端口");
    case StatusColumn:
        return QStringLiteral("状态");
    case DirectionColumn:
        return QStringLiteral("方向");
    case SpeedLimitColumn:
        return QStringLiteral("限速");
    case CaptureCountColumn:
        return QStringLiteral("抓拍");
    default:
        return {};
    }
}

void DeviceTableModel::addDevice(const Device& device)
{
    const int row = devices_.size();
    beginInsertRows(QModelIndex(), row, row);
    devices_.append(device);
    endInsertRows();
}

int DeviceTableModel::upsertDevice(const Device& device)
{
    const int existingRow = rowForDeviceId(device.id);
    if (existingRow < 0) {
        addDevice(device);
        return devices_.size() - 1;
    }

    devices_[existingRow] = device;
    emit dataChanged(index(existingRow, 0), index(existingRow, ColumnCount - 1));
    return existingRow;
}

void DeviceTableModel::setConnectionState(int row, DeviceConnectionState state)
{
    Device* device = deviceAt(row);
    if (!device) {
        return;
    }

    device->status.connectionState = state;
    device->status.runningState = connectionStateText(state);
    device->status.lastHeartbeat = state == DeviceConnectionState::Online ? QDateTime::currentDateTime() : QDateTime();

    emit dataChanged(index(row, StatusColumn), index(row, CaptureCountColumn));
}

void DeviceTableModel::updateStatus(int row, const DeviceStatus& status)
{
    Device* device = deviceAt(row);
    if (!device) {
        return;
    }

    device->status = status;
    emit dataChanged(index(row, StatusColumn), index(row, CaptureCountColumn));
}

void DeviceTableModel::updateConfig(int row, const DeviceConfig& config)
{
    Device* device = deviceAt(row);
    if (!device) {
        return;
    }

    device->config = config;
    emit dataChanged(index(row, DirectionColumn), index(row, SpeedLimitColumn));
}

void DeviceTableModel::incrementCaptureCount(int row)
{
    Device* device = deviceAt(row);
    if (!device) {
        return;
    }

    ++device->status.captureCount;
    emit dataChanged(index(row, CaptureCountColumn), index(row, CaptureCountColumn));
}

void DeviceTableModel::seedDemoDevices()
{
    if (!devices_.isEmpty()) {
        return;
    }

    for (int i = 0; i < 3; ++i) {
        Device device;
        device.id = QStringLiteral("mock-%1").arg(i + 1);
        device.name = QStringLiteral("模拟设备-%1").arg(i + 1, 2, 10, QLatin1Char('0'));
        device.ipAddress = QStringLiteral("192.168.1.%1").arg(100 + i);
        device.port = 8000;
        device.config.location = QStringLiteral("测试路口%1").arg(i + 1);
        device.config.direction = i % 2 == 0 ? QStringLiteral("由北向南") : QStringLiteral("由南向北");
        device.config.laneName = QStringLiteral("%1车道").arg(i + 1);
        device.config.speedLimitKmh = i == 2 ? 80 : 60;
        addDevice(device);
    }
}

const Device* DeviceTableModel::deviceAt(int row) const
{
    if (row < 0 || row >= devices_.size()) {
        return nullptr;
    }
    return &devices_[row];
}

Device* DeviceTableModel::deviceAt(int row)
{
    if (row < 0 || row >= devices_.size()) {
        return nullptr;
    }
    return &devices_[row];
}

int DeviceTableModel::rowForDeviceId(const QString& deviceId) const
{
    for (int row = 0; row < devices_.size(); ++row) {
        if (devices_.at(row).id == deviceId) {
            return row;
        }
    }
    return -1;
}

int DeviceTableModel::deviceCount() const
{
    return devices_.size();
}

int DeviceTableModel::onlineCount() const
{
    int count = 0;
    for (const Device& device : devices_) {
        if (device.status.connectionState == DeviceConnectionState::Online) {
            ++count;
        }
    }
    return count;
}
