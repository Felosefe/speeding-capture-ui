#pragma once

#include "../CaptureRecord.h"

#include <QAbstractTableModel>
#include <QVector>

class CaptureRecordTableModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        TimeColumn,
        PlateColumn,
        PlateColorColumn,
        EventTypeColumn,
        DeviceIdColumn,
        DirectionColumn,
        CoordinateColumn,
        RemarkColumn,
        ColumnCount
    };

    enum Role {
        RecordIdRole = Qt::UserRole + 1,
        PlateStateRole
    };

    explicit CaptureRecordTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void setRecords(const QVector<CaptureRecord>& records);
    void addRecord(const CaptureRecord& record);
    bool removeRecord(int row);
    bool removeRecordById(const QString& id);
    void clear();
    const CaptureRecord* recordAt(int row) const;
    const CaptureRecord* latestForDevice(const QString& deviceId) const;
    int recordCount() const;

private:
    QVector<CaptureRecord> records_;
};
