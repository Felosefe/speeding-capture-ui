#pragma once

#include "../models/SystemSettings.h"

#include <QObject>

class QSettings;

class SystemSettingsService final : public QObject
{
    Q_OBJECT

public:
    explicit SystemSettingsService(QObject* parent = nullptr);
    explicit SystemSettingsService(const QString& settingsPath, QObject* parent = nullptr);

    SystemSettings load();
    bool save(const SystemSettings& settings);
    SystemSettings settings() const;
    QString lastError() const;
    QString settingsPath() const;

signals:
    void settingsChanged(const SystemSettings& settings);

private:
    QString defaultSettingsPath() const;
    SystemSettings normalized(SystemSettings settings) const;
    void readUiSettings(QSettings& store, UiSettings& settings) const;
    void readStorageSettings(QSettings& store, StorageSettings& settings) const;
    void readMaintenanceSettings(QSettings& store, MaintenanceSettings& settings) const;
    void writeUiSettings(QSettings& store, const UiSettings& settings) const;
    void writeStorageSettings(QSettings& store, const StorageSettings& settings) const;
    void writeMaintenanceSettings(QSettings& store, const MaintenanceSettings& settings) const;
    void setLastError(const QString& message) const;

    QString settingsPath_;
    SystemSettings settings_;
    mutable QString lastError_;
};
