#pragma once

#include "../CaptureRecord.h"
#include "../Device.h"

#include <QAbstractTableModel>
#include <QPair>
#include <QVector>

class DevicePropertyModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit DevicePropertyModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void clear();
    void setDevice(const Device* device, const CaptureRecord* latestRecord);

private:
    QVector<QPair<QString, QString>> properties_;
};
