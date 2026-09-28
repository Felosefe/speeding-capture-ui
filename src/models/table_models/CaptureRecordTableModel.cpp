#include "CaptureRecordTableModel.h"

#include <QBrush>
#include <QColor>
#include <QDateTime>

namespace {

QString ocrText(const rv1126b::VehicleEvent& event)
{
    if (event.captureStatus == QStringLiteral("failed")) {
        return event.captureError.isEmpty()
            ? QStringLiteral("抓拍失败")
            : QStringLiteral("抓拍失败：%1").arg(event.captureError);
    }
    using rv1126b::OcrStatus;
    switch (event.ocrStatus.value) {
    case OcrStatus::Queued: return QStringLiteral("排队中");
    case OcrStatus::Matched: return QStringLiteral("已识别");
    case OcrStatus::NoPlate: return QStringLiteral("无车牌");
    case OcrStatus::Ambiguous: return QStringLiteral("结果不确定");
    case OcrStatus::NoVehicle: return QStringLiteral("无车辆");
    case OcrStatus::Failed: return QStringLiteral("识别失败");
    case OcrStatus::TimedOut: return QStringLiteral("识别超时");
    case OcrStatus::QueueFull: return QStringLiteral("队列已满");
    case OcrStatus::Unknown:
    default: return event.ocrStatus.rawValue.isEmpty()
        ? QStringLiteral("未知状态")
        : QStringLiteral("未知：%1").arg(event.ocrStatus.rawValue);
    }
}

QString qualityText(const rv1126b::WireEnum<rv1126b::TimeQuality>& quality)
{
    using rv1126b::TimeQuality;
    switch (quality.value) {
    case TimeQuality::NativeUtc: return QStringLiteral("UTC 已验证");
    case TimeQuality::ConfiguredOffset: return QStringLiteral("已应用偏移");
    case TimeQuality::BoardEpochUnverified: return QStringLiteral("板端时间未校验");
    case TimeQuality::AppApiSetCurrentBoot: return QStringLiteral("本次启动已由应用校时");
    case TimeQuality::RtcRestoredCurrentBoot: return QStringLiteral("本次启动 RTC 已恢复");
    case TimeQuality::Unknown:
    default:
        // 板端现场固件（2026-09 那批）在 formal JSON 里把 time_quality 写成空串，
        // 于是所有事件都落进 Unknown。这里把"板端没给"和"出现了本软件不认识的
        // 新取值"分开说清楚，并保留原始值，便于排查而不是只显示一句"未知"。
        if (quality.rawValue.isEmpty()) {
            return QStringLiteral("板端未提供");
        }
        if (quality.rawValue == QStringLiteral("legacy_unknown")) {
            return QStringLiteral("历史事件（板端未记录）");
        }
        return QStringLiteral("未知（%1）").arg(quality.rawValue);
    }
}

// 车牌颜色：板端 OCR 库给的是英文 token（blue/green/yellow...），界面上要中文。
// 认不出来的取值一律原样返回，避免把模拟数据里已经是中文的值吃掉。
QString plateColorText(const QString& raw)
{
    const QString value = raw.trimmed();
    if (value.isEmpty()) return QStringLiteral("未识别");
    const QString lower = value.toLower();
    if (lower == QStringLiteral("blue")) return QStringLiteral("蓝牌");
    if (lower == QStringLiteral("yellow")) return QStringLiteral("黄牌");
    if (lower == QStringLiteral("green")) return QStringLiteral("绿牌（新能源）");
    if (lower == QStringLiteral("yellow_green") || lower == QStringLiteral("yellowgreen")
        || lower == QStringLiteral("green_yellow")) {
        return QStringLiteral("黄绿牌（新能源）");
    }
    if (lower == QStringLiteral("white")) return QStringLiteral("白牌");
    if (lower == QStringLiteral("black")) return QStringLiteral("黑牌");
    if (lower == QStringLiteral("unknown") || lower == QStringLiteral("none")) {
        return QStringLiteral("未识别");
    }
    return value;
}

// 通行朝向：板端只有 down/up/unknown（历史脏值里出现过 approaching）。
QString directionText(const QString& raw)
{
    const QString value = raw.trimmed();
    if (value.isEmpty()) return QStringLiteral("未知");
    const QString lower = value.toLower();
    if (lower == QStringLiteral("down")) return QStringLiteral("下行（驶离）");
    if (lower == QStringLiteral("up")) return QStringLiteral("上行（驶来）");
    if (lower == QStringLiteral("bidirectional")) return QStringLiteral("双向");
    if (lower == QStringLiteral("unknown")) return QStringLiteral("未知");
    if (lower == QStringLiteral("approaching")) return QStringLiteral("接近（历史取值）");
    return value;
}

// 测速状态：matched/no_data/disabled/required_missing 都来自板端 radar_match_status。
QString speedStatusText(const QString& raw)
{
    const QString value = raw.trimmed();
    if (value.isEmpty()) return QStringLiteral("未记录");
    const QString lower = value.toLower();
    if (lower == QStringLiteral("matched")) return QStringLiteral("测速已匹配");
    if (lower == QStringLiteral("required_missing")) return QStringLiteral("缺少必需测速值");
    if (lower == QStringLiteral("no_data")) return QStringLiteral("无雷达数据");
    if (lower == QStringLiteral("disabled")) return QStringLiteral("测速已关闭");
    if (lower == QStringLiteral("synthetic")) return QStringLiteral("模拟数据");
    if (lower == QStringLiteral("unknown")) return QStringLiteral("未知");
    return value;
}

// 板端上报的融合图状态（本地缓存里没有该事件条目时直接显示它）。
QString evidenceStatusText(const QString& raw)
{
    const QString value = raw.trimmed();
    if (value.isEmpty()) return QStringLiteral("未生成");
    const QString lower = value.toLower();
    if (lower == QStringLiteral("generated")) return QStringLiteral("已生成");
    if (lower == QStringLiteral("not_generated")) return QStringLiteral("未生成");
    return value;
}

QString cacheStatusText(rv1126b::EvidenceCacheStatus status)
{
    using rv1126b::EvidenceCacheStatus;
    switch (status) {
    case EvidenceCacheStatus::Queued: return QStringLiteral("等待缓存");
    case EvidenceCacheStatus::Downloading: return QStringLiteral("下载中");
    case EvidenceCacheStatus::RetryWait: return QStringLiteral("处理中/等待重试");
    case EvidenceCacheStatus::Available: return QStringLiteral("已缓存");
    case EvidenceCacheStatus::Missing: return QStringLiteral("未缓存");
    case EvidenceCacheStatus::Failed: return QStringLiteral("缓存失败");
    case EvidenceCacheStatus::NotRequested:
    default: return QStringLiteral("未请求");
    }
}

} // namespace

CaptureRecordTableModel::CaptureRecordTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int CaptureRecordTableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : (realEventMode_ ? events_.size() : records_.size());
}

int CaptureRecordTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant CaptureRecordTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }

    if (realEventMode_) {
        const rv1126b::VehicleEvent& event = events_.at(index.row());
        if (role == RecordIdRole) {
            return QStringLiteral("%1/%2/%3").arg(event.identity.deviceId)
                .arg(event.identity.eventId).arg(event.identity.trackId);
        }
        if (role == PlateStateRole) return plateStateFor(event);
        if (role == EventIdentityRole) return QVariant::fromValue(event.identity);
        if (role == VehicleEventRole) return QVariant::fromValue(event);
        if (role == RealEventRole) return true;
        if (role == Qt::ToolTipRole
            && event.eventTime.quality.value == rv1126b::TimeQuality::BoardEpochUnverified) {
            return QStringLiteral("板端时间尚未验证，当前显示值不可作为可靠 UTC 依据");
        }
        if (role == Qt::ForegroundRole
            && event.eventTime.quality.value == rv1126b::TimeQuality::BoardEpochUnverified) {
            return QBrush(QColor(180, 90, 0));
        }
        if (role == Qt::DisplayRole) {
            switch (index.column()) {
            case TimeColumn:
                return QDateTime::fromMSecsSinceEpoch(event.eventTime.epochMs)
                    .toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
            case PlateColumn: return event.plateText.isEmpty() ? QStringLiteral("-") : event.plateText;
            case PlateColorColumn: return plateColorText(event.plateColor);
            case EventTypeColumn: return ocrText(event);
            case DeviceIdColumn: return event.identity.deviceId;
            case DirectionColumn: return directionText(event.motionDirection);
            case CoordinateColumn: return QStringLiteral("%1/%2").arg(event.identity.eventId).arg(event.identity.trackId);
            case RemarkColumn: return speedStatusText(event.speedStatus);
            case SpeedColumn: return event.speedValid ? QStringLiteral("%1 km/h").arg(event.speedKmh) : QStringLiteral("无效");
            case TimeQualityColumn: return qualityText(event.eventTime.quality);
            case EvidenceStatusColumn: {
                const auto it = evidenceByIdentity_.constFind(event.identity);
                return it == evidenceByIdentity_.cend() ? evidenceStatusText(event.evidenceStatus)
                                                        : cacheStatusText(it->status);
            }
            default: return {};
            }
        }
        return {};
    }

    const CaptureRecord& record = records_.at(index.row());

    if (role == RecordIdRole) {
        return record.id;
    }

    if (role == PlateStateRole) {
        return record.plateState == CapturePlateState::Unknown
            ? QStringLiteral("unknown")
            : QStringLiteral("valid");
    }

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case TimeColumn:
            return record.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
        case PlateColumn:
            return record.plateNumber;
        case PlateColorColumn:
            return record.plateColor;
        case EventTypeColumn:
            return record.eventType;
        case DeviceIdColumn:
            return record.deviceId;
        case DirectionColumn:
            return record.direction;
        case CoordinateColumn:
            return record.coordinateText;
        case RemarkColumn:
            return record.remark;
        case SpeedColumn:
            return QStringLiteral("%1 km/h").arg(record.speedKmh);
        case TimeQualityColumn:
            return QStringLiteral("模拟数据");
        case EvidenceStatusColumn:
            return record.filePath.isEmpty() ? QStringLiteral("未保存") : QStringLiteral("已保存");
        default:
            return {};
        }
    }

    return {};
}

QVariant CaptureRecordTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }

    switch (section) {
    case TimeColumn:
        return QStringLiteral("抓拍精确时间");
    case PlateColumn:
        return QStringLiteral("车牌号码");
    case PlateColorColumn:
        return QStringLiteral("车牌颜色");
    case EventTypeColumn:
        return QStringLiteral("OCR 状态");
    case DeviceIdColumn:
        return QStringLiteral("设备编号");
    case DirectionColumn:
        return QStringLiteral("通行朝向");
    case CoordinateColumn:
        return QStringLiteral("事件/轨迹 ID");
    case RemarkColumn:
        return QStringLiteral("测速状态");
    case SpeedColumn:
        return QStringLiteral("速度");
    case TimeQualityColumn:
        return QStringLiteral("时间质量");
    case EvidenceStatusColumn:
        return QStringLiteral("图片状态");
    default:
        return {};
    }
}

void CaptureRecordTableModel::setRecords(const QVector<CaptureRecord>& records)
{
    beginResetModel();
    realEventMode_ = false;
    records_ = records;
    events_.clear();
    evidenceByIdentity_.clear();
    endResetModel();
}

void CaptureRecordTableModel::setVehicleEvents(const QVector<rv1126b::VehicleEvent>& events)
{
    beginResetModel();
    realEventMode_ = true;
    records_.clear();
    events_ = events;
    endResetModel();
}

void CaptureRecordTableModel::upsertVehicleEvent(const rv1126b::VehicleEvent& event, int maximumRows)
{
    if (!realEventMode_) setVehicleEvents({});
    for (int row = 0; row < events_.size(); ++row) {
        if (events_.at(row).identity == event.identity) {
            events_[row] = event;
            emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
            return;
        }
    }
    beginInsertRows(QModelIndex(), 0, 0);
    events_.prepend(event);
    endInsertRows();
    if (maximumRows > 0 && events_.size() > maximumRows) {
        beginRemoveRows(QModelIndex(), maximumRows, events_.size() - 1);
        while (events_.size() > maximumRows) events_.removeLast();
        endRemoveRows();
    }
}

void CaptureRecordTableModel::removeVehicleEvent(const rv1126b::EventIdentity& identity)
{
    for (int row = 0; row < events_.size(); ++row) {
        if (events_.at(row).identity == identity) {
            beginRemoveRows(QModelIndex(), row, row);
            events_.removeAt(row);
            evidenceByIdentity_.remove(identity);
            endRemoveRows();
            return;
        }
    }
}

void CaptureRecordTableModel::setEvidenceState(const rv1126b::EvidenceCacheEntry& entry)
{
    evidenceByIdentity_.insert(entry.identity, entry);
    for (int row = 0; row < events_.size(); ++row) {
        if (events_.at(row).identity == entry.identity) {
            emit dataChanged(index(row, EvidenceStatusColumn), index(row, EvidenceStatusColumn));
            return;
        }
    }
}

void CaptureRecordTableModel::addRecord(const CaptureRecord& record)
{
    const int row = records_.size();
    beginInsertRows(QModelIndex(), row, row);
    records_.append(record);
    endInsertRows();
}

bool CaptureRecordTableModel::removeRecord(int row)
{
    if (row < 0 || row >= records_.size()) {
        return false;
    }

    beginRemoveRows(QModelIndex(), row, row);
    records_.removeAt(row);
    endRemoveRows();
    return true;
}

bool CaptureRecordTableModel::removeRecordById(const QString& id)
{
    for (int row = 0; row < records_.size(); ++row) {
        if (records_.at(row).id == id) {
            return removeRecord(row);
        }
    }
    return false;
}

void CaptureRecordTableModel::clear()
{
    beginResetModel();
    records_.clear();
    endResetModel();
}

const CaptureRecord* CaptureRecordTableModel::recordAt(int row) const
{
    if (realEventMode_ || row < 0 || row >= records_.size()) {
        return nullptr;
    }
    return &records_.at(row);
}

const rv1126b::VehicleEvent* CaptureRecordTableModel::vehicleEventAt(int row) const
{
    if (!realEventMode_ || row < 0 || row >= events_.size()) return nullptr;
    return &events_.at(row);
}

std::optional<rv1126b::EvidenceCacheEntry> CaptureRecordTableModel::evidenceStateAt(int row) const
{
    const rv1126b::VehicleEvent* event = vehicleEventAt(row);
    if (!event) return std::nullopt;
    const auto it = evidenceByIdentity_.constFind(event->identity);
    return it == evidenceByIdentity_.cend() ? std::nullopt : std::optional<rv1126b::EvidenceCacheEntry>(*it);
}

const CaptureRecord* CaptureRecordTableModel::latestForDevice(const QString& deviceId) const
{
    for (int i = records_.size() - 1; i >= 0; --i) {
        if (records_.at(i).deviceId == deviceId) {
            return &records_[i];
        }
    }
    return nullptr;
}

int CaptureRecordTableModel::recordCount() const
{
    return rowCount();
}

bool CaptureRecordTableModel::realEventMode() const { return realEventMode_; }

QString CaptureRecordTableModel::plateStateFor(const rv1126b::VehicleEvent& event) const
{
    using rv1126b::OcrStatus;
    if (event.ocrStatus.value == OcrStatus::Queued || event.ocrStatus.value == OcrStatus::Unknown)
        return QStringLiteral("pending");
    return event.plateText.trimmed().isEmpty() ? QStringLiteral("unknown") : QStringLiteral("valid");
}
