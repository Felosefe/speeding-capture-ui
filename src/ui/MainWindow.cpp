#include "MainWindow.h"

#include "../models/table_models/CaptureRecordTableModel.h"
#include "../models/table_models/DevicePropertyModel.h"
#include "../models/table_models/DeviceTableModel.h"
#include "../services/CaptureRecordService.h"
#include "../services/DeviceManager.h"
#include "../services/MaintenanceController.h"
#include "../services/SystemSettingsService.h"
#include "../video/VideoWidget.h"
#include "DeviceConfigDialog.h"
#include "SystemSettingsDialog.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPoint>
#include <QProcess>
#include <QSize>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QSplitter>
#include <QSettings>
#include <QStatusBar>
#include <QStorageInfo>
#include <QStyle>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <optional>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , deviceManager_(new DeviceManager(this))
    , captureService_(new CaptureRecordService(this))
    , systemSettingsService_(new SystemSettingsService(this))
    , maintenanceController_(new MaintenanceController(this))
    , deviceModel_(new DeviceTableModel(this))
    , propertyModel_(new DevicePropertyModel(this))
    , captureModel_(new CaptureRecordTableModel(this))
    , currentSystemSettings_(systemSettingsService_->load())
    , storageService_(currentSystemSettings_.storage)
{
    setWindowTitle(QStringLiteral("车牌识别雷达测速摄像机管理软件"));
    resize(1360, 820);
    setMinimumSize(1024, 680);

    createActions();
    createToolBar();
    createCentralLayout();
    createDeviceContextMenu();
    createStatusBar();
    applySystemSettings(true);

    if (!captureService_->initialize()) {
        statusBar()->showMessage(QStringLiteral("抓拍记录库初始化失败：%1").arg(captureService_->lastError()), 5000);
    } else {
        refreshCaptureRecords();
    }

    connectDeviceManager();
    populateInitialData();

    if (currentSystemSettings_.ui.autoConnectOnStart) {
        const int connected = deviceManager_->connectAllDevices();
        statusBar()->showMessage(QStringLiteral("已自动连接 %1 台设备").arg(connected), 2500);
    }

    configureMaintenanceController();
    maintenanceController_->start();
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() != QEvent::WindowStateChange || !livePreview_ || !snapshotPreview_) {
        return;
    }

    const bool stopVideo = currentSystemSettings_.ui.stopVideoQueryWhenMinimized && isMinimized();
    livePreview_->setUpdatesEnabled(!stopVideo);
    snapshotPreview_->setUpdatesEnabled(!stopVideo);
}

void MainWindow::createActions()
{
    const auto icon = [this](QStyle::StandardPixmap pixmap) {
        return style()->standardIcon(pixmap);
    };

    addDeviceAction_ = new QAction(icon(QStyle::SP_FileDialogNewFolder), QStringLiteral("添加设备"), this);
    connect(addDeviceAction_, &QAction::triggered, this, &MainWindow::addDevice);

    connectAction_ = new QAction(icon(QStyle::SP_DialogApplyButton), QStringLiteral("连接"), this);
    connect(connectAction_, &QAction::triggered, this, &MainWindow::connectSelectedDevice);

    disconnectAction_ = new QAction(icon(QStyle::SP_DialogCancelButton), QStringLiteral("断开"), this);
    connect(disconnectAction_, &QAction::triggered, this, &MainWindow::disconnectSelectedDevice);

    configAction_ = new QAction(icon(QStyle::SP_FileDialogDetailedView), QStringLiteral("设备配置"), this);
    connect(configAction_, &QAction::triggered, this, &MainWindow::openDeviceConfig);

    captureAction_ = new QAction(icon(QStyle::SP_ComputerIcon), QStringLiteral("手动抓拍"), this);
    connect(captureAction_, &QAction::triggered, this, &MainWindow::triggerCapture);

    rebootAction_ = new QAction(icon(QStyle::SP_BrowserReload), QStringLiteral("重启相机"), this);
    connect(rebootAction_, &QAction::triggered, this, &MainWindow::rebootSelectedDevice);

    syncTimeAction_ = new QAction(icon(QStyle::SP_DialogApplyButton), QStringLiteral("同步时间"), this);
    connect(syncTimeAction_, &QAction::triggered, this, &MainWindow::syncSelectedDeviceTime);

    deleteCaptureAction_ = new QAction(icon(QStyle::SP_TrashIcon), QStringLiteral("删除记录"), this);
    connect(deleteCaptureAction_, &QAction::triggered, this, &MainWindow::deleteSelectedCapture);

    clearCaptureAction_ = new QAction(icon(QStyle::SP_DialogResetButton), QStringLiteral("清空记录"), this);
    clearCaptureAction_->setEnabled(false);
    connect(clearCaptureAction_, &QAction::triggered, this, &MainWindow::clearCaptureRecords);

    globalSettingsAction_ = new QAction(icon(QStyle::SP_FileDialogContentsView), QStringLiteral("全局设置"), this);
    connect(globalSettingsAction_, &QAction::triggered, this, &MainWindow::openGlobalSettings);

    refreshAction_ = new QAction(icon(QStyle::SP_BrowserReload), QStringLiteral("刷新记录"), this);
    connect(refreshAction_, &QAction::triggered, this, &MainWindow::refreshCaptureRecords);

    exportAction_ = new QAction(icon(QStyle::SP_DriveFDIcon), QStringLiteral("导出记录"), this);
    connect(exportAction_, &QAction::triggered, this, &MainWindow::exportCaptureRecords);
}

void MainWindow::createToolBar()
{
    auto* toolbar = addToolBar(QStringLiteral("主工具栏"));
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(20, 20));
    toolbar->addAction(addDeviceAction_);
    toolbar->addAction(connectAction_);
    toolbar->addAction(disconnectAction_);
    toolbar->addSeparator();
    toolbar->addAction(configAction_);
    toolbar->addAction(captureAction_);
    toolbar->addAction(rebootAction_);
    toolbar->addAction(syncTimeAction_);
    toolbar->addSeparator();
    toolbar->addAction(deleteCaptureAction_);
    toolbar->addSeparator();
    toolbar->addAction(refreshAction_);
    toolbar->addAction(exportAction_);
    toolbar->addSeparator();
    toolbar->addAction(globalSettingsAction_);
}

void MainWindow::createCentralLayout()
{
    deviceTable_ = createDeviceTable();
    propertyTable_ = createPropertyTable();
    QWidget* capturePanel = createCaptureRecordPanel();
    livePreview_ = new VideoWidget(VideoWidget::Mode::Live, this);
    snapshotPreview_ = new VideoWidget(VideoWidget::Mode::Snapshot, this);

    auto* leftSplitter = new QSplitter(Qt::Vertical, this);
    leftSplitter->addWidget(deviceTable_);
    leftSplitter->addWidget(propertyTable_);
    leftSplitter->setStretchFactor(0, 3);
    leftSplitter->setStretchFactor(1, 2);

    previewSplitter_ = new QSplitter(Qt::Horizontal, this);
    previewSplitter_->addWidget(livePreview_);
    previewSplitter_->addWidget(snapshotPreview_);
    previewSplitter_->setStretchFactor(0, 3);
    previewSplitter_->setStretchFactor(1, 2);

    auto* rightSplitter = new QSplitter(Qt::Vertical, this);
    rightSplitter->addWidget(previewSplitter_);
    rightSplitter->addWidget(capturePanel);
    rightSplitter->setStretchFactor(0, 5);
    rightSplitter->setStretchFactor(1, 2);

    auto* mainSplitter = new QSplitter(Qt::Horizontal, this);
    mainSplitter->addWidget(leftSplitter);
    mainSplitter->addWidget(rightSplitter);
    mainSplitter->setStretchFactor(0, 1);
    mainSplitter->setStretchFactor(1, 4);
    mainSplitter->setCollapsible(0, false);
    mainSplitter->setCollapsible(1, false);

    setCentralWidget(mainSplitter);
}

void MainWindow::createStatusBar()
{
    statusLabel_ = new QLabel(this);
    statusBar()->addWidget(statusLabel_, 1);
    statusBar()->showMessage(QStringLiteral("视频预览系统已启动"));
    updateStatusText();
}

void MainWindow::createDeviceContextMenu()
{
    deviceContextMenu_ = new QMenu(this);
    deviceContextMenu_->addAction(connectAction_);
    deviceContextMenu_->addAction(disconnectAction_);
    deviceContextMenu_->addSeparator();
    deviceContextMenu_->addAction(configAction_);
    deviceContextMenu_->addAction(captureAction_);
    deviceContextMenu_->addSeparator();
    deviceContextMenu_->addAction(rebootAction_);
    deviceContextMenu_->addAction(syncTimeAction_);
    deviceContextMenu_->addAction(QStringLiteral("网络信息"), this, &MainWindow::showActionMessage);
    deviceContextMenu_->addAction(QStringLiteral("打开本地文件夹"), this, &MainWindow::showActionMessage);
}

void MainWindow::connectDeviceManager()
{
    connect(deviceManager_, &DeviceManager::deviceAdded, this, &MainWindow::handleDeviceAdded);
    connect(deviceManager_, &DeviceManager::deviceStatusChanged, this, &MainWindow::handleDeviceStatusChanged);
    connect(deviceManager_, &DeviceManager::deviceConfigChanged, this, &MainWindow::handleDeviceConfigChanged);
    connect(deviceManager_, &DeviceManager::captureGenerated, this, &MainWindow::handleCaptureGenerated);
    connect(deviceManager_, &DeviceManager::errorOccurred, this, &MainWindow::handleDeviceError);
}

void MainWindow::populateInitialData()
{
    deviceManager_->seedMockDevices(3);
    selectDeviceRow(0);
    updateDeviceProperties();
    updateStatusText();
}

void MainWindow::updateStatusText()
{
    const int visibleOrPendingCaptureCount = captureModel_->recordCount() + pendingCaptureCount_;
    statusLabel_->setText(QStringLiteral("就绪 | 设备 %1 台 | 在线 %2 台 | 抓拍记录 %3 条")
                              .arg(deviceModel_->deviceCount())
                              .arg(deviceModel_->onlineCount())
                              .arg(visibleOrPendingCaptureCount));
}

void MainWindow::selectDeviceRow(int row)
{
    if (row < 0 || row >= deviceModel_->rowCount()) {
        return;
    }

    deviceTable_->selectRow(row);
}

void MainWindow::applySystemSettings(bool initialApply)
{
    QApplication::setFont(QFont(currentSystemSettings_.ui.fontFamily, currentSystemSettings_.ui.fontPointSize));
    storageService_.setSettings(currentSystemSettings_.storage);

    VideoDisplayOptions videoOptions;
    videoOptions.previewFrameRate = currentSystemSettings_.ui.previewFrameRate;
    videoOptions.showOnlyVehicleFrames = currentSystemSettings_.ui.showOnlyVehicleFrames;
    videoOptions.overlaySpeed = currentSystemSettings_.ui.overlaySpeed;
    videoOptions.showCalibrationLines = currentSystemSettings_.ui.showCalibrationLines;
    videoOptions.plateImagePosition = currentSystemSettings_.ui.plateImagePosition;
    if (livePreview_) {
        livePreview_->setDisplayOptions(videoOptions);
    }
    if (snapshotPreview_) {
        snapshotPreview_->setDisplayOptions(videoOptions);
    }

    applyCaptureColumnVisibility();
    updatePreviewLayout();
    configureMaintenanceController();
    updateStartupRegistration(currentSystemSettings_.maintenance.startWithSystem);

    if (captureService_) {
        refreshCaptureRecords();
    }

    if (currentSystemSettings_.ui.startMaximized && initialApply) {
        setWindowState(windowState() | Qt::WindowMaximized);
    }
}

void MainWindow::applyCaptureColumnVisibility()
{
    const QHash<int, QString> columns = {
        {CaptureRecordTableModel::TimeColumn, QStringLiteral("time")},
        {CaptureRecordTableModel::PlateColumn, QStringLiteral("plate")},
        {CaptureRecordTableModel::PlateColorColumn, QStringLiteral("plateColor")},
        {CaptureRecordTableModel::EventTypeColumn, QStringLiteral("eventType")},
        {CaptureRecordTableModel::DeviceIdColumn, QStringLiteral("deviceId")},
        {CaptureRecordTableModel::DirectionColumn, QStringLiteral("direction")},
        {CaptureRecordTableModel::CoordinateColumn, QStringLiteral("coordinate")},
        {CaptureRecordTableModel::RemarkColumn, QStringLiteral("remark")},
    };
    const QList<QTableView*> tables = {allCaptureTable_, validCaptureTable_, unknownCaptureTable_};
    for (QTableView* table : tables) {
        if (!table) {
            continue;
        }
        for (auto it = columns.begin(); it != columns.end(); ++it) {
            table->setColumnHidden(it.key(), !currentSystemSettings_.ui.captureListFields.contains(it.value()));
        }
    }
}

void MainWindow::configureMaintenanceController()
{
    if (!maintenanceController_) {
        return;
    }

    maintenanceController_->setSettings(currentSystemSettings_.maintenance);
    maintenanceController_->setSyncDeviceTimesCallback([this]() {
        const int synced = deviceManager_->syncAllDeviceTimes();
        statusBar()->showMessage(QStringLiteral("已定时同步 %1 台设备时间").arg(synced), 2500);
    });
    maintenanceController_->setDiskMaintenanceCallback([this]() {
        performDiskMaintenance();
    });
    maintenanceController_->setShutdownCallback([this]() {
        runScheduledShutdown();
    });
}

void MainWindow::performDiskMaintenance()
{
    if (!currentSystemSettings_.maintenance.enableDiskMaintenance) {
        return;
    }

    const int expiredDays = qMin(
        currentSystemSettings_.maintenance.expireCaptureDays,
        currentSystemSettings_.maintenance.expireVideoDays);
    const int expiredRemoved = storageService_.deleteFilesOlderThan(expiredDays);

    const qint64 minFreeBytes = qint64(currentSystemSettings_.maintenance.minFreeSpaceGb) * 1024 * 1024 * 1024;
    const QStorageInfo storage(currentSystemSettings_.storage.rootPath);
    if (currentSystemSettings_.maintenance.deleteOldestWhenLowSpace && storage.isValid() && storage.bytesAvailable() < minFreeBytes) {
        const int removed = storageService_.deleteOldestFiles(100, minFreeBytes - storage.bytesAvailable());
        statusBar()->showMessage(QStringLiteral("磁盘维护已删除过期素材 %1 个、最早素材 %2 个").arg(expiredRemoved).arg(removed), 3500);
    } else if (expiredRemoved > 0) {
        statusBar()->showMessage(QStringLiteral("磁盘维护已删除过期素材 %1 个").arg(expiredRemoved), 3500);
    }
}

void MainWindow::runScheduledShutdown()
{
#ifdef Q_OS_WIN
    QProcess::startDetached(QStringLiteral("shutdown"), {QStringLiteral("/s"), QStringLiteral("/t"), QStringLiteral("60")});
#else
    statusBar()->showMessage(QStringLiteral("当前平台不支持自动关机命令"), 3500);
#endif
}

void MainWindow::updateStartupRegistration(bool enabled)
{
#ifdef Q_OS_WIN
    QSettings runKey(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), QSettings::NativeFormat);
    const QString appName = QCoreApplication::applicationName();
    if (enabled) {
        runKey.setValue(appName, QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
    } else {
        runKey.remove(appName);
    }
#else
    Q_UNUSED(enabled)
#endif
}

void MainWindow::updatePreviewLayout()
{
    if (snapshotPreview_) {
        snapshotPreview_->setVisible(currentSystemSettings_.ui.multiVideoSplit);
    }
}

int MainWindow::currentDeviceRow() const
{
    const QModelIndex index = deviceTable_->currentIndex();
    return index.isValid() ? index.row() : -1;
}

int MainWindow::currentCaptureRow() const
{
    QSortFilterProxyModel* proxy = currentCaptureProxy();
    QTableView* table = currentCaptureTable();
    if (!proxy || !table || !table->currentIndex().isValid()) {
        return -1;
    }
    return proxy->mapToSource(table->currentIndex()).row();
}

QTableView* MainWindow::createDeviceTable()
{
    auto* table = new QTableView(this);
    table->setModel(deviceModel_);
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    configureTableView(table);

    connect(table->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &MainWindow::updateDeviceProperties);
    connect(table, &QTableView::customContextMenuRequested, this, &MainWindow::showDeviceContextMenu);

    return table;
}

QTableView* MainWindow::createPropertyTable()
{
    auto* table = new QTableView(this);
    table->setModel(propertyModel_);
    configureTableView(table);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    return table;
}

QWidget* MainWindow::createCaptureRecordPanel()
{
    auto* panel = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(panel);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(6);

    auto* controls = new QHBoxLayout();
    controls->setContentsMargins(0, 0, 0, 0);

    auto* refreshButton = new QToolButton(panel);
    refreshButton->setDefaultAction(refreshAction_);
    controls->addWidget(refreshButton);

    pauseCaptureButton_ = new QToolButton(panel);
    pauseCaptureButton_->setText(QStringLiteral("暂停刷新"));
    pauseCaptureButton_->setCheckable(true);
    connect(pauseCaptureButton_, &QToolButton::toggled, this, &MainWindow::toggleCapturePause);
    controls->addWidget(pauseCaptureButton_);

    controls->addWidget(new QLabel(QStringLiteral("暂停秒数"), panel));
    pauseSecondsSpin_ = new QSpinBox(panel);
    pauseSecondsSpin_->setRange(5, 3600);
    pauseSecondsSpin_->setValue(30);
    pauseSecondsSpin_->setSuffix(QStringLiteral(" 秒"));
    controls->addWidget(pauseSecondsSpin_);

    pendingCaptureLabel_ = new QLabel(QStringLiteral("待刷新 0 条"), panel);
    controls->addWidget(pendingCaptureLabel_);
    controls->addStretch(1);

    auto* deleteButton = new QToolButton(panel);
    deleteButton->setDefaultAction(deleteCaptureAction_);
    controls->addWidget(deleteButton);

    auto* exportButton = new QToolButton(panel);
    exportButton->setDefaultAction(exportAction_);
    controls->addWidget(exportButton);

    rootLayout->addLayout(controls);

    allCaptureProxy_ = createCaptureProxy(QString());
    validCaptureProxy_ = createCaptureProxy(QStringLiteral("valid"));
    unknownCaptureProxy_ = createCaptureProxy(QStringLiteral("unknown"));

    allCaptureTable_ = createCaptureTable(allCaptureProxy_);
    validCaptureTable_ = createCaptureTable(validCaptureProxy_);
    unknownCaptureTable_ = createCaptureTable(unknownCaptureProxy_);

    captureTabs_ = new QTabWidget(panel);
    captureTabs_->addTab(allCaptureTable_, QStringLiteral("全部"));
    captureTabs_->addTab(validCaptureTable_, QStringLiteral("有效车牌"));
    captureTabs_->addTab(unknownCaptureTable_, QStringLiteral("无牌未知"));
    connect(captureTabs_, &QTabWidget::currentChanged, this, [this]() {
        updateCaptureControls();
    });

    rootLayout->addWidget(captureTabs_, 1);

    capturePauseTimer_ = new QTimer(this);
    capturePauseTimer_->setSingleShot(true);
    connect(capturePauseTimer_, &QTimer::timeout, this, &MainWindow::finishCapturePause);

    return panel;
}

QTableView* MainWindow::createCaptureTable(QSortFilterProxyModel* proxyModel)
{
    auto* table = new QTableView(this);
    table->setModel(proxyModel);
    configureTableView(table);
    table->horizontalHeader()->setSectionResizeMode(CaptureRecordTableModel::TimeColumn, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(CaptureRecordTableModel::CoordinateColumn, QHeaderView::ResizeToContents);

    connect(table->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this]() {
        updateCaptureControls();
    });

    return table;
}

QSortFilterProxyModel* MainWindow::createCaptureProxy(const QString& plateStateFilter)
{
    auto* proxy = new QSortFilterProxyModel(this);
    proxy->setSourceModel(captureModel_);
    proxy->setDynamicSortFilter(true);
    if (!plateStateFilter.isEmpty()) {
        proxy->setFilterRole(CaptureRecordTableModel::PlateStateRole);
        proxy->setFilterFixedString(plateStateFilter);
        proxy->setFilterCaseSensitivity(Qt::CaseSensitive);
    }
    return proxy;
}

void MainWindow::configureTableView(QTableView* table) const
{
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
}

void MainWindow::updateVideoWidgets()
{
    const int row = currentDeviceRow();
    const Device* device = deviceModel_->deviceAt(row);
    const std::optional<CaptureRecord> latestRecord = device && captureService_
        ? captureService_->latestForDevice(device->id)
        : std::nullopt;
    const CaptureRecord* latestRecordPtr = latestRecord ? &(*latestRecord) : nullptr;

    livePreview_->setDevice(device);
    livePreview_->setLatestRecord(latestRecordPtr);
    snapshotPreview_->setDevice(device);
    snapshotPreview_->setLatestRecord(latestRecordPtr);
}

void MainWindow::updateCaptureControls()
{
    if (pendingCaptureLabel_) {
        pendingCaptureLabel_->setText(QStringLiteral("待刷新 %1 条").arg(pendingCaptureCount_));
    }

    if (captureTabs_) {
        captureTabs_->setTabText(0, QStringLiteral("全部 (%1)").arg(allCaptureProxy_->rowCount()));
        captureTabs_->setTabText(1, QStringLiteral("有效车牌 (%1)").arg(validCaptureProxy_->rowCount()));
        captureTabs_->setTabText(2, QStringLiteral("无牌未知 (%1)").arg(unknownCaptureProxy_->rowCount()));
    }

    if (deleteCaptureAction_) {
        const QTableView* table = currentCaptureTable();
        deleteCaptureAction_->setEnabled(table && table->currentIndex().isValid());
    }

    updateStatusText();
}

QTableView* MainWindow::currentCaptureTable() const
{
    if (!captureTabs_) {
        return nullptr;
    }

    switch (captureTabs_->currentIndex()) {
    case 1:
        return validCaptureTable_;
    case 2:
        return unknownCaptureTable_;
    case 0:
    default:
        return allCaptureTable_;
    }
}

QSortFilterProxyModel* MainWindow::currentCaptureProxy() const
{
    if (!captureTabs_) {
        return nullptr;
    }

    switch (captureTabs_->currentIndex()) {
    case 1:
        return validCaptureProxy_;
    case 2:
        return unknownCaptureProxy_;
    case 0:
    default:
        return allCaptureProxy_;
    }
}

CaptureRecordFilter MainWindow::currentCaptureFilter() const
{
    if (!captureTabs_) {
        return CaptureRecordFilter::All;
    }

    switch (captureTabs_->currentIndex()) {
    case 1:
        return CaptureRecordFilter::ValidPlate;
    case 2:
        return CaptureRecordFilter::UnknownPlate;
    case 0:
    default:
        return CaptureRecordFilter::All;
    }
}

CaptureAssetKind MainWindow::storageKindForRecord(const CaptureRecord& record) const
{
    switch (record.type) {
    case CaptureType::Overspeed:
        return CaptureAssetKind::OverspeedImage;
    case CaptureType::Blacklist:
        return CaptureAssetKind::WatchedVehicleImage;
    case CaptureType::UnknownPlate:
    case CaptureType::Normal:
    default:
        return CaptureAssetKind::NormalImage;
    }
}

void MainWindow::addDevice()
{
    deviceManager_->addMockDevice();
    selectDeviceRow(deviceModel_->deviceCount() - 1);
    updateDeviceProperties();
    updateStatusText();
    statusBar()->showMessage(QStringLiteral("已添加模拟设备"), 2500);
}

void MainWindow::connectSelectedDevice()
{
    const int row = currentDeviceRow();
    if (row < 0) {
        statusBar()->showMessage(QStringLiteral("请先选择一个设备"), 2500);
        return;
    }

    if (deviceManager_->connectDevice(row)) {
        statusBar()->showMessage(QStringLiteral("设备已连接，模拟状态和抓拍开始自动刷新"), 2500);
    }
}

void MainWindow::disconnectSelectedDevice()
{
    const int row = currentDeviceRow();
    if (row < 0) {
        statusBar()->showMessage(QStringLiteral("请先选择一个设备"), 2500);
        return;
    }

    deviceManager_->disconnectDevice(row);
    statusBar()->showMessage(QStringLiteral("设备已断开"), 2500);
}

void MainWindow::openDeviceConfig()
{
    const int row = currentDeviceRow();
    const Device* device = deviceManager_->deviceAt(row);
    if (!device) {
        statusBar()->showMessage(QStringLiteral("请先选择一个设备"), 2500);
        return;
    }

    DeviceConfigDialog dialog(*device, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    if (deviceManager_->writeDeviceConfig(row, dialog.config())) {
        statusBar()->showMessage(QStringLiteral("设备配置已保存并下发"), 2500);
    } else {
        QMessageBox::warning(this, QStringLiteral("设备配置"), QStringLiteral("配置保存失败，请确认设备在线后重试。"));
    }
}

void MainWindow::triggerCapture()
{
    const int row = currentDeviceRow();
    if (row < 0) {
        statusBar()->showMessage(QStringLiteral("请先选择一个设备"), 2500);
        return;
    }

    deviceManager_->triggerCapture(row);
}

void MainWindow::rebootSelectedDevice()
{
    const int row = currentDeviceRow();
    if (row < 0) {
        statusBar()->showMessage(QStringLiteral("请先选择一个设备"), 2500);
        return;
    }

    if (deviceManager_->rebootDevice(row)) {
        statusBar()->showMessage(QStringLiteral("已发送模拟重启命令"), 2500);
    }
}

void MainWindow::syncSelectedDeviceTime()
{
    const int row = currentDeviceRow();
    if (row < 0) {
        statusBar()->showMessage(QStringLiteral("请先选择一个设备"), 2500);
        return;
    }

    if (deviceManager_->syncDeviceTime(row)) {
        statusBar()->showMessage(QStringLiteral("已同步设备时间"), 2500);
    }
}

void MainWindow::openGlobalSettings()
{
    SystemSettingsDialog dialog(currentSystemSettings_, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    if (!systemSettingsService_->save(dialog.settings())) {
        QMessageBox::warning(this, QStringLiteral("全局设置"), systemSettingsService_->lastError());
        return;
    }

    currentSystemSettings_ = systemSettingsService_->settings();
    applySystemSettings();

    if (currentSystemSettings_.ui.showSuccessPopup) {
        QMessageBox::information(this, QStringLiteral("全局设置"), QStringLiteral("设置已保存并应用。"));
    } else {
        statusBar()->showMessage(QStringLiteral("全局设置已保存并应用"), 2500);
    }
}

void MainWindow::refreshCaptureRecords()
{
    if (!captureService_) {
        return;
    }

    captureModel_->setRecords(captureService_->records(CaptureRecordFilter::All, currentSystemSettings_.ui.captureListMaxRows));
    pendingCaptureCount_ = 0;
    updateCaptureControls();
    updateDeviceProperties();
    statusBar()->showMessage(QStringLiteral("抓拍记录已刷新"), 1800);
}

void MainWindow::toggleCapturePause(bool paused)
{
    if (paused) {
        const int seconds = pauseSecondsSpin_ ? pauseSecondsSpin_->value() : 30;
        capturePauseTimer_->start(seconds * 1000);
        pauseCaptureButton_->setText(QStringLiteral("恢复刷新"));
        statusBar()->showMessage(QStringLiteral("抓拍列表已暂停刷新 %1 秒").arg(seconds), 2500);
        return;
    }

    capturePauseTimer_->stop();
    pauseCaptureButton_->setText(QStringLiteral("暂停刷新"));
    refreshCaptureRecords();
}

void MainWindow::finishCapturePause()
{
    if (pauseCaptureButton_ && pauseCaptureButton_->isChecked()) {
        pauseCaptureButton_->setChecked(false);
    } else {
        refreshCaptureRecords();
    }
}

void MainWindow::exportCaptureRecords()
{
    const QString defaultName = QStringLiteral("capture_records_%1.csv")
                                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("导出抓拍记录"),
        QDir::home().filePath(defaultName),
        QStringLiteral("CSV 文件 (*.csv)"));

    if (path.isEmpty()) {
        return;
    }

    if (!captureService_->exportCsv(currentCaptureFilter(), path)) {
        QMessageBox::warning(this, QStringLiteral("导出抓拍记录"), captureService_->lastError());
        return;
    }

    statusBar()->showMessage(QStringLiteral("抓拍记录已导出：%1").arg(path), 3500);
}

void MainWindow::deleteSelectedCapture()
{
    const int sourceRow = currentCaptureRow();
    const CaptureRecord* record = captureModel_->recordAt(sourceRow);
    if (!record) {
        statusBar()->showMessage(QStringLiteral("请先选择一条抓拍记录"), 2500);
        return;
    }

    const QString id = record->id;
    if (!captureService_->deleteRecord(id)) {
        QMessageBox::warning(this, QStringLiteral("删除抓拍记录"), captureService_->lastError());
        return;
    }

    refreshCaptureRecords();
    statusBar()->showMessage(QStringLiteral("已删除选中的抓拍记录"), 2500);
}

void MainWindow::clearCaptureRecords()
{
    statusBar()->showMessage(QStringLiteral("持久化台账未启用一键清空，请逐条删除需要移除的记录"), 2500);
}

void MainWindow::showActionMessage()
{
    const auto* action = qobject_cast<QAction*>(sender());
    const QString name = action ? action->text() : QStringLiteral("操作");
    statusBar()->showMessage(QStringLiteral("%1 功能将在后续阶段完善").arg(name), 2500);
}

void MainWindow::updateDeviceProperties()
{
    const int row = currentDeviceRow();
    const Device* device = deviceModel_->deviceAt(row);
    const std::optional<CaptureRecord> latestRecord = device && captureService_
        ? captureService_->latestForDevice(device->id)
        : std::nullopt;
    const CaptureRecord* latestRecordPtr = latestRecord ? &(*latestRecord) : nullptr;

    propertyModel_->setDevice(device, latestRecordPtr);
    updateVideoWidgets();
    updateStatusText();
}

void MainWindow::showDeviceContextMenu(const QPoint& position)
{
    if (!deviceTable_->indexAt(position).isValid()) {
        return;
    }

    deviceContextMenu_->exec(deviceTable_->viewport()->mapToGlobal(position));
}

void MainWindow::handleDeviceAdded(const Device& device)
{
    deviceModel_->addDevice(device);
    updateStatusText();
}

void MainWindow::handleDeviceStatusChanged(int row, const DeviceStatus& status)
{
    deviceModel_->updateStatus(row, status);
    if (row == currentDeviceRow()) {
        updateDeviceProperties();
    }
    updateStatusText();
}

void MainWindow::handleDeviceConfigChanged(int row, const DeviceConfig& config)
{
    deviceModel_->updateConfig(row, config);
    if (row == currentDeviceRow()) {
        updateDeviceProperties();
    }
    updateStatusText();
}

void MainWindow::handleCaptureGenerated(int row, const CaptureRecord& record)
{
    CaptureRecord storedRecord = record;
    const CaptureStorageResult storageResult = storageService_.saveCaptureAssets(storedRecord, storageKindForRecord(storedRecord));
    if (storageResult.ok) {
        storedRecord.filePath = storageResult.primaryPath;
    } else {
        storedRecord.filePath = QStringLiteral("未保存：%1").arg(storageResult.errorMessage);
    }

    if (!captureService_->addRecord(storedRecord)) {
        QMessageBox::warning(this, QStringLiteral("保存抓拍记录"), captureService_->lastError());
        return;
    }

    deviceModel_->incrementCaptureCount(row);

    if (pauseCaptureButton_ && pauseCaptureButton_->isChecked()) {
        ++pendingCaptureCount_;
        updateCaptureControls();
    } else {
        refreshCaptureRecords();
        const QModelIndex sourceIndex = captureModel_->index(captureModel_->recordCount() - 1, 0);
        const QModelIndex proxyIndex = allCaptureProxy_->mapFromSource(sourceIndex);
        if (proxyIndex.isValid()) {
            allCaptureTable_->selectRow(proxyIndex.row());
        }
    }

    if (row == currentDeviceRow()) {
        updateDeviceProperties();
    }

    updateStatusText();
    statusBar()->showMessage(QStringLiteral("收到模拟抓拍：%1 %2 km/h").arg(storedRecord.plateNumber).arg(storedRecord.speedKmh), 2500);
}

void MainWindow::handleDeviceError(int row, const QString& message)
{
    const Device* device = deviceModel_->deviceAt(row);
    const QString prefix = device ? device->name : QStringLiteral("设备");
    statusBar()->showMessage(QStringLiteral("%1：%2").arg(prefix, message), 3500);
}
