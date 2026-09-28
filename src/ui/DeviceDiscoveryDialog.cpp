#include "DeviceDiscoveryDialog.h"

#include <QAbstractItemView>
#include <QColor>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QDateTime>

#include <utility>

namespace {

// 用户可见术语统一表。板端协议里叫 Bearer Token，界面上一律叫「验证码」——
// 现场操作员不认识 Token 这个词，而它实际就是设备侧预置的一串访问口令。
const QString kVerificationCodeLabel = QStringLiteral("验证码");
const QString kCurrentDeviceMarker = QStringLiteral("● 当前连接");

QString sessionStateText(rv1126b::DeviceSessionState state)
{
    using rv1126b::DeviceSessionState;
    switch (state) {
    case DeviceSessionState::Connecting:
        return QStringLiteral("连接中");
    case DeviceSessionState::Online:
        return QStringLiteral("在线");
    case DeviceSessionState::Degraded:
        return QStringLiteral("连接不稳定");
    case DeviceSessionState::AuthenticationFailed:
        return QStringLiteral("验证码错误");
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
    resize(1020, 600);

    auto* root = new QVBoxLayout(this);
    auto* helpLabel = new QLabel(
        QStringLiteral("直接点击列表中的设备即可选中（整行变蓝）。"
                       "「%1」是设备侧预置的访问口令，只在本机保存（Windows 凭据管理器），"
                       "不会写进配置文件，也不会出现在日志里。")
            .arg(kVerificationCodeLabel),
        this);
    helpLabel->setWordWrap(true);
    root->addWidget(helpLabel);

    deviceTable_ = new QTableWidget(0, 9, this);
    deviceTable_->setObjectName(QStringLiteral("discoveredDeviceTable"));
    deviceTable_->setHorizontalHeaderLabels({
        QStringLiteral("设备 ID"),
        QStringLiteral("当前"),
        QStringLiteral("型号"),
        QStringLiteral("IP 地址"),
        QStringLiteral("版本"),
        QStringLiteral("验证码"),
        QStringLiteral("连接状态"),
        QStringLiteral("来源"),
        QStringLiteral("最近在线"),
    });
    deviceTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    deviceTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    deviceTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    deviceTable_->setFocusPolicy(Qt::StrongFocus);
    deviceTable_->setAlternatingRowColors(false);
    deviceTable_->verticalHeader()->setVisible(false);
    deviceTable_->verticalHeader()->setDefaultSectionSize(26);
    deviceTable_->horizontalHeader()->setStretchLastSection(true);
    deviceTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    // 选中行用明显的蓝色整行高亮，"当前连接设备"再叠一层浅绿底色 + ● 标记，
    // 多台设备同时在线时也能一眼看出正在连哪一台。
    deviceTable_->setStyleSheet(QStringLiteral(
        "QTableWidget::item:selected { background: #1a73e8; color: #ffffff; }"
        "QTableWidget::item:selected:active { background: #1a73e8; color: #ffffff; }"
        "QTableWidget::item:selected:!active { background: #1a73e8; color: #ffffff; }"));
    root->addWidget(deviceTable_, 1);

    auto* form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    auto* tokenRow = new QWidget(this);
    auto* tokenRowLayout = new QHBoxLayout(tokenRow);
    tokenRowLayout->setContentsMargins(0, 0, 0, 0);
    tokenEdit_ = new QLineEdit(tokenRow);
    tokenEdit_->setObjectName(QStringLiteral("bearerTokenEdit"));
    tokenEdit_->setEchoMode(QLineEdit::Password);
    tokenEdit_->setClearButtonEnabled(true);
    tokenEdit_->setPlaceholderText(QStringLiteral("留空则使用本机已保存的验证码"));
    tokenRowLayout->addWidget(tokenEdit_, 1);
    tokenHint_ = new QLabel(QString(), tokenRow);
    tokenHint_->setObjectName(QStringLiteral("tokenHintLabel"));
    tokenHint_->setStyleSheet(QStringLiteral("color: #5f6368;"));
    tokenRowLayout->addWidget(tokenHint_);
    form->addRow(kVerificationCodeLabel, tokenRow);
    root->addLayout(form);

    auto* manualGroup = new QGroupBox(QStringLiteral("搜索不到设备时，手工填写 IP 连接"), this);
    auto* manualLayout = new QHBoxLayout(manualGroup);
    manualIpEdit_ = new QLineEdit(manualGroup);
    manualIpEdit_->setObjectName(QStringLiteral("manualDeviceIpEdit"));
    manualIpEdit_->setPlaceholderText(QStringLiteral("例如 192.168.1.120"));
    manualPortSpin_ = new QSpinBox(manualGroup);
    manualPortSpin_->setObjectName(QStringLiteral("manualDevicePortSpin"));
    manualPortSpin_->setRange(1, 65535);
    manualPortSpin_->setValue(18080);
    auto* manualButton = new QPushButton(QStringLiteral("验证并连接"), manualGroup);
    manualButton->setObjectName(QStringLiteral("connectManualDeviceButton"));
    manualButton->setEnabled(controller_ && controller_->manualProbeAvailable());
    manualLayout->addWidget(new QLabel(QStringLiteral("IP 地址"), manualGroup));
    manualLayout->addWidget(manualIpEdit_, 1);
    manualLayout->addWidget(new QLabel(QStringLiteral("端口"), manualGroup));
    manualLayout->addWidget(manualPortSpin_);
    manualLayout->addWidget(manualButton);
    root->addWidget(manualGroup);

    messageLabel_ = new QLabel(this);
    messageLabel_->setObjectName(QStringLiteral("discoveryMessageLabel"));
    messageLabel_->setWordWrap(true);
    root->addWidget(messageLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    searchButton_ = buttons->addButton(QStringLiteral("重新搜索"), QDialogButtonBox::ActionRole);
    searchButton_->setObjectName(QStringLiteral("searchDevicesButton"));
    connectButton_ = buttons->addButton(QStringLiteral("连接选中设备"), QDialogButtonBox::AcceptRole);
    connectButton_->setObjectName(QStringLiteral("connectDiscoveredDeviceButton"));
    connectButton_->setEnabled(false);
    connectButton_->setDefault(true);
    forgetButton_ = buttons->addButton(QStringLiteral("删除设备档案"), QDialogButtonBox::ActionRole);
    forgetButton_->setObjectName(QStringLiteral("forgetKnownDeviceButton"));
    forgetButton_->setEnabled(false);
    forgetButton_->setToolTip(QStringLiteral("删除本机保存的设备档案和验证码；本地历史事件与图片保留"));
    root->addWidget(buttons);

    connect(searchButton_, &QPushButton::clicked, this, &DeviceDiscoveryDialog::startScan);
    connect(connectButton_, &QPushButton::clicked, this, &DeviceDiscoveryDialog::connectSelectedDevice);
    connect(manualButton, &QPushButton::clicked, this, &DeviceDiscoveryDialog::connectManualEndpoint);
    connect(forgetButton_, &QPushButton::clicked, this, &DeviceDiscoveryDialog::forgetSelectedDevice);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(deviceTable_, &QTableWidget::itemSelectionChanged,
            this, &DeviceDiscoveryDialog::updateSelection);
    connect(deviceTable_, &QTableWidget::currentCellChanged, this, [this](int currentRow, int, int previousRow, int) {
        if (currentRow != previousRow && currentRow >= 0) {
            userPickedRow_ = true;
        }
        updateSelection();
    });
    connect(deviceTable_, &QTableWidget::cellDoubleClicked, this, [this](int, int) {
        if (connectButton_->isEnabled()) {
            connectSelectedDevice();
        }
    });

    if (controller_) {
        connect(controller_, &rv1126b::DeviceIntegrationController::discoveredDevicesReset,
                this, &DeviceDiscoveryDialog::resetDevices);
        connect(controller_, &rv1126b::DeviceIntegrationController::discoveredDeviceUpserted,
                this, &DeviceDiscoveryDialog::upsertDevice);
        connect(controller_, &rv1126b::DeviceIntegrationController::scanStateChanged,
                this, &DeviceDiscoveryDialog::updateScanState);
        connect(controller_, &rv1126b::DeviceIntegrationController::sessionChanged,
                this, &DeviceDiscoveryDialog::updateSession);
        connect(controller_, &rv1126b::DeviceIntegrationController::selectedVideoDeviceChanged,
                this, [this](const QString&) { refreshCurrentDeviceColumn(); });
        connect(controller_, &rv1126b::DeviceIntegrationController::userError,
                 this, &DeviceDiscoveryDialog::showControllerError);
        connect(controller_, &rv1126b::DeviceIntegrationController::deviceForgotten,
                this, [this](const QString& deviceId) {
                    const int row = rowForDevice(deviceId);
                    if (row >= 0) deviceTable_->removeRow(row);
                    refreshCurrentDeviceColumn();
                    updateSelection();
                    setMessage(QStringLiteral("设备档案和本机保存的验证码已删除；本地历史与图片已保留"));
                });

        for (const auto& snapshot : controller_->sessionSnapshots()) {
            upsertKnownDevice(snapshot);
        }
        for (const auto& device : controller_->discoveredDevices()) {
            upsertDevice(device);
        }
        refreshCurrentDeviceColumn();
    }

    const bool available = controller_ && controller_->networkServicesAvailable();
    searchButton_->setEnabled(available);
    if (!available) {
        setMessage(QStringLiteral("真实设备网络服务尚未装配，当前不能搜索或连接设备"), true);
    } else if (deviceTable_->rowCount() == 0) {
        QTimer::singleShot(0, this, &DeviceDiscoveryDialog::startScan);
    } else {
        selectInitialDevice();
        updateSelection();
        setMessage(QStringLiteral("点击一行选中设备，确认「连接选中设备」"));
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
    if (controller_->isScanning()) controller_->cancelScan();
    else controller_->startScan();
}

void DeviceDiscoveryDialog::connectSelectedDevice()
{
    const QString deviceId = selectedDeviceId();
    if (!controller_ || deviceId.isEmpty()) {
        setMessage(QStringLiteral("请先在列表中点击选择一台设备"), true);
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
    const bool discovered = controller_->discoveredDevice(deviceId).has_value();
    awaitingConnectDeviceId_ = deviceId;
    const bool started = discovered
        ? controller_->connectDiscoveredDevice(deviceId, rv1126b::SecretValue(std::move(tokenBytes)))
        : controller_->connectKnownDevice(deviceId, rv1126b::SecretValue(std::move(tokenBytes)));
    if (started) {
        connectButton_->setEnabled(false);
        setMessage(QStringLiteral("正在验证设备身份…"));
    } else {
        awaitingConnectDeviceId_.clear();
    }
}

void DeviceDiscoveryDialog::connectManualEndpoint()
{
    if (!controller_ || !controller_->manualProbeAvailable()) {
        setMessage(QStringLiteral("手工 IP 探测服务尚未装配"), true);
        return;
    }
    QString tokenText = tokenEdit_->text();
    QByteArray tokenBytes = tokenText.toUtf8();
    tokenEdit_->clear();
    tokenText.fill(QChar(u'\0'));
    tokenText.clear();
    awaitingConnectDeviceId_ = manualIpEdit_->text().trimmed();
    if (controller_->connectManualEndpoint(manualIpEdit_->text().trimmed(),
                                           static_cast<quint16>(manualPortSpin_->value()),
                                           rv1126b::SecretValue(std::move(tokenBytes)))) {
        setMessage(QStringLiteral("正在通过设备健康接口验证 IP 与验证码…"));
    } else {
        awaitingConnectDeviceId_.clear();
    }
}

void DeviceDiscoveryDialog::forgetSelectedDevice()
{
    const QString deviceId = selectedDeviceId();
    if (deviceId.isEmpty() || !controller_) return;
    if (QMessageBox::warning(
            this, QStringLiteral("删除设备档案"),
            QStringLiteral("将断开该设备，并删除本机保存的设备档案和验证码。"
                           "本机已同步的历史事件与图片会保留。是否继续？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
        controller_->forgetKnownDevice(deviceId);
    }
}

void DeviceDiscoveryDialog::updateSelection()
{
    const QString deviceId = selectedDeviceId();
    connectButton_->setEnabled(controller_
                               && controller_->networkServicesAvailable()
                               && !deviceId.isEmpty());
    const int row = deviceTable_->currentRow();
    const bool known = row >= 0 && deviceTable_->item(row, 0)
                       && deviceTable_->item(row, 0)->data(Qt::UserRole + 1).toBool();
    forgetButton_->setEnabled(known);
    if (tokenHint_) {
        if (controller_ && !deviceId.isEmpty() && controller_->hasCredentialForDevice(deviceId)) {
            tokenHint_->setText(QStringLiteral("本机已保存，可留空"));
        } else if (!deviceId.isEmpty()) {
            tokenHint_->setText(QStringLiteral("该设备需要验证码"));
        } else {
            tokenHint_->clear();
        }
    }
    if (controller_ && !deviceId.isEmpty() && controller_->hasCredentialForDevice(deviceId)) {
        tokenEdit_->setPlaceholderText(QStringLiteral("留空继续使用本机已保存的验证码"));
    } else {
        tokenEdit_->setPlaceholderText(QStringLiteral("请填写该设备的验证码"));
    }
}

void DeviceDiscoveryDialog::resetDevices()
{
    for (int row = deviceTable_->rowCount() - 1; row >= 0; --row) {
        QTableWidgetItem* item = deviceTable_->item(row, 0);
        if (item && !item->data(Qt::UserRole + 1).toBool())
            deviceTable_->removeRow(row);
    }
    refreshCurrentDeviceColumn();
    updateSelection();
}

int DeviceDiscoveryDialog::ensureRow(const QString& deviceId)
{
    int row = rowForDevice(deviceId);
    if (row >= 0) {
        return row;
    }
    row = deviceTable_->rowCount();
    deviceTable_->insertRow(row);
    for (int column = 0; column < deviceTable_->columnCount(); ++column) {
        deviceTable_->setItem(row, column, new QTableWidgetItem);
    }
    return row;
}

void DeviceDiscoveryDialog::upsertDevice(const rv1126b::DiscoveredDeviceDto& device)
{
    const int row = ensureRow(device.deviceId);

    deviceTable_->item(row, 0)->setText(device.deviceId);
    deviceTable_->item(row, 0)->setData(Qt::UserRole, device.deviceId);
    deviceTable_->item(row, 2)->setText(device.deviceModel);
    deviceTable_->item(row, 3)->setText(device.ipv4);
    deviceTable_->item(row, 4)->setText(device.releaseVersion);
    deviceTable_->item(row, 5)->setText(device.authRequired ? QStringLiteral("需要")
                                                            : QStringLiteral("不需要"));
    deviceTable_->item(row, 6)->setText(
        sessionStateText(sessionStates_.value(device.deviceId,
                                              rv1126b::DeviceSessionState::Disconnected)));
    const bool known = deviceTable_->item(row, 0)->data(Qt::UserRole + 1).toBool();
    deviceTable_->item(row, 7)->setText(known ? QStringLiteral("历史档案 + 搜索到")
                                              : QStringLiteral("搜索到"));
    selectInitialDevice();
    refreshCurrentDeviceColumn();
    updateSelection();
}

void DeviceDiscoveryDialog::upsertKnownDevice(const rv1126b::DeviceSessionSnapshot& snapshot)
{
    const auto& profile = snapshot.profile;
    const int row = ensureRow(profile.deviceId);

    deviceTable_->item(row, 0)->setText(profile.deviceId);
    deviceTable_->item(row, 0)->setData(Qt::UserRole, profile.deviceId);
    deviceTable_->item(row, 0)->setData(Qt::UserRole + 1, true);
    deviceTable_->item(row, 2)->setText(profile.deviceModel);
    deviceTable_->item(row, 3)->setText(profile.endpoint.ipv4);
    deviceTable_->item(row, 4)->setText(profile.releaseVersion);
    deviceTable_->item(row, 5)->setText(profile.credentialRef.isEmpty()
        ? QStringLiteral("需要") : QStringLiteral("已保存"));
    deviceTable_->item(row, 6)->setText(sessionStateText(snapshot.state));
    deviceTable_->item(row, 7)->setText(QStringLiteral("历史档案"));
    deviceTable_->item(row, 8)->setText(profile.lastOnlineEpochMs > 0
        ? QDateTime::fromMSecsSinceEpoch(profile.lastOnlineEpochMs).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
        : QStringLiteral("—"));
    sessionStates_.insert(profile.deviceId, snapshot.state);
    selectInitialDevice();
    refreshCurrentDeviceColumn();
    updateSelection();
}

void DeviceDiscoveryDialog::updateScanState(bool scanning)
{
    searchButton_->setEnabled(controller_ && controller_->networkServicesAvailable());
    searchButton_->setText(scanning ? QStringLiteral("取消搜索") : QStringLiteral("重新搜索"));
    if (scanning) {
        setMessage(QStringLiteral("正在搜索局域网内的设备…"));
    } else {
        if (deviceTable_->rowCount() == 0)
            setMessage(QStringLiteral("未发现设备。请检查网线、网卡和防火墙，也可以在下方手工填写 IP 连接。"), true);
        else
            setMessage(QStringLiteral("搜索完成，共 %1 台设备（含历史档案）").arg(deviceTable_->rowCount()));
        selectInitialDevice();
        refreshCurrentDeviceColumn();
        updateSelection();
    }
}

void DeviceDiscoveryDialog::updateSession(const rv1126b::DeviceSessionSnapshot& snapshot)
{
    const QString deviceId = snapshot.profile.deviceId;
    sessionStates_.insert(deviceId, snapshot.state);
    const int row = rowForDevice(deviceId);
    if (row >= 0) {
        deviceTable_->item(row, 6)->setText(sessionStateText(snapshot.state));
    }
    refreshCurrentDeviceColumn();

    // 只有用户主动点过"连接"才在成功后自动关窗。
    // 以前是"选中的设备一旦在线就关窗"，配合启动时自动连接历史设备，
    // 会让对话框刚打开就自己消失，用户根本来不及点选设备。
    const bool userInitiated = !awaitingConnectDeviceId_.isEmpty()
                               && (awaitingConnectDeviceId_ == deviceId
                                   || awaitingConnectDeviceId_ == snapshot.profile.endpoint.ipv4);
    if (!userInitiated) {
        if (controller_ && deviceId == selectedDeviceId() && snapshot.state == rv1126b::DeviceSessionState::Online) {
            setMessage(QStringLiteral("该设备已在线，可直接点「连接选中设备」切换当前预览"));
        }
        return;
    }

    if (snapshot.state == rv1126b::DeviceSessionState::Online) {
        awaitingConnectDeviceId_.clear();
        setMessage(QStringLiteral("设备身份验证成功，正在打开实时视频"));
        QTimer::singleShot(0, this, &QDialog::accept);
    } else if (snapshot.state == rv1126b::DeviceSessionState::AuthenticationFailed) {
        awaitingConnectDeviceId_.clear();
        connectButton_->setEnabled(true);
        tokenEdit_->setFocus();
        setMessage(QStringLiteral("验证码错误，请重新填写设备的验证码"), true);
    } else if (snapshot.state == rv1126b::DeviceSessionState::Degraded) {
        awaitingConnectDeviceId_.clear();
        connectButton_->setEnabled(true);
        setMessage(QStringLiteral("设备网络暂时不可用，请稍后重试"), true);
    }
}

void DeviceDiscoveryDialog::refreshCurrentDeviceColumn()
{
    const QString currentId = controller_ ? controller_->selectedVideoDeviceId() : QString();
    for (int row = 0; row < deviceTable_->rowCount(); ++row) {
        QTableWidgetItem* idItem = deviceTable_->item(row, 0);
        QTableWidgetItem* currentItem = deviceTable_->item(row, 1);
        if (!idItem || !currentItem) continue;
        const bool isCurrent = !currentId.isEmpty() && idItem->data(Qt::UserRole).toString() == currentId;
        currentItem->setText(isCurrent ? kCurrentDeviceMarker : QString());
        if (isCurrent) {
            currentItem->setForeground(QColor(0x0f, 0x7b, 0x33));
            currentItem->setToolTip(QStringLiteral("当前正在预览的设备"));
        } else {
            currentItem->setToolTip(QString());
        }
        for (int column = 0; column < deviceTable_->columnCount(); ++column) {
            if (QTableWidgetItem* item = deviceTable_->item(row, column)) {
                if (isCurrent) {
                    item->setBackground(QColor(0xe6, 0xf4, 0xea));
                } else {
                    item->setBackground(QBrush());
                }
                QFont font = item->font();
                font.setBold(isCurrent);
                item->setFont(font);
            }
        }
    }
}

void DeviceDiscoveryDialog::showControllerError(const QString&, const QString& message)
{
    awaitingConnectDeviceId_.clear();
    connectButton_->setEnabled(!selectedDeviceId().isEmpty()
                               && controller_
                               && controller_->networkServicesAvailable());
    setMessage(message, true);
}

int DeviceDiscoveryDialog::rowForDevice(const QString& deviceId) const
{
    for (int row = 0; row < deviceTable_->rowCount(); ++row) {
        const QTableWidgetItem* item = deviceTable_->item(row, 0);
        if (item && item->data(Qt::UserRole).toString() == deviceId) {
            return row;
        }
    }
    return -1;
}

QString DeviceDiscoveryDialog::selectedDeviceId() const
{
    const int row = deviceTable_->currentRow();
    if (row < 0) {
        return {};
    }
    const QTableWidgetItem* item = deviceTable_->item(row, 0);
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void DeviceDiscoveryDialog::selectInitialDevice()
{
    // 只在用户还没选过任何行时自动选一次。以前每次 upsert 都会重新调用，
    // 用户刚点中的行会被下一次设备更新覆盖掉，看起来就是"点了选不中"。
    if (userPickedRow_ || deviceTable_->currentRow() >= 0 || deviceTable_->rowCount() == 0) {
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
