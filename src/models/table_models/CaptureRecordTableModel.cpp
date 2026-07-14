#include "CaptureRecordTableModel.h"

#include <QBrush>
#include <QColor>

CaptureRecordTableModel::CaptureRecordTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int CaptureRecordTableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : records_.size();
}

int CaptureRecordTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant CaptureRecordTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= records_.size()) {
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
        return QStringLiteral("事件类型");
    case DeviceIdColumn:
        return QStringLiteral("设备编号");
    case DirectionColumn:
        return QStringLiteral("通行朝向");
    case CoordinateColumn:
        return QStringLiteral("画面坐标参数 (w/c)");
    case RemarkColumn:
        return QStringLiteral("备注");
    default:
        return {};
    }
}

void CaptureRecordTableModel::setRecords(const QVector<CaptureRecord>& records)
{
    beginResetModel();
    records_ = records;
    endResetModel();
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
    if (row < 0 || row >= records_.size()) {
        return nullptr;
    }
    return &records_.at(row);
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
    return records_.size();
}
