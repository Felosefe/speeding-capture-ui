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
    // 真机模式下左下角属性表要显示板端"展示配置"（点位名称/道路方向/限速），
    // 而不是本机 DeviceConfig 里的残留默认值。传入空字符串表示尚未从板端读到。
    void setBoardSiteInfo(const QString& siteName, const QString& roadDirection, int speedLimitKmh);

private:
    void rebuildProperties();

    QVector<QPair<QString, QString>> properties_;
    const Device* device_ = nullptr;
    const CaptureRecord* latestRecord_ = nullptr;
    bool hasBoardSiteInfo_ = false;
    QString boardSiteName_;
    QString boardRoadDirection_;
    int boardSpeedLimitKmh_ = 0;
};
