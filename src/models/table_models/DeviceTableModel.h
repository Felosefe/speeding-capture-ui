#pragma once

#include "../Device.h"

#include <QAbstractTableModel>
#include <QVector>

class DeviceTableModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        NameColumn,
        IpColumn,
        PortColumn,
        StatusColumn,
        DirectionColumn,
        SpeedLimitColumn,
        CaptureCountColumn,
        ColumnCount
    };

    explicit DeviceTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void addDevice(const Device& device);
    int upsertDevice(const Device& device);
    void setConnectionState(int row, DeviceConnectionState state);
    void updateStatus(int row, const DeviceStatus& status);
    void updateConfig(int row, const DeviceConfig& config);
    void incrementCaptureCount(int row);
    void seedDemoDevices();

    const Device* deviceAt(int row) const;
    Device* deviceAt(int row);
    int rowForDeviceId(const QString& deviceId) const;
    int deviceCount() const;
    int onlineCount() const;

private:
    QVector<Device> devices_;
};
