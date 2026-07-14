#include "SystemSettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStringList>
#include <QTimeEdit>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
QWidget* scrollPage(QWidget* content)
{
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    return scroll;
}
}

SystemSettingsDialog::SystemSettingsDialog(const SystemSettings& settings, QWidget* parent)
    : QDialog(parent)
    , settings_(settings)
{
    setWindowTitle(QStringLiteral("全局系统设置"));
    resize(920, 680);
    setMinimumSize(820, 560);

    tree_ = new QTreeWidget(this);
    tree_->setHeaderHidden(true);
    tree_->setFixedWidth(190);

    stack_ = new QStackedWidget(this);
    addPage(QStringLiteral("界面选项"), createUiPage());
    addPage(QStringLiteral("结果保存"), createStoragePage());
    addPage(QStringLiteral("系统选项"), createMaintenancePage());

    connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* current) {
        if (current) {
            stack_->setCurrentIndex(current->data(0, Qt::UserRole).toInt());
        }
    });

    if (auto* first = tree_->topLevelItem(0)) {
        tree_->setCurrentItem(first);
    }

    auto* contentLayout = new QHBoxLayout;
    contentLayout->addWidget(tree_);
    contentLayout->addWidget(stack_, 1);

    buttonBox_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttonBox_->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定"));
    buttonBox_->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttonBox_, &QDialogButtonBox::accepted, this, [this]() {
        applyToSettings();
        accept();
    });
    connect(buttonBox_, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* rootLayout = new QVBoxLayout(this);
    auto* hint = new QLabel(QStringLiteral("配置保存后会立即应用到当前界面，并写入 data/config/system.ini。"), this);
    hint->setStyleSheet(QStringLiteral("color:#44515f; padding:4px 0;"));
    rootLayout->addWidget(hint);
    rootLayout->addLayout(contentLayout, 1);
    rootLayout->addWidget(buttonBox_);

    loadFromSettings();
}

SystemSettings SystemSettingsDialog::settings() const
{
    return settings_;
}

QWidget* SystemSettingsDialog::createUiPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    form->setLabelAlignment(Qt::AlignRight);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    startMaximizedCheck_ = checkBox(QStringLiteral("软件开机最大化"), false);
    autoListenCheck_ = checkBox(QStringLiteral("自动侦听设备数据"), true);
    autoConnectCheck_ = checkBox(QStringLiteral("启动自动连接设备"), false);
    fontCombo_ = new QFontComboBox(page);
    fontSizeSpin_ = spinBox(6, 48, 10, QStringLiteral(" pt"));
    cacheRowsSpin_ = spinBox(1, 100000, 1000, QStringLiteral(" 条"));
    frameRateSpin_ = spinBox(1, 60, 12, QStringLiteral(" fps"));
    onlyVehicleFramesCheck_ = checkBox(QStringLiteral("仅显示有车画面"), false);
    overlaySpeedCheck_ = checkBox(QStringLiteral("实时叠加车速"), true);
    calibrationLinesCheck_ = checkBox(QStringLiteral("显示标定线"), true);
    platePositionCombo_ = comboBox({QStringLiteral("right"), QStringLiteral("bottom"), QStringLiteral("hidden")}, QStringLiteral("right"));
    multiSplitCheck_ = checkBox(QStringLiteral("多路视频分屏"), true);
    stopVideoWhenMinimizedCheck_ = checkBox(QStringLiteral("最小化自动停止视频查询"), true);
    successPopupCheck_ = checkBox(QStringLiteral("操作成功弹窗提示"), false);

    form->addRow(QStringLiteral("启动配置"), startMaximizedCheck_);
    form->addRow(QString(), autoListenCheck_);
    form->addRow(QString(), autoConnectCheck_);
    form->addRow(QStringLiteral("全局字体"), fontCombo_);
    form->addRow(QStringLiteral("字号"), fontSizeSpin_);
    form->addRow(QStringLiteral("抓拍列表缓存"), cacheRowsSpin_);

    auto* columnsBox = new QWidget(page);
    auto* columnsLayout = new QVBoxLayout(columnsBox);
    columnsLayout->setContentsMargins(0, 0, 0, 0);
    const QList<QPair<QString, QString>> columns = {
        {QStringLiteral("time"), QStringLiteral("抓拍精确时间")},
        {QStringLiteral("plate"), QStringLiteral("车牌号码")},
        {QStringLiteral("plateColor"), QStringLiteral("车牌颜色")},
        {QStringLiteral("eventType"), QStringLiteral("事件类型")},
        {QStringLiteral("deviceId"), QStringLiteral("设备编号")},
        {QStringLiteral("direction"), QStringLiteral("通行朝向")},
        {QStringLiteral("coordinate"), QStringLiteral("画面坐标参数")},
        {QStringLiteral("remark"), QStringLiteral("备注")},
    };
    for (const auto& column : columns) {
        auto* columnCheck = checkBox(column.second, true);
        columnChecks_.insert(column.first, columnCheck);
        columnsLayout->addWidget(columnCheck);
    }
    form->addRow(QStringLiteral("列表展示字段"), columnsBox);

    form->addRow(QStringLiteral("预览帧率"), frameRateSpin_);
    form->addRow(QString(), onlyVehicleFramesCheck_);
    form->addRow(QString(), overlaySpeedCheck_);
    form->addRow(QString(), calibrationLinesCheck_);
    form->addRow(QStringLiteral("车牌放大图位置"), platePositionCombo_);
    form->addRow(QString(), multiSplitCheck_);
    form->addRow(QString(), stopVideoWhenMinimizedCheck_);
    form->addRow(QString(), successPopupCheck_);
    return scrollPage(page);
}

QWidget* SystemSettingsDialog::createStoragePage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    form->setLabelAlignment(Qt::AlignRight);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    rootPathEdit_ = lineEdit();
    auto* pathRow = new QWidget(page);
    auto* pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    auto* browseButton = new QPushButton(QStringLiteral("浏览"), pathRow);
    pathLayout->addWidget(rootPathEdit_, 1);
    pathLayout->addWidget(browseButton);
    connect(browseButton, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择抓拍素材存储路径"), rootPathEdit_->text());
        if (!path.isEmpty()) {
            rootPathEdit_->setText(path);
        }
    });

    normalImageTemplateEdit_ = lineEdit();
    overspeedImageTemplateEdit_ = lineEdit();
    watchedImageTemplateEdit_ = lineEdit();
    violationVideoTemplateEdit_ = lineEdit();
    plateCloseupTemplateEdit_ = lineEdit();
    testAssetTemplateEdit_ = lineEdit();
    regularVideoTemplateEdit_ = lineEdit();
    indexDigitsSpin_ = spinBox(2, 12, 6);
    textInfoCheck_ = checkBox(QStringLiteral("同步生成文本信息文件"), true);
    savePlateCloseupCheck_ = checkBox(QStringLiteral("保存车牌特写"), true);
    mergeImagesCheck_ = checkBox(QStringLiteral("全景与车牌图合并"), false);
    autoRecordCheck_ = checkBox(QStringLiteral("自动录像"), false);
    vehiclePassRecordCheck_ = checkBox(QStringLiteral("车辆过车录像"), false);
    maxVideoSegmentSpin_ = spinBox(1, 8192, 512, QStringLiteral(" MB"));

    form->addRow(QStringLiteral("存储路径"), pathRow);
    form->addRow(QStringLiteral("正常车图模板"), normalImageTemplateEdit_);
    form->addRow(QStringLiteral("超速违法图模板"), overspeedImageTemplateEdit_);
    form->addRow(QStringLiteral("关注车辆图模板"), watchedImageTemplateEdit_);
    form->addRow(QStringLiteral("违法录像模板"), violationVideoTemplateEdit_);
    form->addRow(QStringLiteral("车牌特写图模板"), plateCloseupTemplateEdit_);
    form->addRow(QStringLiteral("测试设备素材模板"), testAssetTemplateEdit_);
    form->addRow(QStringLiteral("常规录像模板"), regularVideoTemplateEdit_);
    form->addRow(QStringLiteral("文件索引位数"), indexDigitsSpin_);
    form->addRow(QString(), textInfoCheck_);
    form->addRow(QString(), savePlateCloseupCheck_);
    form->addRow(QString(), mergeImagesCheck_);
    form->addRow(QString(), autoRecordCheck_);
    form->addRow(QString(), vehiclePassRecordCheck_);
    form->addRow(QStringLiteral("单段录像最大容量"), maxVideoSegmentSpin_);
    return scrollPage(page);
}

QWidget* SystemSettingsDialog::createMaintenancePage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    form->setLabelAlignment(Qt::AlignRight);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    startWithSystemCheck_ = checkBox(QStringLiteral("跟随电脑系统开机自启"), false);
    dailySyncCheck_ = checkBox(QStringLiteral("每日同步所有设备时间"), false);
    dailySyncTimeEdit_ = new QTimeEdit(page);
    dailySyncTimeEdit_->setDisplayFormat(QStringLiteral("HH:mm"));
    shutdownCheck_ = checkBox(QStringLiteral("定时自动关机"), false);
    shutdownTimeEdit_ = new QTimeEdit(page);
    shutdownTimeEdit_->setDisplayFormat(QStringLiteral("HH:mm"));
    diskMaintenanceCheck_ = checkBox(QStringLiteral("启用磁盘自动维护"), true);
    expireCaptureDaysSpin_ = spinBox(1, 3650, 30, QStringLiteral(" 天"));
    expireVideoDaysSpin_ = spinBox(1, 3650, 30, QStringLiteral(" 天"));
    minFreeSpaceSpin_ = spinBox(1, 1024, 5, QStringLiteral(" GB"));
    deleteOldestCheck_ = checkBox(QStringLiteral("低于剩余空间阈值时删除最早文件"), true);

    form->addRow(QStringLiteral("开机自启"), startWithSystemCheck_);
    form->addRow(QStringLiteral("设备时间同步"), dailySyncCheck_);
    form->addRow(QStringLiteral("同步时点"), dailySyncTimeEdit_);
    form->addRow(QStringLiteral("自动关机"), shutdownCheck_);
    form->addRow(QStringLiteral("关机时点"), shutdownTimeEdit_);
    form->addRow(QStringLiteral("磁盘维护"), diskMaintenanceCheck_);
    form->addRow(QStringLiteral("图片过期天数"), expireCaptureDaysSpin_);
    form->addRow(QStringLiteral("录像过期天数"), expireVideoDaysSpin_);
    form->addRow(QStringLiteral("最小剩余空间"), minFreeSpaceSpin_);
    form->addRow(QString(), deleteOldestCheck_);
    return scrollPage(page);
}

void SystemSettingsDialog::addPage(const QString& title, QWidget* page)
{
    const int index = stack_->addWidget(page);
    auto* item = new QTreeWidgetItem(tree_, QStringList{title});
    item->setData(0, Qt::UserRole, index);
    tree_->addTopLevelItem(item);
}

void SystemSettingsDialog::loadFromSettings()
{
    startMaximizedCheck_->setChecked(settings_.ui.startMaximized);
    autoListenCheck_->setChecked(settings_.ui.autoListenDeviceData);
    autoConnectCheck_->setChecked(settings_.ui.autoConnectOnStart);
    fontCombo_->setCurrentFont(QFont(settings_.ui.fontFamily));
    fontSizeSpin_->setValue(settings_.ui.fontPointSize);
    cacheRowsSpin_->setValue(settings_.ui.captureListMaxRows);
    for (auto it = columnChecks_.begin(); it != columnChecks_.end(); ++it) {
        it.value()->setChecked(settings_.ui.captureListFields.contains(it.key()));
    }
    frameRateSpin_->setValue(settings_.ui.previewFrameRate);
    onlyVehicleFramesCheck_->setChecked(settings_.ui.showOnlyVehicleFrames);
    overlaySpeedCheck_->setChecked(settings_.ui.overlaySpeed);
    calibrationLinesCheck_->setChecked(settings_.ui.showCalibrationLines);
    platePositionCombo_->setCurrentText(settings_.ui.plateImagePosition);
    multiSplitCheck_->setChecked(settings_.ui.multiVideoSplit);
    stopVideoWhenMinimizedCheck_->setChecked(settings_.ui.stopVideoQueryWhenMinimized);
    successPopupCheck_->setChecked(settings_.ui.showSuccessPopup);

    rootPathEdit_->setText(settings_.storage.rootPath);
    normalImageTemplateEdit_->setText(settings_.storage.normalImageTemplate);
    overspeedImageTemplateEdit_->setText(settings_.storage.overspeedImageTemplate);
    watchedImageTemplateEdit_->setText(settings_.storage.watchedVehicleImageTemplate);
    violationVideoTemplateEdit_->setText(settings_.storage.violationVideoTemplate);
    plateCloseupTemplateEdit_->setText(settings_.storage.plateCloseupTemplate);
    testAssetTemplateEdit_->setText(settings_.storage.testDeviceAssetTemplate);
    regularVideoTemplateEdit_->setText(settings_.storage.regularVideoTemplate);
    indexDigitsSpin_->setValue(settings_.storage.indexDigits);
    textInfoCheck_->setChecked(settings_.storage.syncTextInfoFile);
    savePlateCloseupCheck_->setChecked(settings_.storage.savePlateCloseup);
    mergeImagesCheck_->setChecked(settings_.storage.mergePanoramaAndPlate);
    autoRecordCheck_->setChecked(settings_.storage.autoRecord);
    vehiclePassRecordCheck_->setChecked(settings_.storage.vehiclePassRecord);
    maxVideoSegmentSpin_->setValue(settings_.storage.maxVideoSegmentMb);

    startWithSystemCheck_->setChecked(settings_.maintenance.startWithSystem);
    dailySyncCheck_->setChecked(settings_.maintenance.enableDailyDeviceTimeSync);
    dailySyncTimeEdit_->setTime(settings_.maintenance.dailySyncTime);
    shutdownCheck_->setChecked(settings_.maintenance.enableScheduledShutdown);
    shutdownTimeEdit_->setTime(settings_.maintenance.shutdownTime);
    diskMaintenanceCheck_->setChecked(settings_.maintenance.enableDiskMaintenance);
    expireCaptureDaysSpin_->setValue(settings_.maintenance.expireCaptureDays);
    expireVideoDaysSpin_->setValue(settings_.maintenance.expireVideoDays);
    minFreeSpaceSpin_->setValue(settings_.maintenance.minFreeSpaceGb);
    deleteOldestCheck_->setChecked(settings_.maintenance.deleteOldestWhenLowSpace);
}

void SystemSettingsDialog::applyToSettings()
{
    settings_.ui.startMaximized = startMaximizedCheck_->isChecked();
    settings_.ui.autoListenDeviceData = autoListenCheck_->isChecked();
    settings_.ui.autoConnectOnStart = autoConnectCheck_->isChecked();
    settings_.ui.fontFamily = fontCombo_->currentFont().family();
    settings_.ui.fontPointSize = fontSizeSpin_->value();
    settings_.ui.captureListMaxRows = cacheRowsSpin_->value();
    settings_.ui.captureListFields.clear();
    for (auto it = columnChecks_.begin(); it != columnChecks_.end(); ++it) {
        if (it.value()->isChecked()) {
            settings_.ui.captureListFields.append(it.key());
        }
    }
    settings_.ui.previewFrameRate = frameRateSpin_->value();
    settings_.ui.showOnlyVehicleFrames = onlyVehicleFramesCheck_->isChecked();
    settings_.ui.overlaySpeed = overlaySpeedCheck_->isChecked();
    settings_.ui.showCalibrationLines = calibrationLinesCheck_->isChecked();
    settings_.ui.plateImagePosition = platePositionCombo_->currentText();
    settings_.ui.multiVideoSplit = multiSplitCheck_->isChecked();
    settings_.ui.stopVideoQueryWhenMinimized = stopVideoWhenMinimizedCheck_->isChecked();
    settings_.ui.showSuccessPopup = successPopupCheck_->isChecked();

    settings_.storage.rootPath = rootPathEdit_->text().trimmed();
    settings_.storage.normalImageTemplate = normalImageTemplateEdit_->text().trimmed();
    settings_.storage.overspeedImageTemplate = overspeedImageTemplateEdit_->text().trimmed();
    settings_.storage.watchedVehicleImageTemplate = watchedImageTemplateEdit_->text().trimmed();
    settings_.storage.violationVideoTemplate = violationVideoTemplateEdit_->text().trimmed();
    settings_.storage.plateCloseupTemplate = plateCloseupTemplateEdit_->text().trimmed();
    settings_.storage.testDeviceAssetTemplate = testAssetTemplateEdit_->text().trimmed();
    settings_.storage.regularVideoTemplate = regularVideoTemplateEdit_->text().trimmed();
    settings_.storage.indexDigits = indexDigitsSpin_->value();
    settings_.storage.syncTextInfoFile = textInfoCheck_->isChecked();
    settings_.storage.savePlateCloseup = savePlateCloseupCheck_->isChecked();
    settings_.storage.mergePanoramaAndPlate = mergeImagesCheck_->isChecked();
    settings_.storage.autoRecord = autoRecordCheck_->isChecked();
    settings_.storage.vehiclePassRecord = vehiclePassRecordCheck_->isChecked();
    settings_.storage.maxVideoSegmentMb = maxVideoSegmentSpin_->value();

    settings_.maintenance.startWithSystem = startWithSystemCheck_->isChecked();
    settings_.maintenance.enableDailyDeviceTimeSync = dailySyncCheck_->isChecked();
    settings_.maintenance.dailySyncTime = dailySyncTimeEdit_->time();
    settings_.maintenance.enableScheduledShutdown = shutdownCheck_->isChecked();
    settings_.maintenance.shutdownTime = shutdownTimeEdit_->time();
    settings_.maintenance.enableDiskMaintenance = diskMaintenanceCheck_->isChecked();
    settings_.maintenance.expireCaptureDays = expireCaptureDaysSpin_->value();
    settings_.maintenance.expireVideoDays = expireVideoDaysSpin_->value();
    settings_.maintenance.minFreeSpaceGb = minFreeSpaceSpin_->value();
    settings_.maintenance.deleteOldestWhenLowSpace = deleteOldestCheck_->isChecked();
}

QLineEdit* SystemSettingsDialog::lineEdit(const QString& text)
{
    return new QLineEdit(text, this);
}

QSpinBox* SystemSettingsDialog::spinBox(int min, int max, int value, const QString& suffix)
{
    auto* box = new QSpinBox(this);
    box->setRange(min, max);
    box->setValue(value);
    box->setSuffix(suffix);
    return box;
}

QCheckBox* SystemSettingsDialog::checkBox(const QString& text, bool checked)
{
    auto* box = new QCheckBox(text, this);
    box->setChecked(checked);
    return box;
}

QComboBox* SystemSettingsDialog::comboBox(const QStringList& values, const QString& current)
{
    auto* box = new QComboBox(this);
    box->addItems(values);
    box->setCurrentText(current);
    return box;
}
