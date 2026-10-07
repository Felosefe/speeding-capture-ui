#include "Rv1126bDeviceManagementDialog.h"

#include "../rv1126b/services/BoardDataPullService.h"
#include "../rv1126b/services/DetectionResultWriter.h"

#include "../rv1126b/ports/IBoardApiClient.h"
#include "../rv1126b/services/EventSyncService.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>
#include <QTimeZone>
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
    tabs_->setCurrentIndex(static_cast<int>(initialPage));
    layout->addWidget(tabs_, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

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
        globalMessage_->setText(QStringLiteral("设备离线：无法读取板端配置，事件同步需要设备在线"));
    }
}

Rv1126bDeviceManagementDialog::~Rv1126bDeviceManagementDialog()
{
    if (controller_) controller_->cancelPending();
}

void Rv1126bDeviceManagementDialog::reject()
{
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
    // 2026-10-07：状态行「已同步 N 条 · 最后同步时间」
    eventSyncCountLabel_ = new QLabel(QStringLiteral("已同步：尚未同步"), page);
    eventSyncCountLabel_->setObjectName(QStringLiteral("eventSyncCountLabel"));
    eventSyncCountLabel_->setStyleSheet(QStringLiteral("color:#44515f;"));
    layout->addWidget(eventSyncCountLabel_);

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
    /*
     * 2026-10-07：持续同步（用户要的形态）。
     * 打开后，板端每同步到一条新事件，DetectionResultWriter 的 auto 模式就会往
     * 上面那个文件夹写一整套（evidence.jpg / event.json / detail.json / ocr.json /
     * summary.txt），不需要点任何按钮；配合"目录名跨运行稳定"，重复执行只会补缺、
     * 不会重建。目标文件夹沿用上面的"目标电脑"，所以本机和 \\对方IP\共享名 都支持。
     */
    auto* autoWriteCheck = new QCheckBox(
        QStringLiteral("持续同步到上面这个文件夹（板端每出一条检测结果就写一套）"), pullBox);
    autoWriteCheck->setChecked(true);
    connect(autoWriteCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (!detectionPull_.writer) {
            return;
        }
        detectionPull_.writer->setAutoEnabled(on);
        if (eventSyncTargetEdit_) {
            detectionPull_.writer->setTargetRoot(eventSyncTargetEdit_->text().trimmed());
        }
        if (boardPullStatus_) {
            boardPullStatus_->setText(on
                ? QStringLiteral("持续同步：开 —— 新事件会自动写入上面这个文件夹")
                : QStringLiteral("持续同步：关 —— 只保留手动「开始拉取到本机」"));
        }
    });
    pullLayout->addWidget(autoWriteCheck);
    if (detectionPull_.writer) {
        detectionPull_.writer->setAutoEnabled(true);
        if (eventSyncTargetEdit_) {
            detectionPull_.writer->setTargetRoot(eventSyncTargetEdit_->text().trimmed());
        }
    }


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
    /*
     * 2026-10-07: the ISP / FTP-config / FTP-tasks pages and their board callbacks
     * (ftpConfigLoaded, ftpConfigRolledBack, ftpActivationFinished, ftpControlLoaded,
     * ftpControlSaved, ftpRevisionConflict, ftpTasksLoaded, ftpTaskLoaded,
     * ftpTaskCreated, ftpTaskRetried, localFtpTaskSnapshotsLoaded,
     * operationBusyChanged) are gone.  Nothing in this dialog binds to them anymore.
     */
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
        if (boardPullButton_) boardPullButton_->setText(QStringLiteral("开始拉取到本机"));

        if (eventSyncCountLabel_) {
            eventSyncCountLabel_->setText(QStringLiteral("已同步 %1 条 · 最后同步 %2")
                                              .arg(exportSucceeded_)
                                              .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))));
        }

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

    /*
     * 2026-10-07: NO per-run folder any more.
     * This used to be  <exportRoot>/<device>/export_<yyyyMMdd_HHmmss>/  which meant
     * every pull created a brand new folder, its index.csv always started empty, and
     * the event-level skip could never recognise an already-pulled event - the user
     * saw "another folder" on every pull and nothing was ever skipped.
     * One stable folder per device instead:  <exportRoot>/<device>/
     * so index.csv accumulates across runs and "already pulled" is really recognised.
     */
    exportTargetRoot_ = QDir(currentEventExportRoot()).filePath(
        safeSegment(exportTargetDeviceId_, safeSegment(exportTargetHost_, QStringLiteral("device"))));
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
    // 2026-10-07: same readable shape as the writer: time_plate_event.._track..
    const QString plateText = detail.summary.plateText.trimmed().isEmpty()
                                  ? QStringLiteral("无牌")
                                  : detail.summary.plateText.trimmed();
    const QString folderName = safeSegment(
        QStringLiteral("%1_%2_event%3_track%4")
            .arg(eventTime.toString(QStringLiteral("yyyyMMdd_HHmmss")), plateText)
            .arg(detail.summary.eventId)
            .arg(detail.summary.trackId),
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
    // 唯一的目标就是设备端点里的 host（原来那个 HTTP hosts 输入框已经删掉了）。
    const QString text = deviceEndpointText_.section(QLatin1Char(':'), 0, 0);
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
