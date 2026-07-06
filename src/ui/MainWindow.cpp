#include "MainWindow.h"

#include <QAction>
#include <QAbstractItemView>
#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QList>
#include <QMenu>
#include <QMessageBox>
#include <QPalette>
#include <QPoint>
#include <QPair>
#include <QRandomGenerator>
#include <QSize>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolBar>
#include <QVBoxLayout>

namespace {

QTableWidgetItem* makeReadOnlyItem(const QString& text)
{
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

void stretchLastColumn(QTableWidget* table)
{
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("车牌识别雷达测速摄像机管理软件"));
    resize(1360, 820);
    setMinimumSize(1024, 680);

    createActions();
    createToolBar();
    createCentralLayout();
    createDeviceContextMenu();
    createStatusBar();
    populateEmptyTables();
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

    globalSettingsAction_ = new QAction(icon(QStyle::SP_FileDialogContentsView), QStringLiteral("全局设置"), this);
    connect(globalSettingsAction_, &QAction::triggered, this, &MainWindow::showActionMessage);

    refreshAction_ = new QAction(icon(QStyle::SP_BrowserReload), QStringLiteral("刷新"), this);
    connect(refreshAction_, &QAction::triggered, this, &MainWindow::showActionMessage);

    exportAction_ = new QAction(icon(QStyle::SP_DriveFDIcon), QStringLiteral("导出记录"), this);
    connect(exportAction_, &QAction::triggered, this, &MainWindow::showActionMessage);
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
    captureTable_ = createCaptureTable();

    auto* leftSplitter = new QSplitter(Qt::Vertical, this);
    leftSplitter->addWidget(deviceTable_);
    leftSplitter->addWidget(propertyTable_);
    leftSplitter->setStretchFactor(0, 3);
    leftSplitter->setStretchFactor(1, 2);

    auto* previewSplitter = new QSplitter(Qt::Horizontal, this);
    previewSplitter->addWidget(createPreviewPanel(QStringLiteral("实时预览"), QStringLiteral("等待选择设备或连接视频源")));
    previewSplitter->addWidget(createPreviewPanel(QStringLiteral("最近抓拍"), QStringLiteral("暂无抓拍图片")));
    previewSplitter->setStretchFactor(0, 3);
    previewSplitter->setStretchFactor(1, 2);

    auto* rightSplitter = new QSplitter(Qt::Vertical, this);
    rightSplitter->addWidget(previewSplitter);
    rightSplitter->addWidget(captureTable_);
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
    statusLabel_ = new QLabel(QStringLiteral("就绪 | 设备 0 台 | 在线 0 台 | 抓拍记录 0 条"), this);
    statusBar()->addWidget(statusLabel_, 1);
    statusBar()->showMessage(QStringLiteral("界面骨架已启动"));
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
    deviceContextMenu_->addAction(QStringLiteral("重启相机"), this, &MainWindow::showActionMessage);
    deviceContextMenu_->addAction(QStringLiteral("同步时间"), this, &MainWindow::showActionMessage);
    deviceContextMenu_->addAction(QStringLiteral("网络信息"), this, &MainWindow::showActionMessage);
    deviceContextMenu_->addAction(QStringLiteral("打开本地文件夹"), this, &MainWindow::showActionMessage);
}

void MainWindow::populateEmptyTables()
{
    resetPropertyTable();
    deviceTable_->setRowCount(0);
    captureTable_->setRowCount(0);
}

void MainWindow::resetPropertyTable()
{
    propertyTable_->setRowCount(6);

    const QList<QPair<QString, QString>> properties = {
        {QStringLiteral("设备状态"), QStringLiteral("未连接")},
        {QStringLiteral("IP 地址"), QStringLiteral("-")},
        {QStringLiteral("端口"), QStringLiteral("-")},
        {QStringLiteral("通道"), QStringLiteral("-")},
        {QStringLiteral("限速值"), QStringLiteral("-")},
        {QStringLiteral("最后抓拍"), QStringLiteral("-")},
    };

    for (int row = 0; row < properties.size(); ++row) {
        propertyTable_->setItem(row, 0, makeReadOnlyItem(properties[row].first));
        propertyTable_->setItem(row, 1, makeReadOnlyItem(properties[row].second));
    }
}

QWidget* MainWindow::createPreviewPanel(const QString& title, const QString& subtitle) const
{
    auto* panel = new QFrame;
    panel->setFrameShape(QFrame::StyledPanel);
    panel->setAutoFillBackground(true);

    auto palette = panel->palette();
    palette.setColor(QPalette::Window, QColor(24, 28, 34));
    panel->setPalette(palette);

    auto* titleLabel = new QLabel(title, panel);
    titleLabel->setStyleSheet(QStringLiteral("color: #f4f7fb; font-size: 18px; font-weight: 600;"));
    titleLabel->setAlignment(Qt::AlignCenter);

    auto* subtitleLabel = new QLabel(subtitle, panel);
    subtitleLabel->setStyleSheet(QStringLiteral("color: #9aa7b8; font-size: 13px;"));
    subtitleLabel->setAlignment(Qt::AlignCenter);

    auto* crosshair = new QLabel(QStringLiteral("+"), panel);
    crosshair->setStyleSheet(QStringLiteral("color: #5f6f82; font-size: 48px;"));
    crosshair->setAlignment(Qt::AlignCenter);

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->addStretch();
    layout->addWidget(titleLabel);
    layout->addWidget(subtitleLabel);
    layout->addWidget(crosshair);
    layout->addStretch();

    return panel;
}

QTableWidget* MainWindow::createDeviceTable()
{
    auto* table = new QTableWidget(this);
    table->setColumnCount(5);
    table->setHorizontalHeaderLabels({
        QStringLiteral("名称"),
        QStringLiteral("IP"),
        QStringLiteral("状态"),
        QStringLiteral("方向"),
        QStringLiteral("限速"),
    });
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    stretchLastColumn(table);

    connect(table, &QTableWidget::itemSelectionChanged, this, &MainWindow::updateDeviceProperties);
    connect(table, &QTableWidget::customContextMenuRequested, this, &MainWindow::showDeviceContextMenu);

    return table;
}

QTableWidget* MainWindow::createPropertyTable()
{
    auto* table = new QTableWidget(this);
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({
        QStringLiteral("属性"),
        QStringLiteral("值"),
    });
    stretchLastColumn(table);
    return table;
}

QTableWidget* MainWindow::createCaptureTable()
{
    auto* table = new QTableWidget(this);
    table->setColumnCount(9);
    table->setHorizontalHeaderLabels({
        QStringLiteral("时间"),
        QStringLiteral("设备"),
        QStringLiteral("车牌号"),
        QStringLiteral("颜色"),
        QStringLiteral("速度"),
        QStringLiteral("限速"),
        QStringLiteral("方向"),
        QStringLiteral("类型"),
        QStringLiteral("文件"),
    });
    stretchLastColumn(table);
    return table;
}

void MainWindow::addDevice()
{
    const int row = deviceTable_->rowCount();
    deviceTable_->insertRow(row);

    const QString name = QStringLiteral("模拟设备-%1").arg(row + 1, 2, 10, QLatin1Char('0'));
    const QString ip = QStringLiteral("192.168.1.%1").arg(100 + row);

    deviceTable_->setItem(row, 0, makeReadOnlyItem(name));
    deviceTable_->setItem(row, 1, makeReadOnlyItem(ip));
    deviceTable_->setItem(row, 2, makeReadOnlyItem(QStringLiteral("离线")));
    deviceTable_->setItem(row, 3, makeReadOnlyItem(QStringLiteral("由北向南")));
    deviceTable_->setItem(row, 4, makeReadOnlyItem(QStringLiteral("60 km/h")));
    deviceTable_->selectRow(row);

    statusLabel_->setText(QStringLiteral("就绪 | 设备 %1 台 | 在线 0 台 | 抓拍记录 %2 条")
                              .arg(deviceTable_->rowCount())
                              .arg(captureTable_->rowCount()));
    statusBar()->showMessage(QStringLiteral("已添加模拟设备占位项"), 2500);
}

void MainWindow::connectSelectedDevice()
{
    const int row = deviceTable_->currentRow();
    if (row < 0) {
        statusBar()->showMessage(QStringLiteral("请先选择一个设备"), 2500);
        return;
    }

    deviceTable_->item(row, 2)->setText(QStringLiteral("在线"));
    updateDeviceProperties();
    statusBar()->showMessage(QStringLiteral("设备已标记为在线"), 2500);
}

void MainWindow::disconnectSelectedDevice()
{
    const int row = deviceTable_->currentRow();
    if (row < 0) {
        statusBar()->showMessage(QStringLiteral("请先选择一个设备"), 2500);
        return;
    }

    deviceTable_->item(row, 2)->setText(QStringLiteral("离线"));
    updateDeviceProperties();
    statusBar()->showMessage(QStringLiteral("设备已断开"), 2500);
}

void MainWindow::openDeviceConfig()
{
    QMessageBox::information(
        this,
        QStringLiteral("设备配置"),
        QStringLiteral("设备配置弹窗将在第 5 阶段实现。当前已保留入口。"));
}

void MainWindow::triggerCapture()
{
    const int deviceRow = deviceTable_->currentRow();
    if (deviceRow < 0) {
        statusBar()->showMessage(QStringLiteral("请先选择一个设备"), 2500);
        return;
    }

    const int row = captureTable_->rowCount();
    captureTable_->insertRow(row);

    const QString deviceName = deviceTable_->item(deviceRow, 0)->text();
    const int speed = 45 + static_cast<int>(QRandomGenerator::global()->bounded(35));
    const QString plate = QStringLiteral("粤B%1").arg(10000 + static_cast<int>(QRandomGenerator::global()->bounded(89999)));
    const QString type = speed > 60 ? QStringLiteral("超速") : QStringLiteral("正常");

    const QStringList values = {
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
        deviceName,
        plate,
        QStringLiteral("蓝牌"),
        QStringLiteral("%1 km/h").arg(speed),
        QStringLiteral("60 km/h"),
        QStringLiteral("由北向南"),
        type,
        QStringLiteral("未保存"),
    };

    for (int column = 0; column < values.size(); ++column) {
        captureTable_->setItem(row, column, makeReadOnlyItem(values[column]));
    }

    captureTable_->selectRow(row);
    updateDeviceProperties();
    statusBar()->showMessage(QStringLiteral("已生成一条抓拍占位记录"), 2500);
}

void MainWindow::showActionMessage()
{
    const auto* action = qobject_cast<QAction*>(sender());
    const QString name = action ? action->text() : QStringLiteral("操作");
    statusBar()->showMessage(QStringLiteral("%1 功能将在后续阶段完善").arg(name), 2500);
}

void MainWindow::updateDeviceProperties()
{
    const int row = deviceTable_->currentRow();
    if (row < 0) {
        resetPropertyTable();
        return;
    }

    const QList<QPair<QString, QString>> properties = {
        {QStringLiteral("设备名称"), deviceTable_->item(row, 0)->text()},
        {QStringLiteral("设备状态"), deviceTable_->item(row, 2)->text()},
        {QStringLiteral("IP 地址"), deviceTable_->item(row, 1)->text()},
        {QStringLiteral("端口"), QStringLiteral("8000")},
        {QStringLiteral("通道"), deviceTable_->item(row, 3)->text()},
        {QStringLiteral("限速值"), deviceTable_->item(row, 4)->text()},
        {QStringLiteral("最后抓拍"), captureTable_->rowCount() > 0
             ? captureTable_->item(captureTable_->rowCount() - 1, 0)->text()
             : QStringLiteral("-")},
    };

    propertyTable_->setRowCount(properties.size());
    for (int propertyRow = 0; propertyRow < properties.size(); ++propertyRow) {
        propertyTable_->setItem(propertyRow, 0, makeReadOnlyItem(properties[propertyRow].first));
        propertyTable_->setItem(propertyRow, 1, makeReadOnlyItem(properties[propertyRow].second));
    }

    int onlineCount = 0;
    for (int deviceRow = 0; deviceRow < deviceTable_->rowCount(); ++deviceRow) {
        if (deviceTable_->item(deviceRow, 2)->text() == QStringLiteral("在线")) {
            ++onlineCount;
        }
    }

    statusLabel_->setText(QStringLiteral("就绪 | 设备 %1 台 | 在线 %2 台 | 抓拍记录 %3 条")
                              .arg(deviceTable_->rowCount())
                              .arg(onlineCount)
                              .arg(captureTable_->rowCount()));
}

void MainWindow::showDeviceContextMenu(const QPoint& position)
{
    if (!deviceTable_->indexAt(position).isValid()) {
        return;
    }

    deviceContextMenu_->exec(deviceTable_->viewport()->mapToGlobal(position));
}
