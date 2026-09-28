#include "MockDeviceClient.h"

#include <QRandomGenerator>

MockDeviceClient::MockDeviceClient(const Device& device, QObject* parent)
    : IDeviceClient(parent)
    , device_(device)
    , status_(device.status)
    , config_(device.config)
{
    statusTimer_.setInterval(2500);
    captureTimer_.setInterval(8000);

    connect(&statusTimer_, &QTimer::timeout, this, &MockDeviceClient::refreshStatus);
    connect(&captureTimer_, &QTimer::timeout, this, &MockDeviceClient::generateAutomaticCapture);
}

bool MockDeviceClient::connectDevice()
{
    status_.connectionState = DeviceConnectionState::Online;
    status_.runningState = QStringLiteral("运行正常");
    status_.lastHeartbeat = QDateTime::currentDateTime();

    statusTimer_.start();
    captureTimer_.start();

    emit statusChanged(status_);
    return true;
}

void MockDeviceClient::disconnectDevice()
{
    statusTimer_.stop();
    captureTimer_.stop();

    status_.connectionState = DeviceConnectionState::Offline;
    status_.runningState = QStringLiteral("未连接");
    status_.lastHeartbeat = QDateTime();

    emit statusChanged(status_);
}

DeviceStatus MockDeviceClient::readStatus() const
{
    return status_;
}

DeviceConfig MockDeviceClient::readConfig() const
{
    return config_;
}

bool MockDeviceClient::writeConfig(const DeviceConfig& config)
{
    if (status_.connectionState != DeviceConnectionState::Online) {
        emit errorOccurred(QStringLiteral("离线设备不能下发参数"));
        return false;
    }

    config_ = config;
    device_.config = config;
    return true;
}

bool MockDeviceClient::reboot()
{
    if (status_.connectionState != DeviceConnectionState::Online) {
        emit errorOccurred(QStringLiteral("离线设备不能重启"));
        return false;
    }

    status_.runningState = QStringLiteral("重启中");
    emit statusChanged(status_);

    QTimer::singleShot(1200, this, [this]() {
        status_.runningState = QStringLiteral("运行正常");
        status_.lastHeartbeat = QDateTime::currentDateTime();
        emit statusChanged(status_);
    });

    return true;
}

bool MockDeviceClient::syncTime(const QDateTime& time)
{
    Q_UNUSED(time)

    if (status_.connectionState != DeviceConnectionState::Online) {
        emit errorOccurred(QStringLiteral("离线设备不能同步时间"));
        return false;
    }

    status_.lastHeartbeat = QDateTime::currentDateTime();
    emit statusChanged(status_);
    return true;
}

bool MockDeviceClient::triggerCapture()
{
    if (status_.connectionState != DeviceConnectionState::Online) {
        emit errorOccurred(QStringLiteral("请先连接设备再触发抓拍"));
        return false;
    }

    ++status_.captureCount;
    emit captureGenerated(createCaptureRecord());
    emit statusChanged(status_);
    return true;
}

void MockDeviceClient::refreshStatus()
{
    if (status_.connectionState == DeviceConnectionState::Offline) {
        return;
    }

    const int roll = static_cast<int>(QRandomGenerator::global()->bounded(100));
    if (roll < 5) {
        status_.connectionState = DeviceConnectionState::Fault;
        status_.runningState = QStringLiteral("模拟链路抖动");
    } else {
        status_.connectionState = DeviceConnectionState::Online;
        status_.runningState = QStringLiteral("运行正常");
    }

    status_.lastHeartbeat = QDateTime::currentDateTime();
    emit statusChanged(status_);
}

void MockDeviceClient::generateAutomaticCapture()
{
    if (status_.connectionState != DeviceConnectionState::Online) {
        return;
    }

    ++status_.captureCount;
    emit captureGenerated(createCaptureRecord());
    emit statusChanged(status_);
}

CaptureRecord MockDeviceClient::createCaptureRecord() const
{
    const int speed = 35 + static_cast<int>(QRandomGenerator::global()->bounded(70));
    const int roll = static_cast<int>(QRandomGenerator::global()->bounded(20));
    const bool unknownPlate = roll == 0;
    const int boxX = 680 + static_cast<int>(QRandomGenerator::global()->bounded(160));
    const int boxY = 320 + static_cast<int>(QRandomGenerator::global()->bounded(90));

    CaptureRecord record;
    record.id = QStringLiteral("capture-%1-%2").arg(device_.id, QString::number(QDateTime::currentMSecsSinceEpoch()));
    record.timestamp = QDateTime::currentDateTime();
    record.deviceId = device_.id;
    record.deviceName = device_.name;
    record.plateNumber = unknownPlate ? QStringLiteral("未知") : randomPlateNumber();
    record.plateColor = unknownPlate ? QStringLiteral("-") : QStringLiteral("蓝牌");
    record.eventType = QStringLiteral("抓拍");
    record.speedKmh = speed;
    record.speedLimitKmh = config_.speedLimitKmh;
    record.direction = config_.direction;
    record.coordinateText = QStringLiteral("w=1920,c=(%1,%2,240,80)").arg(boxX).arg(boxY);
    record.remark = unknownPlate ? QStringLiteral("无牌未知记录") : QString();
    record.plateState = unknownPlate ? CapturePlateState::Unknown : CapturePlateState::Valid;
    record.filePath = QStringLiteral("未保存");

    if (unknownPlate) {
        record.type = CaptureType::UnknownPlate;
    } else if (roll == 1) {
        record.type = CaptureType::Blacklist;
    } else if (speed > config_.speedLimitKmh) {
        record.type = CaptureType::Overspeed;
    } else {
        record.type = CaptureType::Normal;
    }

    return record;
}

QString MockDeviceClient::randomPlateNumber() const
{
    static const QString letters = QStringLiteral("ABCDEFGHJKLMNPQRSTUVWXYZ");
    const int letterIndex = static_cast<int>(QRandomGenerator::global()->bounded(letters.size()));
    const int number = 10000 + static_cast<int>(QRandomGenerator::global()->bounded(90000));
    return QStringLiteral("粤%1%2").arg(letters.at(letterIndex)).arg(number);
}
