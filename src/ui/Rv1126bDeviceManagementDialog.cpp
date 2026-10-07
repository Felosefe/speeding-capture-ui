#include "Rv1126bDeviceManagementDialog.h"

#include "../rv1126b/services/BoardDataPullService.h"
#include "../rv1126b/services/DetectionResultWriter.h"

#include "../rv1126b/ports/IBoardApiClient.h"
#include "../rv1126b/services/EmbeddedFtpReceiveServer.h"
#include "../rv1126b/services/EventSyncService.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
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
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextStream>
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

QString csvEscape(QString value)
{
    value.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    if (value.contains(QLatin1Char(',')) || value.contains(QLatin1Char('"'))
        || value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r'))) {
        return QStringLiteral("\"%1\"").arg(value);
    }
    return value;
}

QString safeSegment(QString value, const QString& fallback)
{
    value = value.trimmed();
    if (value.isEmpty()) value = fallback;
    static const QRegularExpression invalid(QStringLiteral(R"([<>:"/\\|?*\x00-\x1f])"));
    value.replace(invalid, QStringLiteral("_"));
    value.replace(QRegularExpression(QStringLiteral(R"(\s+)")), QStringLiteral("_"));
    while (value.contains(QStringLiteral("__"))) value.replace(QStringLiteral("__"), QStringLiteral("_"));
    value = value.left(96).trimmed();
    return value.isEmpty() ? fallback : value;
}

QString jsonString(const QJsonObject& object, const QString& key, const QString& fallback = QString())
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : fallback;
}

QString nestedJsonString(const QJsonObject& object, const QString& objectKey, const QString& key)
{
    const QJsonValue nested = object.value(objectKey);
    if (!nested.isObject()) return {};
    return jsonString(nested.toObject(), key);
}

bool writeTextFile(const QString& path, const QString& text)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << text;
    return true;
}

} // namespace

// 板端错误码 → 现场能看懂的中文。以前直接显示 error.message，而 codec 只在 HTTP 400 时
// 保留板端报文，403/405/500 一律变成 "Board request failed with HTTP status 403."，
// 用户看不到真正原因（配置写入被关掉、ISP 读取失败等）。
QString boardErrorText(const rv1126b::ApiError& error)
{
    const QString code = error.code;
    if (code == QStringLiteral("config_write_disabled")) {
        return QStringLiteral("板端已关闭配置写入（APP_API_CONFIG_WRITE_ENABLED=0），请先在板端开启后再试");
    }
    if (code == QStringLiteral("isp_config_unavailable")) {
        return QStringLiteral("板端无法读取图像配置");
    }
    if (code == QStringLiteral("isp_current_unavailable")) {
        return QStringLiteral("板端无法读取当前运行中的图像参数");
    }
    if (code == QStringLiteral("isp_config_write_failed")) {
        return QStringLiteral("板端写入图像配置失败");
    }
    if (code == QStringLiteral("config_revision_conflict")) {
        return QStringLiteral("配置已被他处修改，请先重新读取再保存");
    }
    if (code == QStringLiteral("time_set_disabled")) {
        return QStringLiteral("板端已关闭校时功能");
    }
    if (code == QStringLiteral("ftp_config_write_disabled")) {
        return QStringLiteral("板端已关闭 FTP 配置写入");
    }
    if (code == QStringLiteral("ftp_task_write_disabled")) {
        return QStringLiteral("板端已关闭 FTP 任务创建");
    }
    const QString message = error.message;
    if (code.isEmpty()) {
        return message.isEmpty() ? QStringLiteral("未知错误") : message;
    }
    if (message.isEmpty()) {
        return QStringLiteral("板端返回错误（%1）").arg(code);
    }
    return QStringLiteral("%1（%2）").arg(message, code);
}

Rv1126bDeviceManagementDialog::Rv1126bDeviceManagementDialog(
    const QString& deviceId,
    rv1126b::DeviceOperationsController* controller,
    InitialPage initialPage,
    QWidget* parent,
    bool deviceOnline,
    rv1126b::EmbeddedFtpReceiveServer* ftpReceiveServer,
    const QString& localFtpRootPath,
    rv1126b::EventSyncService* eventSyncService,
    const QString& evidenceRootPath,
    const QString& deviceEndpointText,
    rv1126b::IBoardApiClient* boardApi,
    std::function<rv1126b::IBoardApiClient*(const QString&)> boardApiForHost,
    std::function<QString(const QString&)> deviceIdForHost,
    const QString& storageRootPath,
    std::function<bool(const QString&)> storageRootChangeHandler,
    rv1126b::DetectionPullDependencies detectionPull)
    : QDialog(parent)
    , deviceId_(deviceId)
    , controller_(controller)
    , ftpReceiveServer_(ftpReceiveServer)
    , eventSyncService_(eventSyncService)
    , boardApi_(boardApi)
    , currentExportApi_(boardApi)
    , boardApiForHost_(std::move(boardApiForHost))
    , deviceIdForHost_(std::move(deviceIdForHost))
    , storageRootChangeHandler_(std::move(storageRootChangeHandler))
    , localFtpRootPath_(localFtpRootPath)
    , evidenceRootPath_(evidenceRootPath)
    , storageRootPath_(storageRootPath)
    , deviceEndpointText_(deviceEndpointText)
    , detectionPull_(detectionPull)
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
    /*
     * 2026-10-07：事件同步重做成独立一页。原来的“HTTP 事件同步”连同图像曝光、
     * FTP 配置、FTP 历史任务一起删掉了（那几页是乱写的）。
     */
    tabs_->addTab(createEventSyncPage(), QStringLiteral("事件同步"));
    /*
     * 2026-10-07: the ISP / FTP-config / FTP-tasks pages were removed from the UI
     * at the user's request. Their widgets are still constructed (and deliberately
     * NOT added to tabs_) because connectController() binds board callbacks to them
     * and the constructor calls controller_->loadAll(); a callback writing into a
     * nullptr widget crashes this dialog. Removing the widgets means also removing
     * those bindings - that cleanup is a separate step.
     */
    createIspPage();
    createFtpConfigPage();
    createFtpTasksPage();
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
    tabs_->setTabEnabled(2, eventSyncService_ != nullptr);
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

QWidget* Rv1126bDeviceManagementDialog::createEventSyncPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* summary = new QGroupBox(QStringLiteral("主动拉取链路"), page);
    auto* form = new QFormLayout(summary);
    eventSyncModeLabel_ = new QLabel(summary);
    eventSyncRootLabel_ = new QLabel(summary);
    eventSyncStatus_ = new QLabel(summary);
    eventStorageRootEdit_ = new QLineEdit(defaultEventStorageRoot(), summary);
    eventStorageBrowseButton_ = new QPushButton(QStringLiteral("选择"), summary);
    eventStorageSaveButton_ = new QPushButton(QStringLiteral("保存路径"), summary);
    eventSyncStatus_->setObjectName(QStringLiteral("eventSyncStatusLabel"));
    eventSyncStatus_->setWordWrap(true);
    eventSyncModeLabel_->setText(QStringLiteral("本机主动向板端拉取事件（HTTP API）；不需要 FTP，也不需要对方电脑开放端口。"));
    eventSyncRootLabel_->setText(QDir(currentEventStorageRoot()).filePath(QStringLiteral("rv1126b/events")));
    eventSyncStatus_->setText(eventSyncService_
                                  ? (eventSyncService_->isRunning()
                                         ? QStringLiteral("正在自动同步新事件")
                                         : QStringLiteral("同步服务已装配，但当前未运行"))
                                  : QStringLiteral("当前设备没有装配 HTTP 事件同步服务"));
    form->addRow(QStringLiteral("同步方式"), eventSyncModeLabel_);
    form->addRow(QStringLiteral("当前板端"), new QLabel(deviceEndpointText_.trimmed().isEmpty()
                                                       ? deviceId_
                                                       : deviceEndpointText_, summary));
    form->addRow(QStringLiteral("事件缓存目录"), eventSyncRootLabel_);
    auto* storageRow = new QWidget(summary);
    auto* storageLayout = new QHBoxLayout(storageRow);
    storageLayout->setContentsMargins(0, 0, 0, 0);
    storageLayout->addWidget(eventStorageRootEdit_, 1);
    storageLayout->addWidget(eventStorageBrowseButton_);
    storageLayout->addWidget(eventStorageSaveButton_);
    form->addRow(QStringLiteral("本地存储根目录"), storageRow);
    form->addRow(QStringLiteral("状态"), eventSyncStatus_);
    layout->addWidget(summary);

    // ── 一键把板端已有数据拉回本机文件夹（用户第 5 项需求）────────────────────
    // 界面上只留「目标文件夹 + 开始拉取 + 进度」，参数全部收起来。
    auto* pullBox = new QGroupBox(QStringLiteral("一键把板端已有检测数据拉回本机文件夹"), page);
    auto* pullLayout = new QVBoxLayout(pullBox);
    auto* pullHint = new QLabel(
        QStringLiteral("把这台相机上已经拍到的检测记录（车牌/速度/方向/OCR 结果 + 事件详情）"
                       "一次性拉到下面这个文件夹，按「设备/日期/事件」分目录，"
                       "文件夹不存在会自动新建。图片会由后台自动同步继续补齐。"),
        pullBox);
    pullHint->setWordWrap(true);
    pullHint->setStyleSheet(QStringLiteral("color:#44515f;"));
    pullLayout->addWidget(pullHint);

    auto* pullRow = new QWidget(pullBox);
    auto* pullRowLayout = new QHBoxLayout(pullRow);
    pullRowLayout->setContentsMargins(0, 0, 0, 0);
    boardPullButton_ = new QPushButton(QStringLiteral("开始拉取到本机"), pullRow);
    boardPullButton_->setObjectName(QStringLiteral("boardDataPullButton"));
    boardPullStatus_ = new QLabel(QStringLiteral("尚未开始"), pullRow);
    boardPullStatus_->setObjectName(QStringLiteral("boardDataPullStatusLabel"));
    boardPullStatus_->setWordWrap(true);
    /*
     * 拉取的时间范围（2026-10-07）。默认“全部”，保持原有行为不变。范围由
     * BoardDataPullService 在翻页时按 source_epoch_ms 判断：板端按时间降序返回，
     * 翻到第一条更早的即停；范围外的事件既不落库、也不写资料包。
     */
    boardPullRangeCombo_ = new QComboBox(pullRow);
    boardPullRangeCombo_->setObjectName(QStringLiteral("boardDataPullRangeCombo"));
    boardPullRangeCombo_->addItem(QStringLiteral("今天"));
    boardPullRangeCombo_->addItem(QStringLiteral("最近一周"));
    boardPullRangeCombo_->addItem(QStringLiteral("最近一月"));
    boardPullRangeCombo_->addItem(QStringLiteral("全部"));
    boardPullRangeCombo_->setCurrentIndex(3);
    boardPullRangeCombo_->setToolTip(
        QStringLiteral("只拉取该时间范围内的检测记录；“全部”= 板端保留的所有事件。"));
    pullRowLayout->addWidget(boardPullButton_);
    pullRowLayout->addWidget(boardPullRangeCombo_);
    pullRowLayout->addWidget(boardPullStatus_, 1);

    /*
     * 目标电脑（2026-10-07）。默认本机；填 UNC（\\对方IP\共享名\子目录）就是把
     * 拉回来的资料包直接写到局域网内指定 IP 的电脑上 —— 这是"拉到指定 IP 的电脑"
     * 里唯一既不用对方装接收程序、也不用改板端的做法：对方只要把那个文件夹设成
     * 共享且可写。另一条路是在那台电脑上装同一软件、填板端 IP+Token 直接拉，
     * 下面那行提示里两条都写明了。
     */
    auto* targetRow = new QWidget(pullBox);
    auto* targetRowLayout = new QHBoxLayout(targetRow);
    targetRowLayout->setContentsMargins(0, 0, 0, 0);
    targetRowLayout->addWidget(new QLabel(QStringLiteral("目标电脑"), targetRow));
    eventSyncTargetEdit_ = new QLineEdit(targetRow);
    eventSyncTargetEdit_->setObjectName(QStringLiteral("eventSyncTargetFolderEdit"));
    eventSyncTargetEdit_->setPlaceholderText(
        QStringLiteral("本机文件夹；或 \\\\对方IP\\共享名\\子目录"));
    if (detectionPull_.writer) {
        eventSyncTargetEdit_->setText(detectionPull_.writer->targetRoot());
    }
    targetRowLayout->addWidget(eventSyncTargetEdit_, 1);
    auto* targetBrowseButton = new QPushButton(QStringLiteral("浏览…"), targetRow);
    connect(targetBrowseButton, &QPushButton::clicked, this,
            [this]() { browseEventSyncTargetFolder(); });
    targetRowLayout->addWidget(targetBrowseButton);
    auto* targetApplyButton = new QPushButton(QStringLiteral("保存目标"), targetRow);
    connect(targetApplyButton, &QPushButton::clicked, this,
            [this]() { applyEventSyncTargetFolder(); });
    targetRowLayout->addWidget(targetApplyButton);
    pullLayout->addWidget(targetRow);

    auto* targetHint = new QLabel(
        QStringLiteral("要给另一台电脑：① 这里填 \\\\对方IP\\共享名（对方需共享可写文件夹）；"
                       "② 或在那台电脑上装同一软件，填板端 IP 与 Token 直接拉。"),
        pullBox);
    targetHint->setWordWrap(true);
    targetHint->setStyleSheet(QStringLiteral("color:#44515f;"));
    pullLayout->addWidget(targetHint);
    pullLayout->addWidget(pullRow);

    const bool pullAvailable = detectionPull_.repository && detectionPull_.writer && boardApi_;
    boardPullButton_->setEnabled(pullAvailable);
    if (!pullAvailable) {
        boardPullStatus_->setText(QStringLiteral("当前设备不可用（需要设备在线且已装配本地库）"));
    } else {
        boardPullStatus_->setText(QStringLiteral("将写入：%1")
                                      .arg(detectionPull_.writer->targetRoot()));
        boardPullService_ = new rv1126b::BoardDataPullService(
            detectionPull_.repository, detectionPull_.writer, this);
        connect(boardPullService_, &rv1126b::BoardDataPullService::progress,
                this, &Rv1126bDeviceManagementDialog::handleBoardDataPullProgress);
        connect(boardPullService_, &rv1126b::BoardDataPullService::finished,
                this, &Rv1126bDeviceManagementDialog::handleBoardDataPullFinished);
        connect(boardPullService_, &rv1126b::BoardDataPullService::failed,
                this, [this](const QString& message) {
                    if (boardPullStatus_) boardPullStatus_->setText(QStringLiteral("拉取失败：%1").arg(message));
                });
    }
    connect(boardPullButton_, &QPushButton::clicked, this, [this]() {
        /*
         * 2026-10-07：这个按钮现在驱动的是"资料包导出"（exportInFlight_），
         * 所以停止必须停导出，而不是只停旧的 boardPullService_ —— 否则点了停止没反应，
         * 再点一次还会重新弹确认框、再起一遍导出（两遍同时跑）。
         */
        if (exportInFlight_) {
            exportInFlight_ = false;
            if (currentExportApi_) currentExportApi_->cancelAll();
            if (boardPullButton_) boardPullButton_->setText(QStringLiteral("开始拉取到本机"));
            if (eventExportButton_) eventExportButton_->setEnabled(boardApi_ != nullptr);
            if (boardPullStatus_) boardPullStatus_->setText(QStringLiteral("已停止拉取"));
            if (eventExportStatus_) eventExportStatus_->setText(QStringLiteral("已停止拉取"));
            return;
        }
        if (boardPullService_ && boardPullService_->isRunning()) {
            boardPullService_->cancel();
            return;
        }
        startBoardDataPull();
    });
    layout->addWidget(pullBox);

    auto* actions = new QGroupBox(QStringLiteral("自动同步"), page);
    auto* actionsLayout = new QGridLayout(actions);
    eventSyncStartButton_ = new QPushButton(QStringLiteral("启动自动同步"), actions);
    eventSyncStopButton_ = new QPushButton(QStringLiteral("暂停自动同步"), actions);
    eventSyncPollButton_ = new QPushButton(QStringLiteral("立即同步一次"), actions);
    eventSyncAllButton_ = new QPushButton(QStringLiteral("拉取全部已有数据"), actions);
    eventSyncFromNowButton_ = new QPushButton(QStringLiteral("从现在开始拉取"), actions);
    eventSyncStartButton_->setObjectName(QStringLiteral("eventSyncStartButton"));
    eventSyncStopButton_->setObjectName(QStringLiteral("eventSyncStopButton"));
    eventSyncPollButton_->setObjectName(QStringLiteral("eventSyncPollButton"));
    eventSyncAllButton_->setObjectName(QStringLiteral("eventSyncAllButton"));
    eventSyncFromNowButton_->setObjectName(QStringLiteral("eventSyncFromNowButton"));
    actionsLayout->addWidget(eventSyncStartButton_, 0, 0);
    actionsLayout->addWidget(eventSyncStopButton_, 0, 1);
    actionsLayout->addWidget(eventSyncPollButton_, 0, 2);
    actionsLayout->addWidget(eventSyncAllButton_, 1, 0);
    actionsLayout->addWidget(eventSyncFromNowButton_, 1, 1);
    layout->addWidget(actions);

    auto* exportBox = new QGroupBox(QStringLiteral("生成用户可读事件资料包"), page);
    auto* exportLayout = new QGridLayout(exportBox);
    eventExportRangeCombo_ = new QComboBox(exportBox);
    eventExportRangeCombo_->addItem(QStringLiteral("拉取全部已有事件"), QStringLiteral("all"));
    eventExportRangeCombo_->addItem(QStringLiteral("仅拉取最新 100 条"), QStringLiteral("latest100"));
    eventExportRangeCombo_->addItem(QStringLiteral("最近 N 天"), QStringLiteral("recent_days"));
    eventExportDaysSpin_ = new QSpinBox(exportBox);
    eventExportDaysSpin_->setRange(1, 3650);
    eventExportDaysSpin_->setValue(1);
    eventExportDaysSpin_->setSuffix(QStringLiteral(" 天"));
    exportEvidenceCheck_ = new QCheckBox(QStringLiteral("证据图 evidence.jpg"), exportBox);
    exportSnapshotCheck_ = new QCheckBox(QStringLiteral("原始抓拍 snapshot.jpg"), exportBox);
    exportFormalCheck_ = new QCheckBox(QStringLiteral("事件 JSON event.json"), exportBox);
    exportOcrCheck_ = new QCheckBox(QStringLiteral("OCR JSON ocr.json"), exportBox);
    exportDetailCheck_ = new QCheckBox(QStringLiteral("详情 JSON detail.json"), exportBox);
    exportSummaryCheck_ = new QCheckBox(QStringLiteral("摘要 summary.txt"), exportBox);
    exportTrackMetaCheck_ = new QCheckBox(QStringLiteral("轨迹调试 track_meta.json"), exportBox);
    for (QCheckBox* box : {exportEvidenceCheck_, exportSnapshotCheck_, exportFormalCheck_,
                           exportOcrCheck_, exportDetailCheck_, exportSummaryCheck_}) {
        box->setChecked(true);
    }
    exportTrackMetaCheck_->setChecked(false);
    eventExportButton_ = new QPushButton(QStringLiteral("导出事件资料包"), exportBox);
    eventExportButton_->setObjectName(QStringLiteral("eventExportButton"));
    eventExportStatus_ = new QLabel(QStringLiteral("导出目录：%1").arg(currentEventExportRoot()), exportBox);
    eventExportStatus_->setObjectName(QStringLiteral("eventExportStatusLabel"));
    eventExportStatus_->setWordWrap(true);
    exportLayout->addWidget(new QLabel(QStringLiteral("板端 IP"), exportBox), 0, 0);
    exportLayout->addWidget(new QLabel(QStringLiteral("范围"), exportBox), 1, 0);
    exportLayout->addWidget(eventExportRangeCombo_, 1, 1);
    exportLayout->addWidget(eventExportDaysSpin_, 1, 2);
    exportLayout->addWidget(exportEvidenceCheck_, 2, 0);
    exportLayout->addWidget(exportSnapshotCheck_, 2, 1);
    exportLayout->addWidget(exportFormalCheck_, 2, 2);
    exportLayout->addWidget(exportOcrCheck_, 3, 0);
    exportLayout->addWidget(exportDetailCheck_, 3, 1);
    exportLayout->addWidget(exportSummaryCheck_, 3, 2);
    exportLayout->addWidget(exportTrackMetaCheck_, 4, 0);
    exportLayout->addWidget(eventExportButton_, 5, 0);
    exportLayout->addWidget(eventExportStatus_, 5, 1, 1, 2);
    layout->addWidget(exportBox);
    layout->addStretch();

    const bool available = eventSyncService_ != nullptr;
    eventSyncStartButton_->setEnabled(available && !eventSyncService_->isRunning());
    eventSyncStopButton_->setEnabled(available && eventSyncService_->isRunning());
    eventSyncPollButton_->setEnabled(available);
    eventSyncAllButton_->setEnabled(available);
    eventSyncFromNowButton_->setEnabled(available);
    eventExportButton_->setEnabled(boardApi_ != nullptr);
    eventExportDaysSpin_->setEnabled(eventExportRangeCombo_->currentData().toString() == QStringLiteral("recent_days"));

    connect(eventSyncStartButton_, &QPushButton::clicked, this, [this]() {
        if (!eventSyncService_) return;
        eventSyncService_->start();
        eventSyncStatus_->setText(QStringLiteral("正在自动同步新事件"));
        eventSyncStartButton_->setEnabled(false);
        eventSyncStopButton_->setEnabled(true);
    });
    connect(eventSyncStopButton_, &QPushButton::clicked, this, [this]() {
        if (!eventSyncService_) return;
        eventSyncService_->stop();
        eventSyncStatus_->setText(QStringLiteral("自动同步已暂停"));
        eventSyncStartButton_->setEnabled(true);
        eventSyncStopButton_->setEnabled(false);
    });
    connect(eventSyncPollButton_, &QPushButton::clicked, this, [this]() {
        if (!eventSyncService_) return;
        eventSyncService_->pollNow();
        eventSyncStatus_->setText(QStringLiteral("已请求立即同步一次"));
    });
    connect(eventSyncAllButton_, &QPushButton::clicked, this, [this]() {
        if (!eventSyncService_) return;
        if (QMessageBox::question(this, QStringLiteral("拉取全部已有数据"),
                                  QStringLiteral("确认从板端补拉所有已有事件？数据较多时会持续一段时间。"))
            != QMessageBox::Yes) return;
        eventSyncService_->syncAllExisting();
        eventSyncStatus_->setText(QStringLiteral("正在补拉板端已有事件"));
        eventSyncStartButton_->setEnabled(false);
        eventSyncStopButton_->setEnabled(true);
    });
    connect(eventSyncFromNowButton_, &QPushButton::clicked, this, [this]() {
        if (!eventSyncService_) return;
        if (QMessageBox::question(this, QStringLiteral("从现在开始拉取"),
                                  QStringLiteral("确认忽略板端已有历史事件，只拉取之后新产生的数据？"))
            != QMessageBox::Yes) return;
        eventSyncService_->markCurrentHeadAsSynced();
        eventSyncStatus_->setText(QStringLiteral("正在设置同步起点"));
        eventSyncStartButton_->setEnabled(false);
        eventSyncStopButton_->setEnabled(true);
    });
    connect(eventStorageBrowseButton_, &QPushButton::clicked,
            this, &Rv1126bDeviceManagementDialog::browseEventStorageRoot);
    connect(eventStorageSaveButton_, &QPushButton::clicked,
            this, &Rv1126bDeviceManagementDialog::saveEventStorageRoot);
    connect(eventStorageRootEdit_, &QLineEdit::textChanged, this, [this]() {
        if (eventSyncRootLabel_)
            eventSyncRootLabel_->setText(QDir(currentEventStorageRoot()).filePath(QStringLiteral("rv1126b/events")));
        if (eventExportStatus_ && !exportInFlight_)
            eventExportStatus_->setText(QStringLiteral("导出目录：%1").arg(currentEventExportRoot()));
    });
    connect(eventExportRangeCombo_, &QComboBox::currentIndexChanged, this, [this]() {
        eventExportDaysSpin_->setEnabled(eventExportRangeCombo_->currentData().toString() == QStringLiteral("recent_days"));
    });
    connect(eventExportButton_, &QPushButton::clicked, this, &Rv1126bDeviceManagementDialog::startEventExport);

    if (eventSyncService_) {
        connect(eventSyncService_, &rv1126b::EventSyncService::syncError,
                this, [this](const QString&, const rv1126b::ApiError& error) {
                    eventSyncStatus_->setText(QStringLiteral("同步失败：%1").arg(error.message.isEmpty() ? error.code : error.message));
                });
        connect(eventSyncService_, &rv1126b::EventSyncService::syncActionFinished,
                this, [this](const QString&, const QString& action) {
                    eventSyncStatus_->setText(QStringLiteral("同步操作完成：%1").arg(action));
                    eventSyncStartButton_->setEnabled(!eventSyncService_->isRunning());
                    eventSyncStopButton_->setEnabled(eventSyncService_->isRunning());
                });
    }

    return page;
}

QWidget* Rv1126bDeviceManagementDialog::createIspPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* box = new QGroupBox(QStringLiteral("开机默认图像参数"), page);
    auto* form = new QFormLayout(box);
    ispStatus_ = new QLabel(QStringLiteral("未读取"), box);
    ispStatus_->setWordWrap(true);
    ispCurrentLabel_ = new QLabel(QStringLiteral("-"), box);
    ispCurrentLabel_->setWordWrap(true);
    ispPersistedLabel_ = new QLabel(QStringLiteral("-"), box);
    ispPersistedLabel_->setWordWrap(true);
    form->addRow(QStringLiteral("状态"), ispStatus_);
    form->addRow(QStringLiteral("当前运行"), ispCurrentLabel_);
    form->addRow(QStringLiteral("开机默认"), ispPersistedLabel_);
    layout->addWidget(box);

    auto* hint = new QLabel(QStringLiteral(
        "这一页只是把相机当前运行中的图像参数记下来当「开机默认」，本身不会去调曝光。"
        "要改画面请先在相机网页端或调试工具里调好，再回到这里点「保存当前为开机默认」。"
        "保存是写在板端配置里的，要重启相机服务或断电重启才会套用——重启前画面不会变。"
        "板端若关闭了配置写入，保存按钮会是灰的。"), page);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#44515f;"));
    layout->addWidget(hint);

    auto* row = new QHBoxLayout();
    ispRefreshButton_ = new QPushButton(QStringLiteral("重新读取"), page);
    ispSaveCurrentButton_ = new QPushButton(QStringLiteral("保存当前为开机默认"), page);
    ispClearButton_ = new QPushButton(QStringLiteral("取消开机默认覆盖"), page);
    ispClearButton_->setToolTip(QStringLiteral("取消后开机不再套用这组参数；已写入板端配置的当前值不会被还原"));
    row->addWidget(ispRefreshButton_);
    row->addWidget(ispSaveCurrentButton_);
    row->addWidget(ispClearButton_);
    row->addStretch();
    layout->addLayout(row);
    layout->addStretch();

    connect(ispRefreshButton_, &QPushButton::clicked, this, &Rv1126bDeviceManagementDialog::refreshIspConfig);
    connect(ispSaveCurrentButton_, &QPushButton::clicked, this, &Rv1126bDeviceManagementDialog::saveCurrentIspConfig);
    connect(ispClearButton_, &QPushButton::clicked, this, &Rv1126bDeviceManagementDialog::clearIspConfig);
    QTimer::singleShot(0, this, &Rv1126bDeviceManagementDialog::refreshIspConfig);
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
    auto* saveTarget = new QPushButton(QStringLiteral("一键启动接收并配置板端"), receiver);
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
    saveFtpButton_ = new QPushButton(QStringLiteral("保存并启用全部事件自动下发"), page);
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
                else ftpStatus_->setText(QStringLiteral("FTP 配置已保存，并已启用 all_existing 自动下发"));
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
        localFtpStatus_->setText(QStringLiteral("已填入本机目标 %1，可一键配置板端或创建历史任务").arg(targetId));
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
        localFtpStatus_->setText(QStringLiteral("点击“一键启动接收并配置板端”即可启动接收服务并写入板端"));
    }
}

void Rv1126bDeviceManagementDialog::startEventExport()
{
    if (exportInFlight_) return;
    const bool anyContent = exportEvidenceCheck_->isChecked()
        || exportSnapshotCheck_->isChecked()
        || exportFormalCheck_->isChecked()
        || exportOcrCheck_->isChecked()
        || exportDetailCheck_->isChecked()
        || exportSummaryCheck_->isChecked()
        || exportTrackMetaCheck_->isChecked();
    if (!anyContent) {
        eventExportStatus_->setText(QStringLiteral("请至少选择一种导出内容"));
        return;
    }

    exportTargetHosts_ = eventExportHosts();
    if (exportTargetHosts_.isEmpty()) {
        eventExportStatus_->setText(QStringLiteral("请填写至少一个板端 IP"));
        return;
    }
    if (!QDir().mkpath(currentEventExportRoot())) {
        eventExportStatus_->setText(QStringLiteral("无法创建导出目录：%1").arg(currentEventExportRoot()));
        return;
    }

    exportInFlight_ = true;
    exportTargetIndex_ = 0;
    exportSucceeded_ = 0;
    exportFailed_ = 0;
    eventExportButton_->setEnabled(false);
    startNextEventExportTarget();
}

/*
 * 2026-10-07：已导出事件的集合（eventId:trackId），从 index.csv 载入。
 * 对话框是单实例，用文件级静态即可，避免为这个改动去动头文件。
 */
static QSet<QString> s_exportDoneKeys;
void Rv1126bDeviceManagementDialog::startNextEventExportTarget()
{
    if (!exportInFlight_) return;
    exportEvents_.clear();
    exportFiles_.clear();
    exportEventIndex_ = 0;
    exportFileIndex_ = 0;
    currentExportApi_ = nullptr;
    exportTargetHost_.clear();
    exportTargetDeviceId_.clear();

    if (exportTargetIndex_ >= exportTargetHosts_.size()) {
        exportInFlight_ = false;
        if (eventExportButton_) eventExportButton_->setEnabled(boardApi_ != nullptr);
        if (eventExportStatus_) {
            eventExportStatus_->setText(QStringLiteral("导出完成：成功事件 %1，失败/缺失文件 %2；目录：%3")
                                            .arg(exportSucceeded_)
                                            .arg(exportFailed_)
                                            .arg(currentEventExportRoot()));
        }
        return;
    }

    exportTargetHost_ = exportTargetHosts_.at(exportTargetIndex_).trimmed();
    const QString currentHost = deviceEndpointText_.section(QLatin1Char(':'), 0, 0).trimmed();
    if (boardApiForHost_) currentExportApi_ = boardApiForHost_(exportTargetHost_);
    if (!currentExportApi_ && exportTargetHost_.compare(currentHost, Qt::CaseInsensitive) == 0)
        currentExportApi_ = boardApi_;
    if (deviceIdForHost_) exportTargetDeviceId_ = deviceIdForHost_(exportTargetHost_);
    if (exportTargetDeviceId_.trimmed().isEmpty()
        && exportTargetHost_.compare(currentHost, Qt::CaseInsensitive) == 0)
        exportTargetDeviceId_ = deviceId_;

    if (!currentExportApi_ || exportTargetDeviceId_.trimmed().isEmpty()) {
        exportFailed_++;
        if (eventExportStatus_)
            eventExportStatus_->setText(QStringLiteral("跳过 %1：该 IP 未在应用中添加或未授权 HTTP API").arg(exportTargetHost_));
        ++exportTargetIndex_;
        QTimer::singleShot(0, this, &Rv1126bDeviceManagementDialog::startNextEventExportTarget);
        return;
    }

    const QString runName = QStringLiteral("export_%1").arg(
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    exportTargetRoot_ = QDir(currentEventExportRoot()).filePath(
        QDir(safeSegment(exportTargetDeviceId_, safeSegment(exportTargetHost_, QStringLiteral("device"))))
            .filePath(runName));
    exportRunRoot_ = exportTargetRoot_;
    QDir().mkpath(exportRunRoot_);
    /*
     * 2026-10-07 事件级跳过：index.csv 每次导出都会追加"已导出事件的目录路径"，
     * 这里把其中的 event<ID>_track<ID> 读回来。已在集合里的事件，下面的
     * exportNextEvent() 会整条跳过 —— 连 getEventDetail 请求都不发。
     *
     * 上一版我只跳过了"下载文件"，但每条事件仍要一次 HTTP 往返才能算出目录名，
     * 所以点第二次还是把 951 条走一遍 —— 那是个错误的层级，这次改对了。
     */
    s_exportDoneKeys.clear();
    {
        QFile indexFile(QDir(exportRunRoot_).filePath(QStringLiteral("index.csv")));
        if (indexFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream stream(&indexFile);
            stream.setEncoding(QStringConverter::Utf8);
            const QRegularExpression doneKeyRe(QStringLiteral("_event(\\d+)_track(\\d+)"));
            while (!stream.atEnd()) {
                const QRegularExpressionMatch match = doneKeyRe.match(stream.readLine());
                if (match.hasMatch()) {
                    s_exportDoneKeys.insert(match.captured(1) + QLatin1Char(':') + match.captured(2));
                }
            }
        }
    }

    writeTextFile(QDir(exportRunRoot_).filePath(QStringLiteral("index.csv")),
                  QStringLiteral("device_id,host,event_id,track_id,event_time,plate,ocr_status,speed_kmh,direction,folder\n"));

    eventExportStatus_->setText(QStringLiteral("正在读取 %1 的事件列表...").arg(exportTargetHost_));
    requestEventExportPage();
}

void Rv1126bDeviceManagementDialog::requestEventExportPage(const std::optional<QString>& cursor)
{
    if (!exportInFlight_ || !currentExportApi_) return;
    currentExportApi_->listEvents(100, cursor, this, [this](rv1126b::ApiResult<rv1126b::EventPageDto> result) {
        handleEventExportPage(std::move(result));
    });
}

void Rv1126bDeviceManagementDialog::handleEventExportPage(rv1126b::ApiResult<rv1126b::EventPageDto> result)
{
    if (!exportInFlight_) return;
    if (!result) {
        exportFailed_++;
        finishEventExport();
        return;
    }
    const rv1126b::EventPageDto page = result.value();
    const QString range = eventExportRangeCombo_->currentData().toString();
    const bool latest100 = range == QStringLiteral("latest100");
    const bool recentDays = range == QStringLiteral("recent_days");
    const qint64 cutoffMs = QDateTime::currentDateTimeUtc().addDays(-eventExportDaysSpin_->value()).toMSecsSinceEpoch();
    bool reachedOldEvent = false;

    for (const rv1126b::EventSummaryDto& value : page.items) {
        if (recentDays && value.eventTime.epochMs > 0 && value.eventTime.epochMs < cutoffMs) {
            reachedOldEvent = true;
            continue;
        }
        exportEvents_.append(value);
        if (latest100 && exportEvents_.size() >= 100) break;
    }
    eventExportStatus_->setText(QStringLiteral("%1 已读取 %2 个事件...")
                                    .arg(exportTargetHost_)
                                    .arg(exportEvents_.size()));
    const bool latest100Done = latest100 && exportEvents_.size() >= 100;
    if (!latest100Done && !reachedOldEvent && page.hasMore && page.nextCursor.has_value()) {
        requestEventExportPage(page.nextCursor);
        return;
    }
    if (exportEvents_.isEmpty()) {
        finishEventExport();
        return;
    }
    exportNextEvent();
}


void Rv1126bDeviceManagementDialog::exportNextEvent()
{
    if (!exportInFlight_ || !currentExportApi_) return;
    if (exportEventIndex_ >= exportEvents_.size()) {
        finishEventExport();
        return;
    }
    /*
     * 2026-10-07：已经导出过的事件整条跳过 —— 不发 getEventDetail、不下载任何文件，
     * 所以"已经拉过一次"的第二次点击是瞬间完成。
     */
    while (exportEventIndex_ < exportEvents_.size()) {
        const rv1126b::EventSummaryDto candidate = exportEvents_.at(exportEventIndex_);
        const QString doneKey = QString::number(candidate.eventId)
            + QLatin1Char(':') + QString::number(candidate.trackId);
        if (!s_exportDoneKeys.contains(doneKey)) {
            break;
        }
        ++exportSucceeded_;
        ++exportEventIndex_;
    }
    if (exportEventIndex_ >= exportEvents_.size()) {
        finishEventExport();
        return;
    }

    const rv1126b::EventSummaryDto summary = exportEvents_.at(exportEventIndex_);
    rv1126b::EventIdentity identity;
    identity.deviceId = exportTargetDeviceId_;
    identity.eventId = summary.eventId;
    identity.trackId = summary.trackId;
    eventExportStatus_->setText(QStringLiteral("%1 正在导出第 %2/%3 个事件：event=%4 track=%5")
                                    .arg(exportTargetHost_)
                                    .arg(exportEventIndex_ + 1)
                                    .arg(exportEvents_.size())
                                    .arg(summary.eventId)
                                    .arg(summary.trackId));
    currentExportApi_->getEventDetail(identity, this,
        [this, summary](rv1126b::ApiResult<rv1126b::EventDetailDto> result) {
            handleExportEventDetail(summary, std::move(result));
        });
}

void Rv1126bDeviceManagementDialog::handleExportEventDetail(
    const rv1126b::EventSummaryDto& summary,
    rv1126b::ApiResult<rv1126b::EventDetailDto> result)
{
    if (!exportInFlight_) return;
    if (!result) {
        exportFailed_++;
        ++exportEventIndex_;
        exportNextEvent();
        return;
    }

    const rv1126b::EventDetailDto detail = result.value();
    const qint64 epochMs = detail.summary.eventTime.epochMs > 0
        ? detail.summary.eventTime.epochMs
        : (summary.eventTime.epochMs > 0 ? summary.eventTime.epochMs : QDateTime::currentMSecsSinceEpoch());
    const QDateTime eventTime = QDateTime::fromMSecsSinceEpoch(epochMs).toLocalTime();
    const QString dateDir = eventTime.toString(QStringLiteral("yyyy-MM-dd"));
    const QString plate = detail.summary.plateText.trimmed().isEmpty()
        ? (detail.summary.ocrStatus.rawValue.isEmpty()
               ? QStringLiteral("no_plate")
               : detail.summary.ocrStatus.rawValue)
        : detail.summary.plateText.trimmed();
    const QString folderName = safeSegment(
        QStringLiteral("%1_%4_event%2_track%3")
            .arg(eventTime.toString(QStringLiteral("yyyyMMdd_HHmmss")))
            .arg(detail.summary.eventId)
            .arg(detail.summary.trackId)
            .arg(plate),
        QStringLiteral("event"));
    const QString folder = QDir(exportRunRoot_).filePath(QDir(dateDir).filePath(folderName));
    QDir().mkpath(folder);

    const QJsonObject files = detail.rawJson.value(QStringLiteral("files")).toObject();
    const QJsonObject images = detail.rawJson.value(QStringLiteral("images")).toObject();
    exportFiles_.clear();
    exportFileIndex_ = 0;
    auto addDownload = [this, &folder](const QString& url, const QString& name) {
        if (url.trimmed().isEmpty()) return;
        ExportFile file;
        file.url = url.trimmed();
        file.finalPath = QDir(folder).filePath(name);
        file.partPath = file.finalPath + QStringLiteral(".part");
        file.optional = true;
        exportFiles_.append(file);
    };
    if (exportEvidenceCheck_->isChecked())
        addDownload(jsonString(files, QStringLiteral("evidence"), jsonString(images, QStringLiteral("evidence"))),
                    QStringLiteral("evidence.jpg"));
    if (exportSnapshotCheck_->isChecked())
        addDownload(jsonString(files, QStringLiteral("snapshot"), jsonString(images, QStringLiteral("snapshot"))),
                    QStringLiteral("snapshot.jpg"));
    if (exportFormalCheck_->isChecked())
        addDownload(jsonString(files, QStringLiteral("formal")), QStringLiteral("event.json"));
    if (exportOcrCheck_->isChecked())
        addDownload(jsonString(files, QStringLiteral("ocr")), QStringLiteral("ocr.json"));
    if (exportTrackMetaCheck_->isChecked())
        addDownload(QStringLiteral("/api/v1/events/%1/%2/files/track_meta")
                        .arg(detail.summary.eventId)
                        .arg(detail.summary.trackId),
                    QStringLiteral("debug/track_meta.json"));

    if (exportDetailCheck_->isChecked()) {
        writeTextFile(QDir(folder).filePath(QStringLiteral("detail.json")),
                      QString::fromUtf8(QJsonDocument(detail.rawJson).toJson(QJsonDocument::Indented)));
    }
    if (exportSummaryCheck_->isChecked()) {
        QString text;
        QTextStream stream(&text);
        stream.setEncoding(QStringConverter::Utf8);
        stream << "设备：" << exportTargetDeviceId_ << "\n";
        stream << "板端 IP：" << exportTargetHost_ << "\n";
        stream << "时间：" << eventTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) << "\n";
        stream << "事件ID：" << detail.summary.eventId << "\n";
        stream << "Track ID：" << detail.summary.trackId << "\n";
        stream << "车牌：" << (detail.summary.plateText.isEmpty() ? QStringLiteral("未识别") : detail.summary.plateText) << "\n";
        stream << "OCR状态：" << detail.summary.ocrStatus.rawValue << "\n";
        stream << "方向：" << detail.summary.motionDirection << "\n";
        stream << "速度：" << detail.summary.speedKmh << " km/h\n";
        stream << "证据图：" << (exportEvidenceCheck_->isChecked() ? QStringLiteral("evidence.jpg") : QStringLiteral("未导出")) << "\n";
        stream << "原始抓拍：" << (exportSnapshotCheck_->isChecked() ? QStringLiteral("snapshot.jpg") : QStringLiteral("未导出")) << "\n";
        writeTextFile(QDir(folder).filePath(QStringLiteral("summary.txt")), text);
    }

    QFile index(QDir(exportRunRoot_).filePath(QStringLiteral("index.csv")));
    if (index.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream stream(&index);
        stream.setEncoding(QStringConverter::Utf8);
        stream << csvEscape(exportTargetDeviceId_) << ','
               << csvEscape(exportTargetHost_) << ','
               << detail.summary.eventId << ','
               << detail.summary.trackId << ','
               << csvEscape(eventTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))) << ','
               << csvEscape(detail.summary.plateText) << ','
               << csvEscape(detail.summary.ocrStatus.rawValue) << ','
               << detail.summary.speedKmh << ','
               << csvEscape(detail.summary.motionDirection) << ','
               << csvEscape(QDir(exportRunRoot_).relativeFilePath(folder)) << '\n';
    }

    /*
     * 2026-10-07：只下载缺的文件，跳过已经有的。
     * 判据是"存在且非空"——所以崩在写一半的残缺文件不会被误认为已完成；
     * 全部文件都在时整条事件跳过、一个字节都不下载，重复拉取第二次几乎瞬间完成。
     * 之前崩溃留下的那批只有 summary.txt 的目录，会因为缺图/缺 JSON 在这里被自动补全。
     */
    {
        QVector<ExportFile> missingFiles;
        for (const ExportFile& file : std::as_const(exportFiles_)) {
            const QFileInfo fileInfo(file.finalPath);
            if (fileInfo.exists() && fileInfo.size() > 0) {
                continue;
            }
            missingFiles.append(file);
        }
        exportFiles_ = missingFiles;
    }
    if (exportFiles_.isEmpty()) {
        ++exportSucceeded_;
        ++exportEventIndex_;
        exportNextEvent();
        return;
    }
    downloadNextExportFile();

}

void Rv1126bDeviceManagementDialog::downloadNextExportFile()
{
    if (!exportInFlight_ || !currentExportApi_) return;
    if (exportFileIndex_ >= exportFiles_.size()) {
        exportSucceeded_++;
        ++exportEventIndex_;
        exportNextEvent();
        return;
    }
    const ExportFile file = exportFiles_.at(exportFileIndex_);
    QFile::remove(file.partPath);
    currentExportApi_->downloadFileToPartFile(file.url, file.partPath, this,
        [this](rv1126b::ApiResult<rv1126b::EvidenceDownloadResult> result) {
            handleExportFileDownloaded(std::move(result));
        });
}

void Rv1126bDeviceManagementDialog::handleExportFileDownloaded(
    rv1126b::ApiResult<rv1126b::EvidenceDownloadResult> result)
{
    if (!exportInFlight_) return;
    const ExportFile file = exportFiles_.value(exportFileIndex_);
    if (result) {
        QFile::remove(file.finalPath);
        if (!QDir().mkpath(QFileInfo(file.finalPath).absolutePath())
            || !QFile::rename(file.partPath, file.finalPath)) {
            exportFailed_++;
        }
    } else {
        QFile::remove(file.partPath);
        exportFailed_++;
    }
    exportFileIndex_++;
    downloadNextExportFile();
}

void Rv1126bDeviceManagementDialog::finishEventExport()
{
    if (eventExportStatus_) {
        eventExportStatus_->setText(QStringLiteral("%1 完成：本设备事件 %2 个，累计失败/缺失文件 %3")
                                        .arg(exportTargetHost_)
                                        .arg(exportEvents_.size())
                                        .arg(exportFailed_));
    }
    ++exportTargetIndex_;
    QTimer::singleShot(0, this, &Rv1126bDeviceManagementDialog::startNextEventExportTarget);
}

void Rv1126bDeviceManagementDialog::refreshIspConfig()
{
    if (!boardApi_) {
        if (ispStatus_) ispStatus_->setText(QStringLiteral("当前设备没有可用的 HTTP API"));
        return;
    }
    if (ispStatus_) ispStatus_->setText(QStringLiteral("正在读取图像参数..."));
    boardApi_->getIspConfig(this, [this](rv1126b::ApiResult<QJsonObject> result) {
        if (!result) {
            if (ispStatus_) ispStatus_->setText(QStringLiteral("读取失败：%1").arg(boardErrorText(result.error())));
            return;
        }
        applyIspConfigJson(result.value(), IspRefresh);
    });
}

void Rv1126bDeviceManagementDialog::saveCurrentIspConfig()
{
    if (!boardApi_) return;
    if (QMessageBox::question(this, QStringLiteral("保存当前图像参数"),
                              QStringLiteral("确认把相机当前运行中的曝光/增益/图像参数保存为开机默认？\n"
                                             "写入后要重启相机服务（或断电重启）才会套用，重启前画面不会立刻变化。"))
        != QMessageBox::Yes) return;
    if (ispStatus_) ispStatus_->setText(QStringLiteral("正在保存当前图像参数..."));
    boardApi_->saveCurrentIspConfig(this, [this](rv1126b::ApiResult<QJsonObject> result) {
        if (!result) {
            if (ispStatus_) ispStatus_->setText(QStringLiteral("保存失败：%1").arg(boardErrorText(result.error())));
            return;
        }
        applyIspConfigJson(result.value(), IspSaveCurrent);
    });
}

void Rv1126bDeviceManagementDialog::clearIspConfig()
{
    if (!boardApi_) return;
    if (QMessageBox::question(this, QStringLiteral("取消开机默认覆盖"),
                              QStringLiteral("确认取消本软件对图像参数的开机覆盖？\n"
                                             "取消后开机不再套用这组参数；"
                                             "已经写进板端配置文件的当前值不会被还原成出厂默认。"))
        != QMessageBox::Yes) return;
    if (ispStatus_) ispStatus_->setText(QStringLiteral("正在取消开机默认覆盖..."));
    boardApi_->clearIspConfig(this, [this](rv1126b::ApiResult<QJsonObject> result) {
        if (!result) {
            if (ispStatus_) ispStatus_->setText(QStringLiteral("取消失败：%1").arg(boardErrorText(result.error())));
            return;
        }
        applyIspConfigJson(result.value(), IspClear);
    });
}

void Rv1126bDeviceManagementDialog::applyIspConfigJson(const QJsonObject& config, int action)
{
    // 板端返回的是 close/open/auto 这类英文枚举，这里统一翻成现场看得懂的中文。
    const auto onOff = [](const QString& value) {
        if (value.compare(QStringLiteral("close"), Qt::CaseInsensitive) == 0
            || value.compare(QStringLiteral("closed"), Qt::CaseInsensitive) == 0
            || value.compare(QStringLiteral("off"), Qt::CaseInsensitive) == 0
            || value == QStringLiteral("0")) {
            return QStringLiteral("关");
        }
        if (value.compare(QStringLiteral("open"), Qt::CaseInsensitive) == 0
            || value.compare(QStringLiteral("on"), Qt::CaseInsensitive) == 0) {
            return QStringLiteral("开");
        }
        return value.isEmpty() ? QStringLiteral("未读取") : value;
    };
    const auto modeText = [](const QString& value) {
        if (value.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0) {
            return QStringLiteral("自动");
        }
        if (value.compare(QStringLiteral("manual"), Qt::CaseInsensitive) == 0) {
            return QStringLiteral("手动");
        }
        return value.isEmpty() ? QStringLiteral("未读取") : value;
    };
    const auto levelText = [&onOff](const QString& state, const QString& level) {
        if (state.isEmpty() && level.isEmpty()) {
            return QStringLiteral("未读取");
        }
        if (state.compare(QStringLiteral("close"), Qt::CaseInsensitive) == 0) {
            return QStringLiteral("关");
        }
        return QStringLiteral("%1（档位 %2）").arg(onOff(state),
                                                 level.isEmpty() ? QStringLiteral("-") : level);
    };
    const auto textOrUnread = [](const QJsonObject& object, const char* key) {
        const QString value = object.value(QLatin1String(key)).toString();
        return value.isEmpty() ? QStringLiteral("未读取") : value;
    };

    const auto summary = [&](const QJsonObject& object, bool includeEnabled) {
        QString text = QStringLiteral("快门：%1（%2）；增益：%3（%4）；亮度：%5；对比度：%6；"
                                      "强光抑制：%7；背光补偿：%8；高动态 HDR：%9；宽动态 WDR：%10")
                           .arg(textOrUnread(object, "exposure_time"),
                                modeText(object.value(QStringLiteral("exposure_mode")).toString()),
                                textOrUnread(object, "exposure_gain"),
                                modeText(object.value(QStringLiteral("gain_mode")).toString()),
                                textOrUnread(object, "brightness"),
                                textOrUnread(object, "contrast"),
                                levelText(object.value(QStringLiteral("hlc")).toString(),
                                          object.value(QStringLiteral("hlc_level")).toString()),
                                levelText(object.value(QStringLiteral("blc_region")).toString(),
                                          object.value(QStringLiteral("blc_strength")).toString()),
                                levelText(object.value(QStringLiteral("hdr")).toString(),
                                          object.value(QStringLiteral("hdr_level")).toString()),
                                levelText(object.value(QStringLiteral("wdr")).toString(),
                                          object.value(QStringLiteral("wdr_level")).toString()));
        if (includeEnabled) {
            text.prepend(QStringLiteral("开机套用：%1；")
                             .arg(object.value(QStringLiteral("enabled")).toBool()
                                      ? QStringLiteral("开")
                                      : QStringLiteral("关")));
        }
        return text;
    };

    QJsonObject current = config.value(QStringLiteral("current_live")).toObject();
    const bool liveAvailable = config.value(QStringLiteral("live_available")).toBool(true);
    const QString sourceRaw = config.value(QStringLiteral("current_source")).toString();
    QString sourceText = QStringLiteral("未知来源");
    if (sourceRaw == QStringLiteral("vendor_cgi")) {
        sourceText = QStringLiteral("实时值（直接读自相机）");
    } else if (sourceRaw == QStringLiteral("rkipc_ini")) {
        sourceText = QStringLiteral("配置文件值（rkipc 启动时读取，不是实时值）");
    }
    if (current.isEmpty()) current = config.value(QStringLiteral("current")).toObject();
    if (current.isEmpty()) current = config.value(QStringLiteral("current_ini")).toObject();
    const QJsonObject persisted = config.value(QStringLiteral("persisted")).toObject();
    // "当前运行"一行不显示开机套用开关：板端实时读取路径里它恒为 true，显示只会误导。
    if (ispCurrentLabel_) ispCurrentLabel_->setText(summary(current, false));
    if (ispPersistedLabel_) ispPersistedLabel_->setText(summary(persisted, true));

    // 板端关闭配置写入时，保存/取消必然 403，按钮直接置灰。
    const bool writeEnabled = config.value(QStringLiteral("write_enabled")).toBool(true);
    if (ispSaveCurrentButton_) ispSaveCurrentButton_->setEnabled(writeEnabled);
    if (ispClearButton_) ispClearButton_->setEnabled(writeEnabled);
    if (ispSaveCurrentButton_) {
        ispSaveCurrentButton_->setToolTip(writeEnabled
            ? QString()
            : QStringLiteral("板端已关闭配置写入，无法保存"));
    }

    if (!ispStatus_) {
        return;
    }
    QStringList lines;
    if (action == IspSaveCurrent) {
        lines << QStringLiteral("已把这组参数保存为开机默认");
    } else if (action == IspClear) {
        lines << QStringLiteral("已取消开机默认覆盖（开机不再套用这组参数）");
    }
    if (!writeEnabled) {
        lines << QStringLiteral("板端已关闭配置写入，保存/取消按钮不可用");
    }
    lines << QStringLiteral("配置版本：%1")
                 .arg(config.value(QStringLiteral("revision")).toString(QStringLiteral("未读取")));
    lines << QStringLiteral("数值来源：%1").arg(sourceText);
    if (!liveAvailable) {
        const QString liveError = config.value(QStringLiteral("live_error")).toString();
        lines << QStringLiteral("实时值读取失败，上面「当前运行」显示的是配置文件里的值%1")
                     .arg(liveError.isEmpty() ? QString() : QStringLiteral("（%1）").arg(liveError));
    }
    lines << (config.value(QStringLiteral("restart_required")).toBool()
                  ? QStringLiteral("已在板端落盘，重启相机服务或断电重启后生效（本次重启前画面不变）")
                  : QStringLiteral("板端已保存的值与本次读取一致，无需重启"));
    ispStatus_->setText(lines.join(QStringLiteral("；")));
}

void Rv1126bDeviceManagementDialog::browseEventSyncTargetFolder()
{
    const QString start = eventSyncTargetEdit_ ? eventSyncTargetEdit_->text().trimmed() : QString();
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择拉取的目标文件夹"), start);
    if (!dir.isEmpty() && eventSyncTargetEdit_) {
        eventSyncTargetEdit_->setText(QDir::toNativeSeparators(dir));
    }
}

void Rv1126bDeviceManagementDialog::applyEventSyncTargetFolder()
{
    if (!eventSyncTargetEdit_ || !boardPullStatus_) {
        return;
    }
    const QString path = eventSyncTargetEdit_->text().trimmed();
    if (path.isEmpty()) {
        boardPullStatus_->setText(QStringLiteral("目标文件夹不能为空（留空即用默认目录时请点“重新读取”）"));
        return;
    }
    if (!detectionPull_.writer) {
        boardPullStatus_->setText(QStringLiteral("本设备没有装配检测结果写入器，无法设置目标"));
        return;
    }
    /*
     * 本地路径与 UNC（\\对方IP\共享名\子目录）在这里一视同仁：交给 writer 去
     * 创建/探测。失败就把原因显示出来 —— 绝不能"设置看起来成功了、数据其实没落地"。
     */
    detectionPull_.writer->setTargetRoot(path);
    QString errorMessage;
    if (detectionPull_.writer->ensureTargetRoot(&errorMessage)) {
        boardPullStatus_->setText(QStringLiteral("目标文件夹：%1").arg(path));
    } else {
        boardPullStatus_->setText(QStringLiteral("目标不可用：%1").arg(errorMessage));
    }
}

void Rv1126bDeviceManagementDialog::startBoardDataPull()
{
    if (!boardPullService_ || !boardApi_ || boardPullService_->isRunning()) {
        return;
    }
    /*
     * 时间范围（2026-10-07）：0..3 => 今天 / 最近一周 / 最近一月 / 全部。
     * “今天”从本地当天 00:00 起算；一周/一月从当前时刻往前推；全部 = 不设下界。
     */
    qint64 cutoffEpochMs = 0;
    QString rangeLabel = QStringLiteral("全部");
    switch (boardPullRangeCombo_ ? boardPullRangeCombo_->currentIndex() : 3) {
    case 0: {
        const QDateTime startOfToday(QDate::currentDate(), QTime(0, 0));
        cutoffEpochMs = startOfToday.toMSecsSinceEpoch();
        rangeLabel = QStringLiteral("今天");
        break;
    }
    case 1:
        cutoffEpochMs = QDateTime::currentDateTime().toMSecsSinceEpoch() - 7LL * 86400000LL;
        rangeLabel = QStringLiteral("最近一周");
        break;
    case 2:
        cutoffEpochMs = QDateTime::currentDateTime().toMSecsSinceEpoch() - 30LL * 86400000LL;
        rangeLabel = QStringLiteral("最近一月");
        break;
    case 3:
    default:
        cutoffEpochMs = 0;
        rangeLabel = QStringLiteral("全部");
        break;
    }

    if (QMessageBox::question(
            this, QStringLiteral("拉取板端数据"),
            QStringLiteral("确认把这台相机上已有的检测记录拉回本机？\n"
                           "范围：%1（最多 5000 条）。\n"
                           "目标文件夹：%2\n"
                           "已存在的同类文件会被覆盖，目录结构不会动。")
                .arg(rangeLabel, boardPullStatus_ ? boardPullStatus_->text() : QString()))
        != QMessageBox::Yes) {
        return;
    }
    boardPullButton_->setText(QStringLiteral("停止拉取"));
    boardPullStatus_->setText(QStringLiteral("正在读取板端事件列表（范围：%1）...").arg(rangeLabel));
    /*
     * 2026-10-07: merge.  "Start pull to this PC" now drives the event-export
     * machinery, which already downloads evidence / snapshot / event JSON / OCR JSON
     * / detail JSON / summary for the chosen range into currentEventExportRoot()
     * (which honours the target folder above).  The range is taken from the pull row
     * so both buttons agree on "today / last week / last month / all".
     */
    if (eventExportRangeCombo_) {
        if (cutoffEpochMs <= 0) {
            const int idx = eventExportRangeCombo_->findData(QStringLiteral("all"));
            if (idx >= 0) eventExportRangeCombo_->setCurrentIndex(idx);
        } else {
            const int idx = eventExportRangeCombo_->findData(QStringLiteral("recent_days"));
            if (idx >= 0) eventExportRangeCombo_->setCurrentIndex(idx);
            const qint64 days = qMax<qint64>(1, (QDateTime::currentMSecsSinceEpoch() - cutoffEpochMs) / 86400000LL);
            if (eventExportDaysSpin_) eventExportDaysSpin_->setValue(static_cast<int>(qMin<qint64>(days, 3650)));
        }
    }
    startEventExport();
}

void Rv1126bDeviceManagementDialog::cancelBoardDataPull()
{
    if (boardPullService_ && boardPullService_->isRunning()) {
        boardPullService_->cancel();
    }
}

void Rv1126bDeviceManagementDialog::handleBoardDataPullProgress(int seen, int written, int failed)
{
    if (!boardPullStatus_) return;
    boardPullStatus_->setText(QStringLiteral("已读取 %1 条 · 已写入 %2 条 · 失败 %3 条")
                                  .arg(seen)
                                  .arg(written)
                                  .arg(failed));
}

void Rv1126bDeviceManagementDialog::handleBoardDataPullFinished(int seen, int written, int failed,
                                                               bool cancelled)
{
    if (boardPullButton_) {
        boardPullButton_->setText(QStringLiteral("开始拉取到本机"));
    }
    if (!boardPullStatus_) return;
    boardPullStatus_->setText(
        cancelled
            ? QStringLiteral("已停止：读取 %1 条，写入 %2 条").arg(seen).arg(written)
            : QStringLiteral("拉取完成：读取 %1 条，写入 %2 条，失败 %3 条").arg(seen).arg(written).arg(failed));
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

QString Rv1126bDeviceManagementDialog::defaultEventStorageRoot() const
{
    if (!storageRootPath_.trimmed().isEmpty()) return QDir::cleanPath(storageRootPath_.trimmed());
    if (!evidenceRootPath_.trimmed().isEmpty()) {
        QDir root(evidenceRootPath_);
        root.cdUp();
        root.cdUp();
        return QDir::cleanPath(root.absolutePath());
    }
    return QDir::cleanPath(QDir::home().filePath(QStringLiteral("RV_CAM_DATA")));
}

QString Rv1126bDeviceManagementDialog::currentEventStorageRoot() const
{
    const QString text = eventStorageRootEdit_ ? eventStorageRootEdit_->text().trimmed() : QString();
    return QDir::cleanPath(text.isEmpty() ? defaultEventStorageRoot() : text);
}

QString Rv1126bDeviceManagementDialog::currentEventExportRoot() const
{
    // 2026-10-07: honour the target folder (local path, or \\host\share for another
    // PC) when it is set, so pull and export write to the same place.
    if (eventSyncTargetEdit_) {
        const QString target = eventSyncTargetEdit_->text().trimmed();
        if (!target.isEmpty()) {
            return QDir::cleanPath(target);
        }
    }
    return QDir::cleanPath(QDir(currentEventStorageRoot()).filePath(QStringLiteral("rv1126b/exports/events")));
}

QStringList Rv1126bDeviceManagementDialog::eventExportHosts() const
{
    QString text = eventSyncHostsEdit_ ? eventSyncHostsEdit_->text() : QString();
    if (text.trimmed().isEmpty()) text = deviceEndpointText_.section(QLatin1Char(':'), 0, 0);
    QStringList values = text.split(QRegularExpression(QStringLiteral(R"([,;，；\s]+)")),
                                    Qt::SkipEmptyParts);
    QStringList hosts;
    for (QString value : values) {
        value = value.trimmed();
        if (value.startsWith(QStringLiteral("http://"))) value = QUrl(value).host();
        if (value.contains(QLatin1Char(':'))) value = value.section(QLatin1Char(':'), 0, 0);
        if (!value.isEmpty() && !hosts.contains(value, Qt::CaseInsensitive)) hosts.append(value);
    }
    return hosts;
}

void Rv1126bDeviceManagementDialog::browseEventStorageRoot()
{
    const QString path = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择本地存储根目录"), currentEventStorageRoot());
    if (!path.isEmpty()) eventStorageRootEdit_->setText(QDir::cleanPath(path));
}

void Rv1126bDeviceManagementDialog::saveEventStorageRoot()
{
    const QString root = currentEventStorageRoot();
    if (root.trimmed().isEmpty()) {
        showError(QStringLiteral("invalid_storage_root"), QStringLiteral("本地存储根目录不能为空"));
        return;
    }
    if (!QDir().mkpath(root)) {
        showError(QStringLiteral("storage_root_unwritable"), QStringLiteral("无法创建本地存储根目录"));
        return;
    }
    if (storageRootChangeHandler_ && !storageRootChangeHandler_(root)) {
        showError(QStringLiteral("storage_root_save_failed"), QStringLiteral("保存本地存储根目录失败"));
        return;
    }
    storageRootPath_ = root;
    evidenceRootPath_ = QDir(root).filePath(QStringLiteral("rv1126b/events"));
    if (eventSyncRootLabel_) eventSyncRootLabel_->setText(evidenceRootPath_);
    if (eventExportStatus_ && !exportInFlight_)
        eventExportStatus_->setText(QStringLiteral("导出目录：%1").arg(currentEventExportRoot()));
    eventSyncStatus_->setText(QStringLiteral("本地存储根目录已保存"));
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
