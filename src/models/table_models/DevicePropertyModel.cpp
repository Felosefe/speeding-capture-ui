#include "DevicePropertyModel.h"

DevicePropertyModel::DevicePropertyModel(QObject* parent)
    : QAbstractTableModel(parent)
{
    clear();
}

int DevicePropertyModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : properties_.size();
}

int DevicePropertyModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : 2;
}

QVariant DevicePropertyModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= properties_.size()) {
        return {};
    }

    if (role != Qt::DisplayRole) {
        return {};
    }

    return index.column() == 0 ? properties_[index.row()].first : properties_[index.row()].second;
}

QVariant DevicePropertyModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }

    return section == 0 ? QStringLiteral("属性") : QStringLiteral("值");
}

void DevicePropertyModel::clear()
{
    beginResetModel();
    properties_ = {
        {QStringLiteral("设备状态"), QStringLiteral("未选择")},
        {QStringLiteral("IP 地址"), QStringLiteral("-")},
        {QStringLiteral("端口"), QStringLiteral("-")},
        {QStringLiteral("地点"), QStringLiteral("-")},
        {QStringLiteral("通道"), QStringLiteral("-")},
        {QStringLiteral("限速值"), QStringLiteral("-")},
        {QStringLiteral("最后抓拍"), QStringLiteral("-")},
    };
    endResetModel();
}

void DevicePropertyModel::setDevice(const Device* device, const CaptureRecord* latestRecord)
{
    if (!device) {
        clear();
        return;
    }

    beginResetModel();
    properties_ = {
        {QStringLiteral("设备 ID"), device->id},
        {QStringLiteral("设备名称"), device->name},
        {QStringLiteral("设备状态"), connectionStateText(device->status.connectionState)},
        {QStringLiteral("运行状态"), device->status.runningState},
        {QStringLiteral("IP 地址"), device->ipAddress},
        {QStringLiteral("端口"), QString::number(device->port)},
        {QStringLiteral("API URL"), device->apiUrl.isEmpty() ? QStringLiteral("-") : device->apiUrl},
        {QStringLiteral("能力声明"), device->capabilities.isEmpty() ? QStringLiteral("未声明（不隐藏功能）")
                                                                    : device->capabilities.join(QStringLiteral(", "))},
        {QStringLiteral("地点"), device->config.location},
        {QStringLiteral("通道"), device->config.laneName},
        {QStringLiteral("方向"), device->config.direction},
        {QStringLiteral("限速值"), QStringLiteral("%1 km/h").arg(device->config.speedLimitKmh)},
        {QStringLiteral("固件版本"), device->status.firmwareVersion},
        {QStringLiteral("抓拍次数"), QString::number(device->status.captureCount)},
        {QStringLiteral("最后心跳"), device->status.lastHeartbeat.isValid()
             ? device->status.lastHeartbeat.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
             : QStringLiteral("-")},
        {QStringLiteral("最后在线"), device->lastOnline.isValid()
             ? device->lastOnline.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
             : QStringLiteral("-")},
        {QStringLiteral("最后错误"), device->lastError.isEmpty() ? QStringLiteral("-") : device->lastError},
        {QStringLiteral("最后抓拍"), latestRecord
             ? latestRecord->timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
             : QStringLiteral("-")},
    };
    endResetModel();
}
