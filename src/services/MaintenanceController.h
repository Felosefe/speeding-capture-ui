#pragma once

#include "../models/SystemSettings.h"

#include <QDate>
#include <QDateTime>
#include <QObject>
#include <QTimer>

#include <functional>

class MaintenanceController final : public QObject
{
    Q_OBJECT

public:
    explicit MaintenanceController(QObject* parent = nullptr);

    void setSettings(const MaintenanceSettings& settings);
    MaintenanceSettings settings() const;
    void setSyncDeviceTimesCallback(std::function<void()> callback);
    void setShutdownCallback(std::function<void()> callback);
    void setDiskMaintenanceCallback(std::function<void()> callback);
    void start();
    void stop();
    void evaluate(const QDateTime& now = QDateTime::currentDateTime());

private:
    bool sameConfiguredMinute(const QTime& left, const QTime& right) const;

    MaintenanceSettings settings_;
    QTimer timer_;
    std::function<void()> syncDeviceTimesCallback_;
    std::function<void()> shutdownCallback_;
    std::function<void()> diskMaintenanceCallback_;
    QDate lastSyncDate_;
    QDate lastShutdownDate_;
    QDate lastDiskMaintenanceDate_;
};
