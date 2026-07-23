#include "Rv1126bDeviceManagementDialog.h"

#include "../rv1126b/services/EmbeddedFtpReceiveServer.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHostAddress>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QTimeZone>
#include <QUuid>
#include <QVBoxLayout>

namespace {

QString timeQualityText(rv1126b::TimeQuality quality, const QString& raw)
{
    using rv1126b::TimeQuality;
    switch (quality) {
    case TimeQuality::NativeUtc: return QStringLiteral("UTC 已验证");
    case TimeQuality::ConfiguredOffset: return QStringLiteral("已应用板端偏移");
    case TimeQuality::BoardEpochUnverified: return QStringLiteral("板端时间未校验（不可作为可靠 UTC）");
    case TimeQuality::AppApiSetCurrentBoot: return QStringLiteral("应用本次启动已校时");
    case TimeQuality::RtcRestoredCurrentBoot: return QStringLiteral("RTC 本次启动已恢复");
    case TimeQuality::Unknown: return QStringLiteral("未知：%1").arg(raw);
    }
    return QStringLiteral("未知");
}

QString taskStateText(rv1126b::FtpTaskState state, const QString& raw = {})
{
    using rv1126b::FtpTaskState;
    switch (state) {
    case FtpTaskState::Queued: return QStringLiteral("等待执行");
    case FtpTaskState::Running: return QStringLiteral("执行中");
    case FtpTaskState::Done: return QStringLiteral("完成");
    case FtpTaskState::Failed: return QStringLiteral("失败");
    case FtpTaskState::Unknown: return QStringLiteral("未知%1").arg(raw.isEmpty() ? QString() : QStringLiteral("：%1").arg(raw));
    }
    return QStringLiteral("未知");
}

QTableWidgetItem* item(const QString& text)
{
    auto* value = new QTableWidgetItem(text);
    value->setFlags(value->flags() & ~Qt::ItemIsEditable);
    return value;
}

QString epochText(qint64 epochMs, Qt::TimeSpec spec)
{
    if (epochMs <= 0) return QStringLiteral("—");
    QDateTime value = QDateTime::fromMSecsSinceEpoch(epochMs, QTimeZone::UTC);
    if (spec == Qt::LocalTime) value = value.toLocalTime();
    return value.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz t"));
}

} // namespace

Rv1126bDeviceManagementDialog::Rv1126bDeviceManagementDialog(
    const QString& deviceId,
    rv1126b::DeviceOperationsController* controller,
    InitialPage initialPage,
    QWidget* parent,
    bool deviceOnline,
    rv1126b::EmbeddedFtpReceiveServer* ftpReceiveServer,
    const QString& localFtpRootPath)
    : QDialog(parent)
    , deviceId_(deviceId)
    , controller_(controller)
    , ftpReceiveServer_(ftpReceiveServer)
    , localFtpRootPath_(localFtpRootPath)
    , taskRefreshTimer_(new QTimer(this))
    , deviceOnline_(deviceOnline)
{
    setObjectName(QStringLiteral("rv1126bDeviceManagementDialog"));
    setWindowTitle(QStringLiteral("RV1126B 设备管理 · %1").arg(deviceId_));
    resize(1040, 720);
    setMinimumSize(900, 620);

    auto* layout = new QVBoxLayout(this);
    globalMessage_ = new QLabel(this);
    globalMessage_->setObjectName(QStringLiteral("deviceOperationsMessage"));
    globalMessage_->setWordWrap(true);
    layout->addWidget(globalMessage_);

    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("deviceOperationsTabs"));
    tabs_->addTab(createEvidencePage(), QStringLiteral("展示配置"));
    tabs_->addTab(createTimePage(), QStringLiteral("时间"));
    tabs_->addTab(createFtpConfigPage(), QStringLiteral("FTP 配置"));
    tabs_->addTab(createFtpTasksPage(), QStringLiteral("FTP 历史任务"));
    tabs_->setCurrentIndex(static_cast<int>(initialPage));
    layout->addWidget(tabs_, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    taskRefreshTimer_->setInterval(2000);
    connect(taskRefreshTimer_, &QTimer::timeout, this, &Rv1126bDeviceManagementDialog::refreshTasks);
    connect(tabs_, &QTabWidget::currentChanged, this, [this]() { updateTaskRefreshState(); });
    connectController();

    if (!controller_) {
        globalMessage_->setText(QStringLiteral("设备操作控制器尚未装配"));
        tabs_->setEnabled(false);
        return;
    }
    controller_->selectDevice(deviceId_);
    tabs_->setTabEnabled(0, deviceOnline_ && controller_->boardApiAvailable());
    tabs_->setTabEnabled(1, deviceOnline_ && controller_->boardApiAvailable());
    tabs_->setTabEnabled(2, deviceOnline_ && controller_->ftpServiceAvailable());
    tabs_->setTabEnabled(3, (deviceOnline_ && controller_->ftpServiceAvailable())
                                || controller_->ftpTaskSnapshotAvailable());
    createTaskButton_->setEnabled(deviceOnline_ && controller_->ftpServiceAvailable());
    if (deviceOnline_) {
        controller_->loadAll();
    } else {
        globalMessage_->setText(QStringLiteral("设备离线：显示本地 FTP 任务快照；创建、重试和自动刷新已禁用"));
        controller_->loadLocalFtpTaskSnapshots();
    }
    updateTaskRefreshState();
}

Rv1126bDeviceManagementDialog::~Rv1126bDeviceManagementDialog()
{
    taskRefreshTimer_->stop();
    if (controller_) controller_->cancelPending();
}

void Rv1126bDeviceManagementDialog::reject()
{
    taskRefreshTimer_->stop();
    if (controller_) controller_->cancelPending();
    QDialog::reject();
}

QWidget* Rv1126bDeviceManagementDialog::createEvidencePage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    auto* form = new QFormLayout;
    siteNameEdit_ = new QLineEdit(page);
    roadDirectionEdit_ = new QLineEdit(page);
    speedLimitSpin_ = new QSpinBox(page);
    speedLimitSpin_->setRange(0, 300);
    speedLimitSpin_->setSuffix(QStringLiteral(" km/h"));
    statusTextEdit_ = new QLineEdit(page);
    codeTextEdit_ = new QLineEdit(page);
    siteNameEdit_->setObjectName(QStringLiteral("siteNameEdit"));
    form->addRow(QStringLiteral("点位名称（最多 128 UTF-8 字节）"), siteNameEdit_);
    form->addRow(QStringLiteral("道路方向（最多 64 UTF-8 字节）"), roadDirectionEdit_);
    form->addRow(QStringLiteral("限速"), speedLimitSpin_);
    form->addRow(QStringLiteral("状态文字（最多 64 UTF-8 字节）"), statusTextEdit_);
    form->addRow(QStringLiteral("代码文字（最多 128 UTF-8 字节）"), codeTextEdit_);
    layout->addLayout(form);
    evidenceStatus_ = new QLabel(QStringLiteral("正在读取…"), page);
    evidenceStatus_->setObjectName(QStringLiteral("evidenceStatusLabel"));
    evidenceStatus_->setWordWrap(true);
    layout->addWidget(evidenceStatus_);
    auto* row = new QHBoxLayout;
    auto* reload = new QPushButton(QStringLiteral("重新读取"), page);
    auto* save = new QPushButton(QStringLiteral("保存展示配置"), page);
    save->setObjectName(QStringLiteral("saveEvidenceButton"));
    connect(reload, &QPushButton::clicked, controller_, [this]() { if (controller_) controller_->loadEvidenceConfig(); });
    connect(save, &QPushButton::clicked, this, [this]() {
        rv1126b::EvidenceConfigUpdate update;
        update.siteName = siteNameEdit_->text();
        update.roadDirection = roadDirectionEdit_->text();
        update.speedLimitKmh = speedLimitSpin_->value();
        update.statusText = statusTextEdit_->text();
        update.codeText = codeTextEdit_->text();
        controller_->saveEvidenceConfig(update);
    });
    row->addWidget(reload);
    row->addWidget(save);
    row->addStretch();
    layout->addLayout(row);
    layout->addStretch();
    return page;
}

QWidget* Rv1126bDeviceManagementDialog::createTimePage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    auto* form = new QFormLayout;
    utcTimeLabel_ = new QLabel(page);
    localTimeLabel_ = new QLabel(page);
    sourceEpochLabel_ = new QLabel(page);
    offsetLabel_ = new QLabel(page);
    timeQualityLabel_ = new QLabel(page);
    timeQualityLabel_->setObjectName(QStringLiteral("timeQualityLabel"));
    ntpStatusLabel_ = new QLabel(page);
    timeWriteLabel_ = new QLabel(page);
    form->addRow(QStringLiteral("归一化 UTC"), utcTimeLabel_);
    form->addRow(QStringLiteral("本地显示"), localTimeLabel_);
    form->addRow(QStringLiteral("source epoch ms"), sourceEpochLabel_);
    form->addRow(QStringLiteral("offset applied ms"), offsetLabel_);
    form->addRow(QStringLiteral("时间质量"), timeQualityLabel_);
    form->addRow(QStringLiteral("NTP 状态"), ntpStatusLabel_);
    form->addRow(QStringLiteral("校时能力"), timeWriteLabel_);
    layout->addLayout(form);
    auto* warning = new QLabel(QStringLiteral("协议输入始终是 UTC epoch 毫秒；客户端不会固定加减 8 小时。"), page);
    warning->setWordWrap(true);
    layout->addWidget(warning);
    auto* row = new QHBoxLayout;
    auto* reload = new QPushButton(QStringLiteral("重新读取"), page);
    syncTimeButton_ = new QPushButton(QStringLiteral("使用当前 PC UTC 校时"), page);
    syncTimeButton_->setObjectName(QStringLiteral("syncUtcButton"));
    syncTimeButton_->setEnabled(false);
    connect(reload, &QPushButton::clicked, controller_, [this]() { if (controller_) controller_->loadTime(); });
    connect(syncTimeButton_, &QPushButton::clicked, this, [this]() {
        const qint64 utcEpochMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
        const QString text = epochText(utcEpochMs, Qt::UTC);
        if (QMessageBox::question(this, QStringLiteral("设备校时"),
                                  QStringLiteral("确认将设备归一化时间设置为：\n%1？").arg(text))
            == QMessageBox::Yes)
            controller_->setTimeUtc(utcEpochMs);
    });
    row->addWidget(reload);
    row->addWidget(syncTimeButton_);
    row->addStretch();
    layout->addLayout(row);
    layout->addStretch();
    return page;
}

QWidget* Rv1126bDeviceManagementDialog::createFtpConfigPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    ftpRevisionLabel_ = new QLabel(QStringLiteral("revision：未读取"), page);
    ftpRevisionLabel_->setObjectName(QStringLiteral("ftpRevisionLabel"));
    layout->addWidget(ftpRevisionLabel_);

    auto* receiver = new QGroupBox(QStringLiteral("本机 FTP 接收服务"), page);
    receiver->setObjectName(QStringLiteral("localFtpReceiverGroup"));
    auto* receiverLayout = new QGridLayout(receiver);
    localFtpRootEdit_ = new QLineEdit(receiver);
    if (localFtpRootPath_.trimmed().isEmpty()) {
        const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        localFtpRootEdit_->setText(QDir(documents.isEmpty() ? QDir::homePath() : documents)
                                       .filePath(QStringLiteral("RV1126B_Events")));
    } else {
        localFtpRootEdit_->setText(localFtpRootPath_);
    }
    localFtpRootEdit_->setObjectName(QStringLiteral("localFtpRootEdit"));
    auto* browse = new QPushButton(QStringLiteral("选择"), receiver);
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择接收目录"), localFtpRootEdit_->text());
        if (!path.isEmpty()) localFtpRootEdit_->setText(path);
    });
    localFtpHostEdit_ = new QLineEdit(defaultLocalFtpAddress(), receiver);
    localFtpHostEdit_->setObjectName(QStringLiteral("localFtpHostEdit"));
    localFtpTargetIdEdit_ = new QLineEdit(defaultLocalFtpTargetId(localFtpHostEdit_->text()), receiver);
    localFtpTargetIdEdit_->setObjectName(QStringLiteral("localFtpTargetIdEdit"));
    connect(localFtpHostEdit_, &QLineEdit::textChanged,
            this, &Rv1126bDeviceManagementDialog::syncLocalFtpTargetIdFromHost);
    connect(localFtpTargetIdEdit_, &QLineEdit::textEdited, this, [this]() {
        localFtpTargetIdAuto_ = false;
    });
    localFtpPortSpin_ = new QSpinBox(receiver);
    localFtpPortSpin_->setRange(1, 65535);
    localFtpPortSpin_->setValue(21210);
    localFtpPortSpin_->setObjectName(QStringLiteral("localFtpPortSpin"));
    localFtpPassiveStartSpin_ = new QSpinBox(receiver);
    localFtpPassiveStartSpin_->setRange(0, 65535);
    localFtpPassiveStartSpin_->setValue(21211);
    localFtpPassiveStartSpin_->setObjectName(QStringLiteral("localFtpPassiveStartSpin"));
    localFtpPassiveEndSpin_ = new QSpinBox(receiver);
    localFtpPassiveEndSpin_->setRange(0, 65535);
    localFtpPassiveEndSpin_->setValue(21230);
    localFtpPassiveEndSpin_->setObjectName(QStringLiteral("localFtpPassiveEndSpin"));
    localFtpUserEdit_ = new QLineEdit(QStringLiteral("upload"), receiver);
    localFtpUserEdit_->setObjectName(QStringLiteral("localFtpUserEdit"));
    localFtpPasswordEdit_ = new QLineEdit(QUuid::createUuid().toString(QUuid::WithoutBraces).remove(QLatin1Char('-')).left(16), receiver);
    localFtpPasswordEdit_->setObjectName(QStringLiteral("localFtpPasswordEdit"));
    localFtpPasswordEdit_->setEchoMode(QLineEdit::Password);
    localFtpStartButton_ = new QPushButton(QStringLiteral("启动接收服务"), receiver);
    localFtpStartButton_->setObjectName(QStringLiteral("localFtpStartButton"));
    localFtpStopButton_ = new QPushButton(QStringLiteral("停止"), receiver);
    localFtpStopButton_->setObjectName(QStringLiteral("localFtpStopButton"));
    auto* fillTarget = new QPushButton(QStringLiteral("填入本机目标"), receiver);
    fillTarget->setObjectName(QStringLiteral("localFtpFillTargetButton"));
    auto* saveTarget = new QPushButton(QStringLiteral("写入板端并启用新事件"), receiver);
    saveTarget->setObjectName(QStringLiteral("localFtpSaveTargetButton"));
    localFtpStatus_ = new QLabel(receiver);
    localFtpStatus_->setObjectName(QStringLiteral("localFtpStatusLabel"));
    localFtpStatus_->setWordWrap(true);
    connect(localFtpStartButton_, &QPushButton::clicked, this, &Rv1126bDeviceManagementDialog::startLocalFtpReceiver);
    connect(localFtpStopButton_, &QPushButton::clicked, this, &Rv1126bDeviceManagementDialog::stopLocalFtpReceiver);
    connect(fillTarget, &QPushButton::clicked, this, [this]() { applyLocalFtpTarget(false); });
    connect(saveTarget, &QPushButton::clicked, this, [this]() { applyLocalFtpTarget(true); });
    receiverLayout->addWidget(new QLabel(QStringLiteral("保存目录"), receiver), 0, 0);
    receiverLayout->addWidget(localFtpRootEdit_, 0, 1, 1, 4);
    receiverLayout->addWidget(browse, 0, 5);
    receiverLayout->addWidget(new QLabel(QStringLiteral("本机 IP"), receiver), 1, 0);
    receiverLayout->addWidget(localFtpHostEdit_, 1, 1);
    receiverLayout->addWidget(new QLabel(QStringLiteral("端口"), receiver), 1, 2);
    receiverLayout->addWidget(localFtpPortSpin_, 1, 3);
    receiverLayout->addWidget(new QLabel(QStringLiteral("被动端口"), receiver), 1, 4);
    auto* passiveRow = new QWidget(receiver);
    auto* passiveLayout = new QHBoxLayout(passiveRow);
    passiveLayout->setContentsMargins(0, 0, 0, 0);
    passiveLayout->addWidget(localFtpPassiveStartSpin_);
    passiveLayout->addWidget(new QLabel(QStringLiteral("-"), passiveRow));
    passiveLayout->addWidget(localFtpPassiveEndSpin_);
    receiverLayout->addWidget(passiveRow, 1, 5);
    receiverLayout->addWidget(new QLabel(QStringLiteral("用户"), receiver), 2, 0);
    receiverLayout->addWidget(localFtpUserEdit_, 2, 1);
    receiverLayout->addWidget(new QLabel(QStringLiteral("密码"), receiver), 2, 2);
    receiverLayout->addWidget(localFtpPasswordEdit_, 2, 3);
    receiverLayout->addWidget(localFtpStartButton_, 2, 4);
    receiverLayout->addWidget(localFtpStopButton_, 2, 5);
    receiverLayout->addWidget(new QLabel(QStringLiteral("目标 ID"), receiver), 3, 0);
    receiverLayout->addWidget(localFtpTargetIdEdit_, 3, 1);
    receiverLayout->addWidget(fillTarget, 4, 0);
    receiverLayout->addWidget(saveTarget, 4, 1);
    receiverLayout->addWidget(localFtpStatus_, 4, 2, 1, 4);
    layout->addWidget(receiver);
    updateLocalFtpReceiverState();

    auto* globals = new QGroupBox(QStringLiteral("全局参数"), page);
    auto* grid = new QGridLayout(globals);
    const auto spin = [globals](int max = 86400) {
        auto* value = new QSpinBox(globals);
        value->setRange(0, max);
        return value;
    };
    retryMaxSpin_ = spin(1000);
    retryIntervalSpin_ = spin();
    connectTimeoutSpin_ = spin();
    transferTimeoutSpin_ = spin();
    scanIntervalSpin_ = spin();
    grid->addWidget(new QLabel(QStringLiteral("最大重试"), globals), 0, 0);
    grid->addWidget(retryMaxSpin_, 0, 1);
    grid->addWidget(new QLabel(QStringLiteral("重试间隔(s)"), globals), 0, 2);
    grid->addWidget(retryIntervalSpin_, 0, 3);
    grid->addWidget(new QLabel(QStringLiteral("连接超时(s)"), globals), 1, 0);
    grid->addWidget(connectTimeoutSpin_, 1, 1);
    grid->addWidget(new QLabel(QStringLiteral("传输超时(s)"), globals), 1, 2);
    grid->addWidget(transferTimeoutSpin_, 1, 3);
    grid->addWidget(new QLabel(QStringLiteral("扫描间隔(s)"), globals), 2, 0);
    grid->addWidget(scanIntervalSpin_, 2, 1);
    layout->addWidget(globals);

    ftpTargetsTable_ = new QTableWidget(0, 10, page);
    ftpTargetsTable_->setObjectName(QStringLiteral("ftpTargetsTable"));
    ftpTargetsTable_->setHorizontalHeaderLabels({QStringLiteral("启用"), QStringLiteral("ID"),
        QStringLiteral("IPv4"), QStringLiteral("端口"), QStringLiteral("用户"),
        QStringLiteral("密码处理"), QStringLiteral("新密码"), QStringLiteral("远端目录"),
        QStringLiteral("被动"), QStringLiteral("已有密码")});
    ftpTargetsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ftpTargetsTable_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(ftpTargetsTable_, 1);

    auto* targetButtons = new QHBoxLayout;
    auto* add = new QPushButton(QStringLiteral("添加目标"), page);
    add->setObjectName(QStringLiteral("addFtpTargetButton"));
    auto* remove = new QPushButton(QStringLiteral("删除目标"), page);
    connect(add, &QPushButton::clicked, this, [this]() {
        if (ftpTargetsTable_->rowCount() >= rv1126b::DeviceOperationsController::MaxFtpTargets) {
            showError(QStringLiteral("too_many_ftp_targets"), QStringLiteral("FTP 目标最多 8 个"));
            return;
        }
        addFtpTargetRow();
    });
    connect(remove, &QPushButton::clicked, this, [this]() {
        const int row = ftpTargetsTable_->currentRow();
        if (row >= 0) ftpTargetsTable_->removeRow(row);
    });
    targetButtons->addWidget(add);
    targetButtons->addWidget(remove);
    targetButtons->addStretch();
    layout->addLayout(targetButtons);

    auto* control = new QGroupBox(QStringLiteral("自动下发控制"), page);
    auto* controlLayout = new QHBoxLayout(control);
    autoEnabledCheck_ = new QCheckBox(QStringLiteral("启用"), control);
    autoScopeCombo_ = new QComboBox(control);
    autoScopeCombo_->addItem(QStringLiteral("仅新事件"), static_cast<int>(rv1126b::FtpControlScope::NewEventsOnly));
    autoScopeCombo_->addItem(QStringLiteral("包含已有事件"), static_cast<int>(rv1126b::FtpControlScope::AllExisting));
    auto* applyControl = new QPushButton(QStringLiteral("应用控制"), control);
    connect(applyControl, &QPushButton::clicked, this, [this]() {
        const auto scope = static_cast<rv1126b::FtpControlScope>(autoScopeCombo_->currentData().toInt());
        controller_->updateFtpControl(autoEnabledCheck_->isChecked(), scope);
    });
    controlLayout->addWidget(autoEnabledCheck_);
    controlLayout->addWidget(autoScopeCombo_);
    controlLayout->addWidget(applyControl);
    controlLayout->addStretch();
    layout->addWidget(control);

    ftpStatus_ = new QLabel(page);
    ftpStatus_->setObjectName(QStringLiteral("ftpStatusLabel"));
    ftpStatus_->setWordWrap(true);
    layout->addWidget(ftpStatus_);
    auto* actions = new QHBoxLayout;
    auto* reload = new QPushButton(QStringLiteral("重新读取"), page);
    saveFtpButton_ = new QPushButton(QStringLiteral("保存并启用新事件自动下发"), page);
    saveFtpButton_->setObjectName(QStringLiteral("saveAndEnableFtpButton"));
    auto* rollback = new QPushButton(QStringLiteral("回滚最近配置"), page);
    connect(reload, &QPushButton::clicked, controller_, [this]() {
        if (controller_) { controller_->loadFtpConfig(); controller_->loadFtpControl(); }
    });
    connect(saveFtpButton_, &QPushButton::clicked, this, [this]() {
        const rv1126b::FtpConfigUpdate update = collectFtpConfig();
        controller_->saveFtpConfigAndEnableNewEvents(update);
    });
    connect(rollback, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, QStringLiteral("回滚 FTP 配置"),
                                  QStringLiteral("确认恢复板端最近一次 FTP 配置备份？")) == QMessageBox::Yes)
            controller_->rollbackFtpConfig();
    });
    actions->addWidget(reload);
    actions->addWidget(saveFtpButton_);
    actions->addWidget(rollback);
    actions->addStretch();
    layout->addLayout(actions);
    return page;
}

QWidget* Rv1126bDeviceManagementDialog::createFtpTasksPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    auto* createGroup = new QGroupBox(QStringLiteral("创建 UTC [start,end) 历史任务"), page);
    auto* createLayout = new QGridLayout(createGroup);
    taskStartEdit_ = new QDateTimeEdit(QDateTime::currentDateTime().addDays(-1), createGroup);
    taskEndEdit_ = new QDateTimeEdit(QDateTime::currentDateTime(), createGroup);
    taskStartEdit_->setCalendarPopup(true);
    taskEndEdit_->setCalendarPopup(true);
    taskStartEdit_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    taskEndEdit_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    taskTargets_ = new QListWidget(createGroup);
    taskTargets_->setObjectName(QStringLiteral("ftpTaskTargets"));
    taskTargets_->setMaximumHeight(90);
    createTaskButton_ = new QPushButton(QStringLiteral("创建历史任务"), createGroup);
    createTaskButton_->setObjectName(QStringLiteral("createFtpTaskButton"));
    connect(createTaskButton_, &QPushButton::clicked, this, &Rv1126bDeviceManagementDialog::createTask);
    createLayout->addWidget(new QLabel(QStringLiteral("本地开始时间"), createGroup), 0, 0);
    createLayout->addWidget(taskStartEdit_, 0, 1);
    createLayout->addWidget(new QLabel(QStringLiteral("本地结束时间（不含）"), createGroup), 0, 2);
    createLayout->addWidget(taskEndEdit_, 0, 3);
    createLayout->addWidget(new QLabel(QStringLiteral("已启用目标"), createGroup), 1, 0);
    createLayout->addWidget(taskTargets_, 1, 1, 1, 3);
    createLayout->addWidget(createTaskButton_, 2, 0);
    layout->addWidget(createGroup);

    taskTable_ = new QTableWidget(0, 5, page);
    taskTable_->setObjectName(QStringLiteral("ftpTaskTable"));
    taskTable_->setHorizontalHeaderLabels({QStringLiteral("任务 ID"), QStringLiteral("状态"),
        QStringLiteral("UTC 范围"), QStringLiteral("创建时间"), QStringLiteral("目标")});
    taskTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    taskTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    taskTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    taskTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    taskTable_->horizontalHeader()->setStretchLastSection(true);
    connect(taskTable_, &QTableWidget::itemSelectionChanged, this, [this]() {
        const int row = taskTable_->currentRow();
        if (row < 0 || !taskTable_->item(row, 0)) return;
        selectedTaskId_ = taskTable_->item(row, 0)->data(Qt::UserRole).toString();
        selectedTaskState_ = static_cast<rv1126b::FtpTaskState>(taskTable_->item(row, 1)->data(Qt::UserRole).toInt());
        retryTaskButton_->setEnabled(selectedTaskState_ == rv1126b::FtpTaskState::Failed);
        if (deviceOnline_) {
            controller_->loadFtpTask(selectedTaskId_);
        } else {
            for (const auto& task : localTaskSnapshots_) {
                if (task.taskId == selectedTaskId_) {
                    applyLocalTaskDetail(task);
                    break;
                }
            }
        }
    });
    layout->addWidget(taskTable_, 1);

    auto* paging = new QHBoxLayout;
    previousTasksButton_ = new QPushButton(QStringLiteral("上一页"), page);
    nextTasksButton_ = new QPushButton(QStringLiteral("下一页"), page);
    taskPageLabel_ = new QLabel(QStringLiteral("第 1 页"), page);
    previousTasksButton_->setEnabled(false);
    nextTasksButton_->setEnabled(false);
    connect(previousTasksButton_, &QPushButton::clicked, this, [this]() {
        if (taskPageIndex_ <= 0) return;
        --taskPageIndex_;
        controller_->listFtpTasks(taskPageCursors_.at(taskPageIndex_).isEmpty()
                                      ? std::nullopt
                                      : std::optional<QString>(taskPageCursors_.at(taskPageIndex_)));
    });
    connect(nextTasksButton_, &QPushButton::clicked, this, [this]() {
        if (!nextTaskCursor_) return;
        ++taskPageIndex_;
        if (taskPageCursors_.size() <= taskPageIndex_) taskPageCursors_.append(*nextTaskCursor_);
        else taskPageCursors_[taskPageIndex_] = *nextTaskCursor_;
        controller_->listFtpTasks(*nextTaskCursor_);
    });
    paging->addWidget(previousTasksButton_);
    paging->addWidget(nextTasksButton_);
    paging->addWidget(taskPageLabel_);
    paging->addStretch();
    layout->addLayout(paging);

    taskDetailTable_ = new QTableWidget(0, 9, page);
    taskDetailTable_->setObjectName(QStringLiteral("ftpTaskDetailTable"));
    taskDetailTable_->setHorizontalHeaderLabels({QStringLiteral("目标"), QStringLiteral("状态"),
        QStringLiteral("总数"), QStringLiteral("等待"), QStringLiteral("上传中"),
        QStringLiteral("成功"), QStringLiteral("失败"), QStringLiteral("尝试"), QStringLiteral("最后错误")});
    taskDetailTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    taskDetailTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    taskDetailTable_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(taskDetailTable_);
    retryTaskButton_ = new QPushButton(QStringLiteral("重试失败任务"), page);
    retryTaskButton_->setObjectName(QStringLiteral("retryFtpTaskButton"));
    retryTaskButton_->setEnabled(false);
    connect(retryTaskButton_, &QPushButton::clicked, this, [this]() {
        if (!selectedTaskId_.isEmpty() && selectedTaskState_ == rv1126b::FtpTaskState::Failed)
            controller_->retryFtpTask(selectedTaskId_);
    });
    layout->addWidget(retryTaskButton_, 0, Qt::AlignLeft);
    return page;
}

void Rv1126bDeviceManagementDialog::connectController()
{
    if (!controller_) return;
    connect(controller_, &rv1126b::DeviceOperationsController::userError,
            this, &Rv1126bDeviceManagementDialog::showError);
    connect(controller_, &rv1126b::DeviceOperationsController::evidenceConfigLoaded,
            this, &Rv1126bDeviceManagementDialog::applyEvidence);
    connect(controller_, &rv1126b::DeviceOperationsController::evidenceConfigSaved,
            this, [this](const rv1126b::EvidenceConfigDto& config) {
                applyEvidence(config);
                evidenceStatus_->setText(config.restartRequired
                    ? QStringLiteral("保存成功；需重启完整 pipeline 后仅对新 OCR 任务生效，已有事件不变")
                    : QStringLiteral("保存成功；配置已生效，已有事件不变"));
            });
    connect(controller_, &rv1126b::DeviceOperationsController::timeLoaded,
            this, &Rv1126bDeviceManagementDialog::applyTime);
    connect(controller_, &rv1126b::DeviceOperationsController::timeSaved,
            this, [this](const rv1126b::TimeStatusDto& time) {
                applyTime(time);
                globalMessage_->setText(QStringLiteral("设备校时成功"));
            });
    connect(controller_, &rv1126b::DeviceOperationsController::ftpConfigLoaded,
            this, &Rv1126bDeviceManagementDialog::applyFtpConfig);
    connect(controller_, &rv1126b::DeviceOperationsController::ftpConfigRolledBack,
            this, [this](const rv1126b::FtpConfigSnapshotDto&) { ftpStatus_->setText(QStringLiteral("FTP 配置已回滚")); });
    connect(controller_, &rv1126b::DeviceOperationsController::ftpActivationFinished,
            this, [this](const rv1126b::FtpActivationResult& result) {
                if (result.configSaved) clearPasswordEditors();
                if (!result.configSaved) ftpStatus_->setText(QStringLiteral("FTP 配置未保存"));
                else if (!result.autoEnabled) ftpStatus_->setText(QStringLiteral("FTP 配置已保存，但自动下发未开启；%1")
                    .arg(result.error ? result.error->code : QStringLiteral("请检查控制写能力")));
                else ftpStatus_->setText(QStringLiteral("FTP 配置已保存，并已启用 new_events_only 自动下发"));
                if (!result.newRevision.isEmpty()) {
                    ftpRevision_ = result.newRevision;
                    ftpRevisionLabel_->setText(QStringLiteral("revision：%1").arg(ftpRevision_));
                }
            });
    connect(controller_, &rv1126b::DeviceOperationsController::ftpControlLoaded,
            this, &Rv1126bDeviceManagementDialog::applyFtpControl);
    connect(controller_, &rv1126b::DeviceOperationsController::ftpControlSaved,
            this, [this](const rv1126b::FtpControlDto& control) {
                applyFtpControl(control);
                ftpStatus_->setText(control.enabled ? QStringLiteral("自动下发已启用")
                                                    : QStringLiteral("自动下发已暂停；手工任务不受影响"));
            });
    connect(controller_, &rv1126b::DeviceOperationsController::ftpRevisionConflict,
            this, &Rv1126bDeviceManagementDialog::showRevisionConflict);
    connect(controller_, &rv1126b::DeviceOperationsController::ftpTasksLoaded,
            this, &Rv1126bDeviceManagementDialog::applyTaskPage);
    connect(controller_, &rv1126b::DeviceOperationsController::ftpTaskLoaded,
            this, &Rv1126bDeviceManagementDialog::applyTaskDetail);
    connect(controller_, &rv1126b::DeviceOperationsController::ftpTaskCreated,
            this, [this](const rv1126b::FtpTaskDetailDto& task) {
                selectedTaskId_ = task.summary.taskId;
                applyTaskDetail(task);
                taskPageIndex_ = 0;
                taskPageCursors_ = {QString()};
                controller_->listFtpTasks();
            });
    connect(controller_, &rv1126b::DeviceOperationsController::ftpTaskRetried,
            this, [this](const rv1126b::FtpTaskDetailDto& task) {
                if (!task.targets.isEmpty()) applyTaskDetail(task);
                controller_->listFtpTasks(taskPageCursors_.at(taskPageIndex_).isEmpty()
                                              ? std::nullopt
                                              : std::optional<QString>(taskPageCursors_.at(taskPageIndex_)));
            });
    connect(controller_, &rv1126b::DeviceOperationsController::localFtpTaskSnapshotsLoaded,
            this, &Rv1126bDeviceManagementDialog::applyLocalTaskSnapshots);
    connect(controller_, &rv1126b::DeviceOperationsController::operationBusyChanged,
            this, [this](const QString& operation, bool busy) {
                if (operation == QStringLiteral("evidence.save")) {
                    if (auto* button = findChild<QPushButton*>(QStringLiteral("saveEvidenceButton")))
                        button->setEnabled(!busy);
                }
                if (operation == QStringLiteral("time.save"))
                    syncTimeButton_->setEnabled(!busy && timeSetEnabled_);
                if (operation == QStringLiteral("ftp.config.save")) {
                    if (busy) saveFtpButton_->setEnabled(false);
                    else validateFtpRowsInline();
                }
                if (operation == QStringLiteral("ftp.task.create")) createTaskButton_->setEnabled(!busy && deviceOnline_);
                if (operation == QStringLiteral("ftp.task.retry")) retryTaskButton_->setEnabled(!busy && deviceOnline_
                    && selectedTaskState_ == rv1126b::FtpTaskState::Failed);
                if (operation == QStringLiteral("ftp.tasks.list")) {
                    previousTasksButton_->setEnabled(!busy && taskPageIndex_ > 0);
                    nextTasksButton_->setEnabled(!busy && nextTaskCursor_.has_value());
                }
                if (busy) globalMessage_->setText(QStringLiteral("操作进行中：%1").arg(operation));
            });
}

void Rv1126bDeviceManagementDialog::showError(const QString& code, const QString& message)
{
    globalMessage_->setText(QStringLiteral("%1：%2").arg(code, message));
    if (code.startsWith(QStringLiteral("invalid_")) || code == QStringLiteral("too_many_ftp_targets"))
        globalMessage_->setStyleSheet(QStringLiteral("color: #b00020;"));
}

void Rv1126bDeviceManagementDialog::applyEvidence(const rv1126b::EvidenceConfigDto& config)
{
    siteNameEdit_->setText(config.evidence.siteName);
    roadDirectionEdit_->setText(config.evidence.roadDirection);
    speedLimitSpin_->setValue(config.evidence.speedLimitKmh);
    statusTextEdit_->setText(config.evidence.statusText);
    codeTextEdit_->setText(config.evidence.codeText);
    evidenceStatus_->setText(QStringLiteral("生效方式：%1；范围：%2；已有事件%3")
                                 .arg(config.applyMode, config.effectiveScope,
                                      config.existingEventsUnchanged ? QStringLiteral("不变") : QStringLiteral("状态未知")));
}

void Rv1126bDeviceManagementDialog::applyTime(const rv1126b::TimeStatusDto& time)
{
    utcTimeLabel_->setText(epochText(time.time.epochMs, Qt::UTC));
    localTimeLabel_->setText(epochText(time.time.epochMs, Qt::LocalTime));
    sourceEpochLabel_->setText(QString::number(time.time.sourceEpochMs));
    offsetLabel_->setText(QString::number(time.time.offsetAppliedMs));
    timeQualityLabel_->setText(timeQualityText(time.time.quality.value, time.time.quality.rawValue));
    const bool unreliable = time.time.quality.value == rv1126b::TimeQuality::BoardEpochUnverified
        || time.time.quality.value == rv1126b::TimeQuality::Unknown;
    timeQualityLabel_->setStyleSheet(unreliable ? QStringLiteral("color: #b00020; font-weight: bold;") : QString());
    ntpStatusLabel_->setText(time.ntpStatus);
    timeSetEnabled_ = time.timeSetEnabled;
    timeWriteLabel_->setText(timeSetEnabled_ ? QStringLiteral("已开放") : QStringLiteral("未开放"));
    syncTimeButton_->setEnabled(timeSetEnabled_);
}

void Rv1126bDeviceManagementDialog::applyFtpConfig(const rv1126b::FtpConfigSnapshotDto& config)
{
    ftpRevision_ = config.revision;
    ftpRevisionLabel_->setText(QStringLiteral("revision：%1").arg(ftpRevision_));
    retryMaxSpin_->setValue(config.retryMax);
    retryIntervalSpin_->setValue(config.retryIntervalSec);
    connectTimeoutSpin_->setValue(config.connectTimeoutSec);
    transferTimeoutSpin_->setValue(config.transferTimeoutSec);
    scanIntervalSpin_->setValue(config.scanIntervalSec);
    ftpTargetsTable_->setRowCount(0);
    taskTargets_->clear();
    for (const rv1126b::FtpTargetSnapshotDto& target : config.targets) {
        addFtpTargetRow(target);
        if (target.enabled) {
            auto* entry = new QListWidgetItem(target.id, taskTargets_);
            entry->setData(Qt::UserRole, target.id);
            entry->setFlags(entry->flags() | Qt::ItemIsUserCheckable);
            entry->setCheckState(Qt::Checked);
        }
    }
    ftpStatus_->setText(config.restartRequired ? QStringLiteral("配置需要重启后生效")
                                              : QStringLiteral("配置支持热加载，无需重启"));
}

void Rv1126bDeviceManagementDialog::addFtpTargetRow(
    const std::optional<rv1126b::FtpTargetSnapshotDto>& target)
{
    const int row = ftpTargetsTable_->rowCount();
    ftpTargetsTable_->insertRow(row);
    auto* enabled = new QCheckBox(ftpTargetsTable_);
    enabled->setChecked(target ? target->enabled : true);
    auto* id = new QLineEdit(target ? target->id : QStringLiteral("server%1").arg(row + 1), ftpTargetsTable_);
    auto* host = new QLineEdit(target ? target->host : QString(), ftpTargetsTable_);
    auto* port = new QSpinBox(ftpTargetsTable_);
    port->setRange(1, 65535);
    port->setValue(target ? target->port : 21);
    auto* user = new QLineEdit(target ? target->user : QString(), ftpTargetsTable_);
    auto* action = new QComboBox(ftpTargetsTable_);
    action->addItem(QStringLiteral("保留"), static_cast<int>(rv1126b::FtpPasswordAction::Keep));
    action->addItem(QStringLiteral("替换"), static_cast<int>(rv1126b::FtpPasswordAction::Replace));
    action->addItem(QStringLiteral("清除"), static_cast<int>(rv1126b::FtpPasswordAction::Clear));
    auto* password = new QLineEdit(ftpTargetsTable_);
    password->setEchoMode(QLineEdit::Password);
    password->setObjectName(QStringLiteral("ftpPasswordEdit"));
    password->setEnabled(false);
    connect(action, &QComboBox::currentIndexChanged, password, [this, action, password]() {
        const bool replace = action->currentData().toInt() == static_cast<int>(rv1126b::FtpPasswordAction::Replace);
        if (!replace) password->clear();
        password->setEnabled(replace);
        validateFtpRowsInline();
    });
    auto* remote = new QLineEdit(target ? target->remoteDir : QStringLiteral("/vehicle_events"), ftpTargetsTable_);
    auto* passive = new QCheckBox(ftpTargetsTable_);
    passive->setChecked(!target || target->passive);
    auto* configured = new QLabel(target && target->passwordConfigured ? QStringLiteral("是") : QStringLiteral("否"), ftpTargetsTable_);
    ftpTargetsTable_->setCellWidget(row, 0, enabled);
    ftpTargetsTable_->setCellWidget(row, 1, id);
    ftpTargetsTable_->setCellWidget(row, 2, host);
    ftpTargetsTable_->setCellWidget(row, 3, port);
    ftpTargetsTable_->setCellWidget(row, 4, user);
    ftpTargetsTable_->setCellWidget(row, 5, action);
    ftpTargetsTable_->setCellWidget(row, 6, password);
    ftpTargetsTable_->setCellWidget(row, 7, remote);
    ftpTargetsTable_->setCellWidget(row, 8, passive);
    ftpTargetsTable_->setCellWidget(row, 9, configured);
    for (QLineEdit* editor : {id, host, user, password, remote}) {
        connect(editor, &QLineEdit::textChanged, this,
                [this]() { validateFtpRowsInline(); });
    }
    validateFtpRowsInline();
}

bool Rv1126bDeviceManagementDialog::validateFtpRowsInline()
{
    if (!ftpTargetsTable_) return false;
    QHash<QString, int> idCounts;
    for (int row = 0; row < ftpTargetsTable_->rowCount(); ++row) {
        auto* id = qobject_cast<QLineEdit*>(ftpTargetsTable_->cellWidget(row, 1));
        if (id) ++idCounts[id->text().trimmed()];
    }
    bool valid = true;
    const auto mark = [&valid](QLineEdit* editor, bool rowValid, const QString& message) {
        if (!editor) return;
        editor->setStyleSheet(rowValid ? QString() : QStringLiteral("border: 1px solid #b00020;"));
        editor->setToolTip(rowValid ? QString() : message);
        valid = valid && rowValid;
    };
    for (int row = 0; row < ftpTargetsTable_->rowCount(); ++row) {
        auto* id = qobject_cast<QLineEdit*>(ftpTargetsTable_->cellWidget(row, 1));
        auto* host = qobject_cast<QLineEdit*>(ftpTargetsTable_->cellWidget(row, 2));
        auto* user = qobject_cast<QLineEdit*>(ftpTargetsTable_->cellWidget(row, 4));
        auto* action = qobject_cast<QComboBox*>(ftpTargetsTable_->cellWidget(row, 5));
        auto* password = qobject_cast<QLineEdit*>(ftpTargetsTable_->cellWidget(row, 6));
        auto* remote = qobject_cast<QLineEdit*>(ftpTargetsTable_->cellWidget(row, 7));
        const QString idValue = id ? id->text().trimmed() : QString();
        mark(id, !idValue.isEmpty() && idCounts.value(idValue) == 1,
             QStringLiteral("ID 不能为空且不能重复"));
        QHostAddress address;
        const bool ipv4 = host && address.setAddress(host->text().trimmed())
            && address.protocol() == QAbstractSocket::IPv4Protocol;
        mark(host, ipv4, QStringLiteral("请输入有效 IPv4 地址"));
        mark(user, user && !user->text().trimmed().isEmpty(), QStringLiteral("用户名不能为空"));
        mark(remote, remote && !remote->text().trimmed().isEmpty(), QStringLiteral("远端目录不能为空"));
        const bool replacementPresent = !action || action->currentData().toInt()
                != static_cast<int>(rv1126b::FtpPasswordAction::Replace)
            || (password && !password->text().isEmpty());
        mark(password, replacementPresent, QStringLiteral("选择替换密码后必须输入新密码"));
    }
    if (saveFtpButton_) saveFtpButton_->setEnabled(valid);
    return valid;
}

rv1126b::FtpConfigUpdate Rv1126bDeviceManagementDialog::collectFtpConfig() const
{
    rv1126b::FtpConfigUpdate update;
    update.expectedRevision = ftpRevision_;
    update.deviceId = deviceId_;
    update.retryMax = retryMaxSpin_->value();
    update.retryIntervalSec = retryIntervalSpin_->value();
    update.connectTimeoutSec = connectTimeoutSpin_->value();
    update.transferTimeoutSec = transferTimeoutSpin_->value();
    update.scanIntervalSec = scanIntervalSpin_->value();
    for (int row = 0; row < ftpTargetsTable_->rowCount(); ++row) {
        rv1126b::FtpTargetUpdate target;
        target.enabled = qobject_cast<QCheckBox*>(ftpTargetsTable_->cellWidget(row, 0))->isChecked();
        target.id = qobject_cast<QLineEdit*>(ftpTargetsTable_->cellWidget(row, 1))->text().trimmed();
        target.host = qobject_cast<QLineEdit*>(ftpTargetsTable_->cellWidget(row, 2))->text().trimmed();
        target.port = static_cast<quint16>(qobject_cast<QSpinBox*>(ftpTargetsTable_->cellWidget(row, 3))->value());
        target.user = qobject_cast<QLineEdit*>(ftpTargetsTable_->cellWidget(row, 4))->text().trimmed();
        auto* action = qobject_cast<QComboBox*>(ftpTargetsTable_->cellWidget(row, 5));
        target.passwordAction.value = static_cast<rv1126b::FtpPasswordAction>(action->currentData().toInt());
        auto* password = qobject_cast<QLineEdit*>(ftpTargetsTable_->cellWidget(row, 6));
        if (target.passwordAction.value == rv1126b::FtpPasswordAction::Replace)
            target.replacementPassword = password->text();
        target.remoteDir = qobject_cast<QLineEdit*>(ftpTargetsTable_->cellWidget(row, 7))->text().trimmed();
        target.passive = qobject_cast<QCheckBox*>(ftpTargetsTable_->cellWidget(row, 8))->isChecked();
        update.targets.append(target);
    }
    return update;
}

void Rv1126bDeviceManagementDialog::clearPasswordEditors()
{
    for (int row = 0; row < ftpTargetsTable_->rowCount(); ++row)
        if (auto* password = qobject_cast<QLineEdit*>(ftpTargetsTable_->cellWidget(row, 6))) password->clear();
}

void Rv1126bDeviceManagementDialog::applyFtpControl(const rv1126b::FtpControlDto& control)
{
    autoEnabledCheck_->setChecked(control.enabled);
    if (control.scope.value == rv1126b::FtpControlScope::AllExisting)
        autoScopeCombo_->setCurrentIndex(autoScopeCombo_->findData(static_cast<int>(rv1126b::FtpControlScope::AllExisting)));
    else
        autoScopeCombo_->setCurrentIndex(autoScopeCombo_->findData(static_cast<int>(rv1126b::FtpControlScope::NewEventsOnly)));
}

void Rv1126bDeviceManagementDialog::applyTaskPage(const rv1126b::FtpTaskPageDto& page,
                                                  const QString& requestedCursor)
{
    const QString currentCursor = taskPageCursors_.value(taskPageIndex_);
    if (currentCursor != requestedCursor) return;
    taskTable_->setRowCount(page.items.size());
    bool active = false;
    for (int row = 0; row < page.items.size(); ++row) {
        const auto& task = page.items.at(row);
        auto* id = item(task.taskId);
        id->setData(Qt::UserRole, task.taskId);
        auto* state = item(taskStateText(task.state.value, task.state.rawValue));
        state->setData(Qt::UserRole, static_cast<int>(task.state.value));
        taskTable_->setItem(row, 0, id);
        taskTable_->setItem(row, 1, state);
        taskTable_->setItem(row, 2, item(QStringLiteral("%1 — %2")
            .arg(epochText(task.startEpochMs, Qt::UTC), epochText(task.endEpochMs, Qt::UTC))));
        taskTable_->setItem(row, 3, item(epochText(task.createdEpochMs, Qt::LocalTime)));
        taskTable_->setItem(row, 4, item(task.targetIds.join(QStringLiteral(", "))));
        active = active || task.state.value == rv1126b::FtpTaskState::Queued
            || task.state.value == rv1126b::FtpTaskState::Running;
    }
    nextTaskCursor_ = page.hasMore ? page.nextCursor : std::nullopt;
    previousTasksButton_->setEnabled(taskPageIndex_ > 0);
    nextTasksButton_->setEnabled(nextTaskCursor_.has_value());
    taskPageLabel_->setText(QStringLiteral("第 %1 页").arg(taskPageIndex_ + 1));
    if (active && tabs_->currentIndex() == static_cast<int>(InitialPage::FtpTasks)) taskRefreshTimer_->start();
}

void Rv1126bDeviceManagementDialog::applyTaskDetail(const rv1126b::FtpTaskDetailDto& detail)
{
    selectedTaskId_ = detail.summary.taskId;
    selectedTaskState_ = detail.summary.state.value;
    retryTaskButton_->setEnabled(selectedTaskState_ == rv1126b::FtpTaskState::Failed);
    taskDetailTable_->setRowCount(detail.targets.size());
    for (int row = 0; row < detail.targets.size(); ++row) {
        const auto& target = detail.targets.at(row);
        taskDetailTable_->setItem(row, 0, item(target.targetId));
        taskDetailTable_->setItem(row, 1, item(taskStateText(target.state.value, target.state.rawValue)));
        taskDetailTable_->setItem(row, 2, item(QString::number(target.total)));
        taskDetailTable_->setItem(row, 3, item(QString::number(target.pending)));
        taskDetailTable_->setItem(row, 4, item(QString::number(target.uploading)));
        taskDetailTable_->setItem(row, 5, item(QString::number(target.done)));
        taskDetailTable_->setItem(row, 6, item(QString::number(target.failed)));
        taskDetailTable_->setItem(row, 7, item(QString::number(target.attempts)));
        taskDetailTable_->setItem(row, 8, item(target.lastError));
    }
    updateTaskRefreshState();
}

void Rv1126bDeviceManagementDialog::applyLocalTaskSnapshots(
    const QVector<rv1126b::StoredFtpTask>& tasks)
{
    localTaskSnapshots_ = tasks;
    taskTable_->setRowCount(tasks.size());
    qint64 newestRefresh = 0;
    for (int row = 0; row < tasks.size(); ++row) {
        const auto& task = tasks.at(row);
        newestRefresh = qMax(newestRefresh, task.refreshedEpochMs);
        auto* id = item(task.taskId);
        id->setData(Qt::UserRole, task.taskId);
        auto* state = item(taskStateText(task.state.value, task.state.rawValue));
        state->setData(Qt::UserRole, static_cast<int>(task.state.value));
        QStringList targetIds;
        for (const auto& target : task.targets) targetIds.append(target.targetId);
        taskTable_->setItem(row, 0, id);
        taskTable_->setItem(row, 1, state);
        taskTable_->setItem(row, 2, item(QStringLiteral("%1 — %2")
            .arg(epochText(task.startEpochMs, Qt::UTC), epochText(task.endEpochMs, Qt::UTC))));
        taskTable_->setItem(row, 3, item(epochText(task.createdEpochMs, Qt::LocalTime)));
        taskTable_->setItem(row, 4, item(targetIds.join(QStringLiteral(", "))));
    }
    previousTasksButton_->setEnabled(false);
    nextTasksButton_->setEnabled(false);
    taskPageLabel_->setText(QStringLiteral("本地快照 · %1 条 · 刷新于 %2")
        .arg(tasks.size()).arg(epochText(newestRefresh, Qt::LocalTime)));
    retryTaskButton_->setEnabled(false);
}

void Rv1126bDeviceManagementDialog::applyLocalTaskDetail(const rv1126b::StoredFtpTask& task)
{
    selectedTaskId_ = task.taskId;
    selectedTaskState_ = task.state.value;
    retryTaskButton_->setEnabled(false);
    taskDetailTable_->setRowCount(task.targets.size());
    for (int row = 0; row < task.targets.size(); ++row) {
        const auto& target = task.targets.at(row);
        taskDetailTable_->setItem(row, 0, item(target.targetId));
        taskDetailTable_->setItem(row, 1, item(taskStateText(target.state.value, target.state.rawValue)));
        taskDetailTable_->setItem(row, 2, item(QString::number(target.total)));
        taskDetailTable_->setItem(row, 3, item(QString::number(target.pending)));
        taskDetailTable_->setItem(row, 4, item(QString::number(target.uploading)));
        taskDetailTable_->setItem(row, 5, item(QString::number(target.done)));
        taskDetailTable_->setItem(row, 6, item(QString::number(target.failed)));
        taskDetailTable_->setItem(row, 7, item(QString::number(target.attempts)));
        taskDetailTable_->setItem(row, 8, item(target.lastError));
    }
}

void Rv1126bDeviceManagementDialog::createTask()
{
    rv1126b::FtpTaskCreate request;
    request.startEpochMs = taskStartEdit_->dateTime().toUTC().toMSecsSinceEpoch();
    request.endEpochMs = taskEndEdit_->dateTime().toUTC().toMSecsSinceEpoch();
    for (int row = 0; row < taskTargets_->count(); ++row) {
        QListWidgetItem* target = taskTargets_->item(row);
        if (target->checkState() == Qt::Checked) request.targetIds.append(target->data(Qt::UserRole).toString());
    }
    controller_->createFtpTask(request);
}

void Rv1126bDeviceManagementDialog::refreshTasks()
{
    if (!controller_ || tabs_->currentIndex() != static_cast<int>(InitialPage::FtpTasks)) return;
    if (!deviceOnline_) {
        controller_->loadLocalFtpTaskSnapshots();
        return;
    }
    if (!controller_->isBusy(QStringLiteral("ftp.tasks.list"))) {
        const QString cursor = taskPageCursors_.value(taskPageIndex_);
        controller_->listFtpTasks(cursor.isEmpty() ? std::nullopt : std::optional<QString>(cursor));
    }
    if (!selectedTaskId_.isEmpty()
        && !controller_->isBusy(QStringLiteral("ftp.task.load"))
        && (selectedTaskState_ == rv1126b::FtpTaskState::Queued
            || selectedTaskState_ == rv1126b::FtpTaskState::Running))
        controller_->loadFtpTask(selectedTaskId_);
}

void Rv1126bDeviceManagementDialog::updateTaskRefreshState()
{
    const bool taskPage = tabs_ && tabs_->currentIndex() == static_cast<int>(InitialPage::FtpTasks);
    if (taskPage && deviceOnline_) {
        if (!taskRefreshTimer_->isActive()) taskRefreshTimer_->start();
    } else {
        taskRefreshTimer_->stop();
    }
}

void Rv1126bDeviceManagementDialog::startLocalFtpReceiver()
{
    if (!ftpReceiveServer_) {
        if (localFtpStatus_) localFtpStatus_->setText(QStringLiteral("当前应用运行时未装配内置 FTP 接收服务"));
        return;
    }
    QHostAddress host;
    if (!host.setAddress(localFtpHostEdit_->text().trimmed())
        || host.protocol() != QAbstractSocket::IPv4Protocol) {
        localFtpStatus_->setText(QStringLiteral("本机 IP 必须是有效 IPv4 地址"));
        return;
    }
    if (localFtpPassiveStartSpin_->value() > localFtpPassiveEndSpin_->value()) {
        localFtpStatus_->setText(QStringLiteral("被动端口范围无效"));
        return;
    }

    rv1126b::EmbeddedFtpReceiveServerConfig config;
    config.rootPath = localFtpRootEdit_->text().trimmed();
    config.userName = localFtpUserEdit_->text().trimmed();
    config.password = localFtpPasswordEdit_->text();
    config.listenAddress = QHostAddress::AnyIPv4;
    config.advertisedAddress = host;
    config.controlPort = static_cast<quint16>(localFtpPortSpin_->value());
    config.passivePortStart = static_cast<quint16>(localFtpPassiveStartSpin_->value());
    config.passivePortEnd = static_cast<quint16>(localFtpPassiveEndSpin_->value());
    if (!ftpReceiveServer_->start(config)) {
        localFtpStatus_->setText(QStringLiteral("接收服务启动失败：%1").arg(ftpReceiveServer_->lastError()));
        updateLocalFtpReceiverState();
        return;
    }
    localFtpStatus_->setText(QStringLiteral("接收服务已启动：%1:%2，目录 %3")
                                 .arg(host.toString())
                                 .arg(ftpReceiveServer_->controlPort())
                                 .arg(config.rootPath));
    updateLocalFtpReceiverState();
}

void Rv1126bDeviceManagementDialog::stopLocalFtpReceiver()
{
    if (ftpReceiveServer_) ftpReceiveServer_->stop();
    if (localFtpStatus_) localFtpStatus_->setText(QStringLiteral("接收服务已停止"));
    updateLocalFtpReceiverState();
}

void Rv1126bDeviceManagementDialog::applyLocalFtpTarget(bool saveAndEnable)
{
    if (!ftpReceiveServer_ || !ftpReceiveServer_->isListening()) {
        startLocalFtpReceiver();
    }
    if (!ftpReceiveServer_ || !ftpReceiveServer_->isListening()) return;
    writeLocalFtpTargetRow();
    if (!validateFtpRowsInline()) return;
    if (!saveAndEnable) {
        const QString targetId = localFtpTargetIdEdit_
            ? localFtpTargetIdEdit_->text().trimmed()
            : QStringLiteral("local_pc");
        localFtpStatus_->setText(QStringLiteral("已填入本机目标 %1，可手动保存或创建历史任务").arg(targetId));
        return;
    }
    if (!controller_) return;
    const rv1126b::FtpConfigUpdate update = collectFtpConfig();
    controller_->saveFtpConfigAndEnableNewEvents(update);
}

void Rv1126bDeviceManagementDialog::updateLocalFtpReceiverState()
{
    const bool available = ftpReceiveServer_ != nullptr;
    const bool running = available && ftpReceiveServer_->isListening();
    if (localFtpStartButton_) localFtpStartButton_->setEnabled(available && !running);
    if (localFtpStopButton_) localFtpStopButton_->setEnabled(running);
    if (!localFtpStatus_) return;
    if (!available) {
        localFtpStatus_->setText(QStringLiteral("当前应用运行时未装配内置 FTP 接收服务"));
    } else if (running) {
        const auto config = ftpReceiveServer_->config();
        localFtpStatus_->setText(QStringLiteral("接收服务运行中：%1:%2 -> %3")
                                     .arg(config.advertisedAddress.toString())
                                     .arg(ftpReceiveServer_->controlPort())
                                     .arg(config.rootPath));
    } else if (localFtpStatus_->text().isEmpty()) {
        localFtpStatus_->setText(QStringLiteral("启动后可一键写入板端 FTP 目标，客户无需另开 FTP 工具"));
    }
}

QString Rv1126bDeviceManagementDialog::defaultLocalFtpAddress() const
{
    QString fallback;
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : interfaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp)
            || !(iface.flags() & QNetworkInterface::IsRunning)
            || (iface.flags() & QNetworkInterface::IsLoopBack)) {
            continue;
        }
        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            const QHostAddress address = entry.ip();
            if (address.protocol() != QAbstractSocket::IPv4Protocol) continue;
            const QString text = address.toString();
            if (text.startsWith(QStringLiteral("192.168.137."))) return text;
            if (fallback.isEmpty()) fallback = text;
        }
    }
    return fallback.isEmpty() ? QStringLiteral("192.168.137.1") : fallback;
}

QString Rv1126bDeviceManagementDialog::defaultLocalFtpTargetId(const QString& host) const
{
    QString suffix = host.trimmed();
    suffix.replace(QRegularExpression(QStringLiteral(R"([^A-Za-z0-9]+)")), QStringLiteral("_"));
    suffix.replace(QRegularExpression(QStringLiteral(R"(^_+|_+$)")), QString());
    return QStringLiteral("pc_%1").arg(suffix.isEmpty() ? QStringLiteral("local") : suffix);
}

void Rv1126bDeviceManagementDialog::syncLocalFtpTargetIdFromHost()
{
    if (!localFtpTargetIdAuto_ || !localFtpTargetIdEdit_) return;
    localFtpTargetIdEdit_->setText(defaultLocalFtpTargetId(localFtpHostEdit_->text()));
}

int Rv1126bDeviceManagementDialog::localFtpTargetRow() const
{
    if (!ftpTargetsTable_) return -1;
    const QString targetId = localFtpTargetIdEdit_
        ? localFtpTargetIdEdit_->text().trimmed()
        : QStringLiteral("local_pc");
    for (int row = 0; row < ftpTargetsTable_->rowCount(); ++row) {
        auto* id = qobject_cast<QLineEdit*>(ftpTargetsTable_->cellWidget(row, 1));
        if (id && id->text().trimmed() == targetId) return row;
    }
    return -1;
}

void Rv1126bDeviceManagementDialog::writeLocalFtpTargetRow()
{
    const QString targetId = localFtpTargetIdEdit_
        ? localFtpTargetIdEdit_->text().trimmed()
        : QStringLiteral("local_pc");
    if (targetId.isEmpty()) {
        showError(QStringLiteral("invalid_ftp_target_id"), QStringLiteral("FTP 目标 ID 不能为空"));
        return;
    }
    if (targetId.size() > 32
        || !QRegularExpression(QStringLiteral(R"(^[A-Za-z0-9_-]+$)")).match(targetId).hasMatch()) {
        showError(QStringLiteral("invalid_ftp_target_id"),
                  QStringLiteral("FTP 目标 ID 只能包含 1..32 位字母、数字、下划线和横线"));
        return;
    }
    int row = localFtpTargetRow();
    if (row < 0) {
        if (ftpTargetsTable_->rowCount() >= rv1126b::DeviceOperationsController::MaxFtpTargets) {
            showError(QStringLiteral("too_many_ftp_targets"), QStringLiteral("FTP 目标最大 8 个，请先删除一个目标"));
            return;
        }
        addFtpTargetRow();
        row = ftpTargetsTable_->rowCount() - 1;
    }
    qobject_cast<QCheckBox*>(ftpTargetsTable_->cellWidget(row, 0))->setChecked(true);
    qobject_cast<QLineEdit*>(ftpTargetsTable_->cellWidget(row, 1))->setText(targetId);
    qobject_cast<QLineEdit*>(ftpTargetsTable_->cellWidget(row, 2))->setText(localFtpHostEdit_->text().trimmed());
    qobject_cast<QSpinBox*>(ftpTargetsTable_->cellWidget(row, 3))->setValue(localFtpPortSpin_->value());
    qobject_cast<QLineEdit*>(ftpTargetsTable_->cellWidget(row, 4))->setText(localFtpUserEdit_->text().trimmed());
    auto* action = qobject_cast<QComboBox*>(ftpTargetsTable_->cellWidget(row, 5));
    action->setCurrentIndex(action->findData(static_cast<int>(rv1126b::FtpPasswordAction::Replace)));
    auto* password = qobject_cast<QLineEdit*>(ftpTargetsTable_->cellWidget(row, 6));
    password->setEnabled(true);
    password->setText(localFtpPasswordEdit_->text());
    qobject_cast<QLineEdit*>(ftpTargetsTable_->cellWidget(row, 7))->setText(QStringLiteral("/vehicle_events"));
    qobject_cast<QCheckBox*>(ftpTargetsTable_->cellWidget(row, 8))->setChecked(true);
    if (auto* configured = qobject_cast<QLabel*>(ftpTargetsTable_->cellWidget(row, 9))) {
        configured->setText(QStringLiteral("将替换"));
    }
}

void Rv1126bDeviceManagementDialog::showRevisionConflict(
    const rv1126b::FtpConfigSnapshotDto& remote,
    const rv1126b::FtpConfigUpdate& local,
    const QStringList& passwordTargetIds)
{
    const QString passwordNote = passwordTargetIds.isEmpty()
        ? QString()
        : QStringLiteral("\n\n以下目标的替换密码已清空，必须重新输入后再提交：%1")
              .arg(passwordTargetIds.join(QStringLiteral(", ")));
    const auto answer = QMessageBox::question(
        this, QStringLiteral("FTP revision 冲突"),
        QStringLiteral("远端配置已变化。保留当前非敏感编辑并采用最新 revision 重提？\n\n%1%2")
            .arg(conflictSummary(remote, local), passwordNote));
    if (answer != QMessageBox::Yes) return;
    ftpRevision_ = remote.revision;
    ftpRevisionLabel_->setText(QStringLiteral("revision：%1（冲突后已更新）").arg(ftpRevision_));
    if (!passwordTargetIds.isEmpty()) {
        ftpStatus_->setText(QStringLiteral("请重新输入替换密码，再点击“保存并启用”"));
        return;
    }
    rv1126b::FtpConfigUpdate retry = collectFtpConfig();
    retry.expectedRevision = remote.revision;
    controller_->saveFtpConfigAndEnableNewEvents(retry);
}

QString Rv1126bDeviceManagementDialog::conflictSummary(
    const rv1126b::FtpConfigSnapshotDto& remote,
    const rv1126b::FtpConfigUpdate& local) const
{
    QStringList remoteTargets;
    for (const auto& target : remote.targets)
        remoteTargets.append(QStringLiteral("%1=%2:%3/%4")
            .arg(target.id, target.host).arg(target.port).arg(target.remoteDir));
    QStringList localTargets;
    for (const auto& target : local.targets)
        localTargets.append(QStringLiteral("%1=%2:%3/%4")
            .arg(target.id, target.host).arg(target.port).arg(target.remoteDir));
    return QStringLiteral("远端 revision：%1\n远端目标：%2\n本地目标：%3")
        .arg(remote.revision, remoteTargets.join(QStringLiteral("；")), localTargets.join(QStringLiteral("；")));
}
