#include "MaintenanceController.h"

MaintenanceController::MaintenanceController(QObject* parent)
    : QObject(parent)
{
    timer_.setInterval(60 * 1000);
    connect(&timer_, &QTimer::timeout, this, [this]() {
        evaluate();
    });
}

void MaintenanceController::setSettings(const MaintenanceSettings& settings)
{
    settings_ = settings;
}

MaintenanceSettings MaintenanceController::settings() const
{
    return settings_;
}

void MaintenanceController::setSyncDeviceTimesCallback(std::function<void()> callback)
{
    syncDeviceTimesCallback_ = std::move(callback);
}

void MaintenanceController::setShutdownCallback(std::function<void()> callback)
{
    shutdownCallback_ = std::move(callback);
}

void MaintenanceController::setDiskMaintenanceCallback(std::function<void()> callback)
{
    diskMaintenanceCallback_ = std::move(callback);
}

void MaintenanceController::start()
{
    timer_.start();
}

void MaintenanceController::stop()
{
    timer_.stop();
}

void MaintenanceController::evaluate(const QDateTime& now)
{
    if (settings_.enableDailyDeviceTimeSync
        && sameConfiguredMinute(now.time(), settings_.dailySyncTime)
        && lastSyncDate_ != now.date()) {
        lastSyncDate_ = now.date();
        if (syncDeviceTimesCallback_) {
            syncDeviceTimesCallback_();
        }
    }

    if (settings_.enableScheduledShutdown
        && sameConfiguredMinute(now.time(), settings_.shutdownTime)
        && lastShutdownDate_ != now.date()) {
        lastShutdownDate_ = now.date();
        if (shutdownCallback_) {
            shutdownCallback_();
        }
    }

    if (settings_.enableDiskMaintenance
        && now.time().hour() == 4
        && now.time().minute() == 0
        && lastDiskMaintenanceDate_ != now.date()) {
        lastDiskMaintenanceDate_ = now.date();
        if (diskMaintenanceCallback_) {
            diskMaintenanceCallback_();
        }
    }
}

bool MaintenanceController::sameConfiguredMinute(const QTime& left, const QTime& right) const
{
    return left.hour() == right.hour() && left.minute() == right.minute();
}
