#include "DeviceManager.h"

#include "../device/MockDeviceClient.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

DeviceManager::DeviceManager(QObject* parent)
    : QObject(parent)
{
}

void DeviceManager::addMockDevice()
{
    const int row = devices_.size();
    Device device = createMockDevice(row + 1);
    loadStoredConfig(device);
    auto* client = new MockDeviceClient(device, this);

    devices_.append({device, client});

    connect(client, &IDeviceClient::statusChanged, this, [this, row](const DeviceStatus& status) {
        if (auto* managed = managedAt(row)) {
            managed->device.status = status;
        }
        emit deviceStatusChanged(row, status);
    });

    connect(client, &IDeviceClient::captureGenerated, this, [this, row](const CaptureRecord& record) {
        emit captureGenerated(row, record);
    });

    connect(client, &IDeviceClient::errorOccurred, this, [this, row](const QString& message) {
        emit errorOccurred(row, message);
    });

    emit deviceAdded(device);
}

void DeviceManager::seedMockDevices(int count)
{
    if (!devices_.isEmpty()) {
        return;
    }

    for (int i = 0; i < count; ++i) {
        addMockDevice();
    }
}

bool DeviceManager::connectDevice(int row)
{
    auto* managed = managedAt(row);
    return managed ? managed->client->connectDevice() : false;
}

int DeviceManager::connectAllDevices()
{
    int connected = 0;
    for (int row = 0; row < devices_.size(); ++row) {
        if (connectDevice(row)) {
            ++connected;
        }
    }
    return connected;
}

void DeviceManager::disconnectDevice(int row)
{
    if (auto* managed = managedAt(row)) {
        managed->client->disconnectDevice();
    }
}

int DeviceManager::disconnectAllDevices()
{
    int disconnected = 0;
    for (int row = 0; row < devices_.size(); ++row) {
        if (auto* managed = managedAt(row)) {
            managed->client->disconnectDevice();
            ++disconnected;
        }
    }
    return disconnected;
}

bool DeviceManager::triggerCapture(int row)
{
    auto* managed = managedAt(row);
    return managed ? managed->client->triggerCapture() : false;
}

bool DeviceManager::rebootDevice(int row)
{
    auto* managed = managedAt(row);
    return managed ? managed->client->reboot() : false;
}

bool DeviceManager::syncDeviceTime(int row)
{
    auto* managed = managedAt(row);
    return managed ? managed->client->syncTime(QDateTime::currentDateTime()) : false;
}

int DeviceManager::syncAllDeviceTimes()
{
    int synced = 0;
    for (int row = 0; row < devices_.size(); ++row) {
        if (syncDeviceTime(row)) {
            ++synced;
        }
    }
    return synced;
}

bool DeviceManager::writeDeviceConfig(int row, const DeviceConfig& config)
{
    auto* managed = managedAt(row);
    if (!managed) {
        return false;
    }

    if (!managed->client->writeConfig(config)) {
        return false;
    }

    managed->device.config = config;
    saveStoredConfig(managed->device);
    emit deviceConfigChanged(row, config);
    return true;
}

int DeviceManager::deviceCount() const
{
    return devices_.size();
}

const Device* DeviceManager::deviceAt(int row) const
{
    const auto* managed = managedAt(row);
    return managed ? &managed->device : nullptr;
}

DeviceManager::ManagedDevice* DeviceManager::managedAt(int row)
{
    if (row < 0 || row >= devices_.size()) {
        return nullptr;
    }
    return &devices_[row];
}

const DeviceManager::ManagedDevice* DeviceManager::managedAt(int row) const
{
    if (row < 0 || row >= devices_.size()) {
        return nullptr;
    }
    return &devices_[row];
}

Device DeviceManager::createMockDevice(int number) const
{
    Device device;
    device.id = QStringLiteral("mock-%1").arg(number);
    device.name = QStringLiteral("模拟设备-%1").arg(number, 2, 10, QLatin1Char('0'));
    device.ipAddress = QStringLiteral("192.168.1.%1").arg(99 + number);
    device.port = 8000;
    device.config.location = QStringLiteral("测试路口%1").arg(number);
    device.config.direction = number % 2 == 0 ? QStringLiteral("由南向北") : QStringLiteral("由北向南");
    device.config.laneName = QStringLiteral("%1车道").arg(number);
    device.config.speedLimitKmh = number % 3 == 0 ? 80 : 60;
    device.status.runningState = QStringLiteral("未连接");
    return device;
}

void DeviceManager::loadStoredConfig(Device& device) const
{
    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("devices/%1").arg(device.id));

    device.config.location = settings.value(QStringLiteral("location"), device.config.location).toString();
    device.config.direction = settings.value(QStringLiteral("direction"), device.config.direction).toString();
    device.config.laneName = settings.value(QStringLiteral("laneName"), device.config.laneName).toString();
    device.config.speedLimitKmh = settings.value(QStringLiteral("speedLimitKmh"), device.config.speedLimitKmh).toInt();
    device.config.savePlateImage = settings.value(QStringLiteral("savePlateImage"), device.config.savePlateImage).toBool();
    device.config.enableOverspeedAlert = settings.value(QStringLiteral("enableOverspeedAlert"), device.config.enableOverspeedAlert).toBool();
    device.config.ntpEnabled = settings.value(QStringLiteral("ntpEnabled"), device.config.ntpEnabled).toBool();
    device.config.cameraModel = settings.value(QStringLiteral("cameraModel"), device.config.cameraModel).toString();
    device.config.streamResolution = settings.value(QStringLiteral("streamResolution"), device.config.streamResolution).toString();
    device.config.previewFrameRate = settings.value(QStringLiteral("previewFrameRate"), device.config.previewFrameRate).toInt();
    device.config.adminUser = settings.value(QStringLiteral("adminUser"), device.config.adminUser).toString();
    device.config.remoteAuthEnabled = settings.value(QStringLiteral("remoteAuthEnabled"), device.config.remoteAuthEnabled).toBool();
    device.config.ipWhitelist = settings.value(QStringLiteral("ipWhitelist"), device.config.ipWhitelist).toString();
    device.config.radarModel = settings.value(QStringLiteral("radarModel"), device.config.radarModel).toString();
    device.config.speedCalibration = settings.value(QStringLiteral("speedCalibration"), device.config.speedCalibration).toDouble();
    device.config.lowSpeedFilterKmh = settings.value(QStringLiteral("lowSpeedFilterKmh"), device.config.lowSpeedFilterKmh).toInt();
    device.config.flashEnabled = settings.value(QStringLiteral("flashEnabled"), device.config.flashEnabled).toBool();
    device.config.recognitionMarginLeft = settings.value(QStringLiteral("recognitionMarginLeft"), device.config.recognitionMarginLeft).toInt();
    device.config.recognitionMarginTop = settings.value(QStringLiteral("recognitionMarginTop"), device.config.recognitionMarginTop).toInt();
    device.config.recognitionMarginRight = settings.value(QStringLiteral("recognitionMarginRight"), device.config.recognitionMarginRight).toInt();
    device.config.recognitionMarginBottom = settings.value(QStringLiteral("recognitionMarginBottom"), device.config.recognitionMarginBottom).toInt();
    device.config.saveUnknownPlate = settings.value(QStringLiteral("saveUnknownPlate"), device.config.saveUnknownPlate).toBool();
    device.config.enableBlacklist = settings.value(QStringLiteral("enableBlacklist"), device.config.enableBlacklist).toBool();
    device.config.overspeedViolationName = settings.value(QStringLiteral("overspeedViolationName"), device.config.overspeedViolationName).toString();
    device.config.overspeedViolationCode = settings.value(QStringLiteral("overspeedViolationCode"), device.config.overspeedViolationCode).toString();
    device.config.preRecordSeconds = settings.value(QStringLiteral("preRecordSeconds"), device.config.preRecordSeconds).toInt();
    device.config.postRecordSeconds = settings.value(QStringLiteral("postRecordSeconds"), device.config.postRecordSeconds).toInt();
    device.config.jpegQuality = settings.value(QStringLiteral("jpegQuality"), device.config.jpegQuality).toInt();
    device.config.rtspEnabled = settings.value(QStringLiteral("rtspEnabled"), device.config.rtspEnabled).toBool();
    device.config.uploadEnabled = settings.value(QStringLiteral("uploadEnabled"), device.config.uploadEnabled).toBool();
    device.config.uploadServer = settings.value(QStringLiteral("uploadServer"), device.config.uploadServer).toString();
    device.config.uploadPort = settings.value(QStringLiteral("uploadPort"), device.config.uploadPort).toInt();

    settings.endGroup();
}

void DeviceManager::saveStoredConfig(const Device& device) const
{
    const QString path = settingsFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QSettings settings(path, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("devices/%1").arg(device.id));

    settings.setValue(QStringLiteral("location"), device.config.location);
    settings.setValue(QStringLiteral("direction"), device.config.direction);
    settings.setValue(QStringLiteral("laneName"), device.config.laneName);
    settings.setValue(QStringLiteral("speedLimitKmh"), device.config.speedLimitKmh);
    settings.setValue(QStringLiteral("savePlateImage"), device.config.savePlateImage);
    settings.setValue(QStringLiteral("enableOverspeedAlert"), device.config.enableOverspeedAlert);
    settings.setValue(QStringLiteral("ntpEnabled"), device.config.ntpEnabled);
    settings.setValue(QStringLiteral("cameraModel"), device.config.cameraModel);
    settings.setValue(QStringLiteral("streamResolution"), device.config.streamResolution);
    settings.setValue(QStringLiteral("previewFrameRate"), device.config.previewFrameRate);
    settings.setValue(QStringLiteral("adminUser"), device.config.adminUser);
    settings.setValue(QStringLiteral("remoteAuthEnabled"), device.config.remoteAuthEnabled);
    settings.setValue(QStringLiteral("ipWhitelist"), device.config.ipWhitelist);
    settings.setValue(QStringLiteral("radarModel"), device.config.radarModel);
    settings.setValue(QStringLiteral("speedCalibration"), device.config.speedCalibration);
    settings.setValue(QStringLiteral("lowSpeedFilterKmh"), device.config.lowSpeedFilterKmh);
    settings.setValue(QStringLiteral("flashEnabled"), device.config.flashEnabled);
    settings.setValue(QStringLiteral("recognitionMarginLeft"), device.config.recognitionMarginLeft);
    settings.setValue(QStringLiteral("recognitionMarginTop"), device.config.recognitionMarginTop);
    settings.setValue(QStringLiteral("recognitionMarginRight"), device.config.recognitionMarginRight);
    settings.setValue(QStringLiteral("recognitionMarginBottom"), device.config.recognitionMarginBottom);
    settings.setValue(QStringLiteral("saveUnknownPlate"), device.config.saveUnknownPlate);
    settings.setValue(QStringLiteral("enableBlacklist"), device.config.enableBlacklist);
    settings.setValue(QStringLiteral("overspeedViolationName"), device.config.overspeedViolationName);
    settings.setValue(QStringLiteral("overspeedViolationCode"), device.config.overspeedViolationCode);
    settings.setValue(QStringLiteral("preRecordSeconds"), device.config.preRecordSeconds);
    settings.setValue(QStringLiteral("postRecordSeconds"), device.config.postRecordSeconds);
    settings.setValue(QStringLiteral("jpegQuality"), device.config.jpegQuality);
    settings.setValue(QStringLiteral("rtspEnabled"), device.config.rtspEnabled);
    settings.setValue(QStringLiteral("uploadEnabled"), device.config.uploadEnabled);
    settings.setValue(QStringLiteral("uploadServer"), device.config.uploadServer);
    settings.setValue(QStringLiteral("uploadPort"), device.config.uploadPort);

    settings.endGroup();
    settings.sync();
}

QString DeviceManager::settingsFilePath() const
{
#ifdef CAMERA_MANAGER_SOURCE_DIR
    QDir root(QString::fromUtf8(CAMERA_MANAGER_SOURCE_DIR));
#else
    QDir root(QCoreApplication::applicationDirPath());
#endif
    return root.filePath(QStringLiteral("data/config/devices.ini"));
}
