#include "DeviceDiscoveryDialog.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

#include <utility>

namespace {

QString sessionStateText(rv1126b::DeviceSessionState state)
{
    using rv1126b::DeviceSessionState;
    switch (state) {
    case DeviceSessionState::Connecting:
        return QStringLiteral("连接中");
    case DeviceSessionState::Online:
        return QStringLiteral("在线");
    case DeviceSessionState::Degraded:
        return QStringLiteral("退化");
    case DeviceSessionState::AuthenticationFailed:
        return QStringLiteral("认证失败");
    case DeviceSessionState::Disconnecting:
        return QStringLiteral("断开中");
    case DeviceSessionState::Disconnected:
    default:
        return QStringLiteral("未连接");
    }
}

} // namespace

DeviceDiscoveryDialog::DeviceDiscoveryDialog(
    rv1126b::DeviceIntegrationController* controller,
    QString initialDeviceId,
    QWidget* parent)
    : QDialog(parent)
    , controller_(controller)
    , initialDeviceId_(std::move(initialDeviceId))
{
    setWindowTitle(QStringLiteral("搜索并连接 RV1126B 设备"));
    resize(820, 460);

    auto* root = new QVBoxLayout(this);
    auto* helpLabel = new QLabel(
        QStringLiteral("按 device_id 识别设备。Bearer Token 仅用于本次安全保存，不会写入普通配置或日志。"),
        this);
    helpLabel->setWordWrap(true);
    root->addWidget(helpLabel);

    deviceTable_ = new QTableWidget(0, 6, this);
    deviceTable_->setObjectName(QStringLiteral("discoveredDeviceTable"));
    deviceTable_->setHorizontalHeaderLabels({
        QStringLiteral("设备 ID"),
        QStringLiteral("型号"),
        QStringLiteral("IPv4"),
        QStringLiteral("版本"),
        QStringLiteral("认证"),
        QStringLiteral("状态"),
    });
    deviceTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    deviceTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    deviceTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    deviceTable_->verticalHeader()->setVisible(false);
    deviceTable_->horizontalHeader()->setStretchLastSection(true);
    deviceTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    root->addWidget(deviceTable_, 1);

    auto* form = new QFormLayout;
    tokenEdit_ = new QLineEdit(this);
    tokenEdit_->setObjectName(QStringLiteral("bearerTokenEdit"));
    tokenEdit_->setEchoMode(QLineEdit::Password);
    tokenEdit_->setClearButtonEnabled(true);
    tokenEdit_->setPlaceholderText(QStringLiteral("输入新 Token；已有安全引用时可留空"));
    form->addRow(QStringLiteral("Bearer Token"), tokenEdit_);
    root->addLayout(form);

    messageLabel_ = new QLabel(this);
    messageLabel_->setObjectName(QStringLiteral("discoveryMessageLabel"));
    messageLabel_->setWordWrap(true);
    root->addWidget(messageLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    searchButton_ = buttons->addButton(QStringLiteral("重新搜索"), QDialogButtonBox::ActionRole);
    searchButton_->setObjectName(QStringLiteral("searchDevicesButton"));
    connectButton_ = buttons->addButton(QStringLiteral("连接所选设备"), QDialogButtonBox::AcceptRole);
    connectButton_->setObjectName(QStringLiteral("connectDiscoveredDeviceButton"));
    connectButton_->setEnabled(false);
    root->addWidget(buttons);

    connect(searchButton_, &QPushButton::clicked, this, &DeviceDiscoveryDialog::startScan);
    connect(connectButton_, &QPushButton::clicked, this, &DeviceDiscoveryDialog::connectSelectedDevice);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(deviceTable_, &QTableWidget::itemSelectionChanged,
            this, &DeviceDiscoveryDialog::updateSelection);

    if (controller_) {
        connect(controller_, &rv1126b::DeviceIntegrationController::discoveredDevicesReset,
                this, &DeviceDiscoveryDialog::resetDevices);
        connect(controller_, &rv1126b::DeviceIntegrationController::discoveredDeviceUpserted,
                this, &DeviceDiscoveryDialog::upsertDevice);
        connect(controller_, &rv1126b::DeviceIntegrationController::scanStateChanged,
                this, &DeviceDiscoveryDialog::updateScanState);
        connect(controller_, &rv1126b::DeviceIntegrationController::sessionChanged,
                this, &DeviceDiscoveryDialog::updateSession);
        connect(controller_, &rv1126b::DeviceIntegrationController::userError,
                this, &DeviceDiscoveryDialog::showControllerError);

        for (const auto& device : controller_->discoveredDevices()) {
            upsertDevice(device);
        }
    }

    const bool available = controller_ && controller_->networkServicesAvailable();
    searchButton_->setEnabled(available);
    if (!available) {
        setMessage(QStringLiteral("真实设备网络服务尚未装配，当前不能搜索或连接设备"), true);
    } else if (deviceTable_->rowCount() == 0) {
        QTimer::singleShot(0, this, &DeviceDiscoveryDialog::startScan);
    } else {
        selectInitialDevice();
        setMessage(QStringLiteral("请选择设备并配置 Token"));
    }
}

DeviceDiscoveryDialog::~DeviceDiscoveryDialog()
{
    if (controller_ && controller_->isScanning()) {
        controller_->cancelScan();
    }
    tokenEdit_->clear();
}

void DeviceDiscoveryDialog::startScan()
{
    if (!controller_ || !controller_->networkServicesAvailable()) {
        setMessage(QStringLiteral("真实设备网络服务尚未装配"), true);
        return;
    }
    controller_->startScan();
}

void DeviceDiscoveryDialog::connectSelectedDevice()
{
    const QString deviceId = selectedDeviceId();
    if (!controller_ || deviceId.isEmpty()) {
        setMessage(QStringLiteral("请先选择一个设备"), true);
        return;
    }

    QString tokenText = tokenEdit_->text();
    QByteArray tokenBytes = tokenText.toUtf8();
    tokenEdit_->clear();
    tokenText.fill(QChar(u'\0'));
    tokenText.clear();

    if (controller_->isScanning()) {
        controller_->cancelScan();
    }
    const bool started = controller_->connectDiscoveredDevice(
        deviceId, rv1126b::SecretValue(std::move(tokenBytes)));
    if (started) {
        connectButton_->setEnabled(false);
        setMessage(QStringLiteral("正在验证设备身份…"));
    }
}

void DeviceDiscoveryDialog::updateSelection()
{
    const QString deviceId = selectedDeviceId();
    connectButton_->setEnabled(controller_
                               && controller_->networkServicesAvailable()
                               && !deviceId.isEmpty());
    if (controller_ && !deviceId.isEmpty() && controller_->hasCredentialForDevice(deviceId)) {
        tokenEdit_->setPlaceholderText(QStringLiteral("留空继续使用已安全保存的 Token"));
    } else {
        tokenEdit_->setPlaceholderText(QStringLiteral("请输入该设备的 Bearer Token"));
    }
}

void DeviceDiscoveryDialog::resetDevices()
{
    deviceTable_->setRowCount(0);
    connectButton_->setEnabled(false);
}

void DeviceDiscoveryDialog::upsertDevice(const rv1126b::DiscoveredDeviceDto& device)
{
    int row = rowForDevice(device.deviceId);
    if (row < 0) {
        row = deviceTable_->rowCount();
        deviceTable_->insertRow(row);
        for (int column = 0; column < deviceTable_->columnCount(); ++column) {
            deviceTable_->setItem(row, column, new QTableWidgetItem);
        }
    }

    deviceTable_->item(row, 0)->setText(device.deviceId);
    deviceTable_->item(row, 0)->setData(Qt::UserRole, device.deviceId);
    deviceTable_->item(row, 1)->setText(device.deviceModel);
    deviceTable_->item(row, 2)->setText(device.ipv4);
    deviceTable_->item(row, 3)->setText(device.releaseVersion);
    deviceTable_->item(row, 4)->setText(device.authRequired ? QStringLiteral("需要 Token")
                                                            : QStringLiteral("无需认证"));
    deviceTable_->item(row, 5)->setText(
        sessionStateText(sessionStates_.value(device.deviceId,
                                              rv1126b::DeviceSessionState::Disconnected)));
    selectInitialDevice();
}

void DeviceDiscoveryDialog::updateScanState(bool scanning)
{
    searchButton_->setEnabled(!scanning && controller_ && controller_->networkServicesAvailable());
    searchButton_->setText(scanning ? QStringLiteral("正在搜索…") : QStringLiteral("重新搜索"));
    if (scanning) {
        setMessage(QStringLiteral("正在向可用 IPv4 子网搜索设备…"));
    } else {
        setMessage(QStringLiteral("搜索完成，共发现 %1 台设备").arg(deviceTable_->rowCount()));
        selectInitialDevice();
    }
}

void DeviceDiscoveryDialog::updateSession(const rv1126b::DeviceSessionSnapshot& snapshot)
{
    const QString deviceId = snapshot.profile.deviceId;
    sessionStates_.insert(deviceId, snapshot.state);
    const int row = rowForDevice(deviceId);
    if (row >= 0) {
        deviceTable_->item(row, 5)->setText(sessionStateText(snapshot.state));
    }
    if (deviceId != selectedDeviceId()) {
        return;
    }

    if (snapshot.state == rv1126b::DeviceSessionState::Online) {
        setMessage(QStringLiteral("设备身份验证成功，正在打开实时视频"));
        QTimer::singleShot(0, this, &QDialog::accept);
    } else if (snapshot.state == rv1126b::DeviceSessionState::AuthenticationFailed) {
        connectButton_->setEnabled(true);
        tokenEdit_->setFocus();
        setMessage(QStringLiteral("认证失败，请重新配置 Token"), true);
    } else if (snapshot.state == rv1126b::DeviceSessionState::Degraded) {
        connectButton_->setEnabled(true);
        setMessage(QStringLiteral("设备网络暂时不可用，请稍后重试"), true);
    }
}

void DeviceDiscoveryDialog::showControllerError(const QString&, const QString& message)
{
    connectButton_->setEnabled(!selectedDeviceId().isEmpty()
                               && controller_
                               && controller_->networkServicesAvailable());
    setMessage(message, true);
}

int DeviceDiscoveryDialog::rowForDevice(const QString& deviceId) const
{
    for (int row = 0; row < deviceTable_->rowCount(); ++row) {
        if (deviceTable_->item(row, 0)->data(Qt::UserRole).toString() == deviceId) {
            return row;
        }
    }
    return -1;
}

QString DeviceDiscoveryDialog::selectedDeviceId() const
{
    const int row = deviceTable_->currentRow();
    if (row < 0 || !deviceTable_->item(row, 0)) {
        return {};
    }
    return deviceTable_->item(row, 0)->data(Qt::UserRole).toString();
}

void DeviceDiscoveryDialog::selectInitialDevice()
{
    if (deviceTable_->currentRow() >= 0 || deviceTable_->rowCount() == 0) {
        return;
    }
    int row = initialDeviceId_.isEmpty() ? 0 : rowForDevice(initialDeviceId_);
    if (row < 0) {
        row = 0;
    }
    deviceTable_->selectRow(row);
}

void DeviceDiscoveryDialog::setMessage(const QString& message, bool error)
{
    messageLabel_->setText(message);
    messageLabel_->setStyleSheet(error ? QStringLiteral("color: #b42318;") : QString());
}
