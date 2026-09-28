#include "DevicePropertyModel.h"

namespace {

// 板端"展示配置"尚未读到时的统一提示：告诉用户去哪里看真实值，
// 而不是把本机结构体里的陈旧默认值当成现场配置显示出来。
const QString kSiteInfoNotLoaded = QStringLiteral("未读取（打开「设备配置 › 展示配置」）");

QString displayOrFallback(const QString& value)
{
    const QString trimmed = value.trimmed();
    return trimmed.isEmpty() ? kSiteInfoNotLoaded : trimmed;
}

} // namespace

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

void DevicePropertyModel::setBoardSiteInfo(const QString& siteName, const QString& roadDirection,
                                           int speedLimitKmh)
{
    hasBoardSiteInfo_ = true;
    boardSiteName_ = siteName;
    boardRoadDirection_ = roadDirection;
    boardSpeedLimitKmh_ = speedLimitKmh > 0 ? speedLimitKmh : 0;
    rebuildProperties();
}

void DevicePropertyModel::rebuildProperties()
{
    if (!device_) {
        clear();
        return;
    }

    beginResetModel();
    QString speedLimitText;
    if (boardSpeedLimitKmh_ > 0) {
        speedLimitText = QStringLiteral("%1 km/h").arg(boardSpeedLimitKmh_);
    } else if (hasBoardSiteInfo_) {
        speedLimitText = QStringLiteral("未设置限速");
    } else {
        speedLimitText = kSiteInfoNotLoaded;
    }

    properties_ = {
        {QStringLiteral("设备 ID"), device_->id},
        {QStringLiteral("设备名称"), device_->name},
        {QStringLiteral("设备状态"), connectionStateText(device_->status.connectionState)},
        {QStringLiteral("运行状态"), device_->status.runningState},
        {QStringLiteral("IP 地址"), device_->ipAddress},
        {QStringLiteral("端口"), QString::number(device_->port)},
        {QStringLiteral("API URL"), device_->apiUrl.isEmpty() ? QStringLiteral("-") : device_->apiUrl},
        {QStringLiteral("能力声明"), device_->capabilities.isEmpty() ? QStringLiteral("未声明（不隐藏功能）")
                                                                    : device_->capabilities.join(QStringLiteral(", "))},
        // 以下四项来自板端「展示配置」（GET /api/v1/config/evidence），与配置界面同源。
        {QStringLiteral("点位名称"), displayOrFallback(boardSiteName_)},
        {QStringLiteral("道路方向"), displayOrFallback(boardRoadDirection_)},
        {QStringLiteral("限速值"), speedLimitText},
        {QStringLiteral("固件版本"), device_->status.firmwareVersion},
        {QStringLiteral("最后心跳"), device_->status.lastHeartbeat.isValid()
             ? device_->status.lastHeartbeat.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
             : QStringLiteral("-")},
        {QStringLiteral("最后在线"), device_->lastOnline.isValid()
             ? device_->lastOnline.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
             : QStringLiteral("-")},
        {QStringLiteral("最后错误"), device_->lastError.isEmpty() ? QStringLiteral("-") : device_->lastError},
    };

    // 本地抓拍记录只在模拟模式存在；真机模式下"抓拍次数/最后抓拍"没有数据来源，
    // 以前会一直显示 "0" 和 "-" 冒充真实统计，这里直接不显示这两行。
    if (latestRecord_) {
        properties_.append({QStringLiteral("最后抓拍"),
                            latestRecord_->timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))});
    }
    endResetModel();
}

void DevicePropertyModel::clear()
{
    beginResetModel();
    properties_ = {
        {QStringLiteral("设备状态"), QStringLiteral("未选择")},
        {QStringLiteral("IP 地址"), QStringLiteral("-")},
        {QStringLiteral("端口"), QStringLiteral("-")},
        {QStringLiteral("点位名称"), QStringLiteral("-")},
        {QStringLiteral("道路方向"), QStringLiteral("-")},
        {QStringLiteral("限速值"), QStringLiteral("-")},
    };
    endResetModel();
}

void DevicePropertyModel::setDevice(const Device* device, const CaptureRecord* latestRecord)
{
    device_ = device;
    latestRecord_ = latestRecord;
    if (!device) {
        clear();
        return;
    }

    rebuildProperties();
}
