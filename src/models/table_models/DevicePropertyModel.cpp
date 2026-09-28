#include "DevicePropertyModel.h"

namespace {

// 板端"展示配置"尚未读到时的统一提示。以前这里挂着一长串
// "未读取（打开「设备配置 › 展示配置」）"，一屏里出现三遍很吵；
// 具体去哪里看由面板标题一次说清（见 MainWindow::createPropertyPanel）。
const QString kSiteInfoNotLoaded = QStringLiteral("未读取");

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
        // 只留现场真正会看的几项。设备 ID / 型号 / 端口 / API URL / 能力声明这些
        // 要么左栏卡片已经有了，要么属于排查时才看的东西，不再往这一小格里堆。
        {QStringLiteral("设备状态"), connectionStateText(device_->status.connectionState)},
        {QStringLiteral("IP 地址"), device_->ipAddress.isEmpty() ? QStringLiteral("未获取") : device_->ipAddress},
        {QStringLiteral("最后心跳"), device_->status.lastHeartbeat.isValid()
             ? device_->status.lastHeartbeat.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
             : QStringLiteral("—")},
        {QStringLiteral("点位名称"), displayOrFallback(boardSiteName_)},
        {QStringLiteral("道路方向"), displayOrFallback(boardRoadDirection_)},
        {QStringLiteral("限速值"), speedLimitText},
    };

    if (latestRecord_) {
        properties_.append({QStringLiteral("最后抓拍"),
                            latestRecord_->timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))});
    }
    // 出错时单独补一行，正常时不占位置。
    if (!device_->lastError.isEmpty()) {
        properties_.append({QStringLiteral("最后错误"), device_->lastError});
    }
    endResetModel();
}

void DevicePropertyModel::clear()
{
    beginResetModel();
    properties_ = {
        {QStringLiteral("设备状态"), QStringLiteral("未选择")},
        {QStringLiteral("IP 地址"), QStringLiteral("—")},
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
