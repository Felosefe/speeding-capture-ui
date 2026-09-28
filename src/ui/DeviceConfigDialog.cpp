#include "DeviceConfigDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStringList>
#include <QTreeWidget>
#include <QVBoxLayout>

DeviceConfigDialog::DeviceConfigDialog(const Device& device, QWidget* parent)
    : QDialog(parent)
    , device_(device)
    , config_(device.config)
    , editable_(device.status.connectionState == DeviceConnectionState::Online)
{
    setWindowTitle(QStringLiteral("设备配置 - %1").arg(device_.name));
    resize(860, 620);
    setMinimumSize(760, 520);

    tree_ = new QTreeWidget(this);
    tree_->setHeaderHidden(true);
    tree_->setFixedWidth(170);

    stack_ = new QStackedWidget(this);
    addPage(QStringLiteral("相机"), createCameraPage());
    addPage(QStringLiteral("安全"), createSecurityPage());
    addPage(QStringLiteral("硬件"), createHardwarePage());
    addPage(QStringLiteral("算法"), createAlgorithmPage());
    addPage(QStringLiteral("高级选项"), createAdvancedPage());

    connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* current) {
        if (!current) {
            return;
        }
        stack_->setCurrentIndex(current->data(0, Qt::UserRole).toInt());
    });

    auto* contentLayout = new QHBoxLayout;
    contentLayout->addWidget(tree_);
    contentLayout->addWidget(stack_, 1);

    if (auto* first = tree_->topLevelItem(0)) {
        tree_->setCurrentItem(first);
    }

    auto* hint = new QLabel(this);
    hint->setWordWrap(true);
    hint->setText(editable_
                      ? QStringLiteral("当前设备在线，修改后点击“确定”会下发到设备并保存到本地配置文件。")
                      : QStringLiteral("当前设备离线，配置参数仅可查看；连接设备后才能编辑并下发。"));
    hint->setStyleSheet(editable_
                            ? QStringLiteral("color:#44515f; padding:4px 0;")
                            : QStringLiteral("color:#9b4b24; padding:4px 0;"));

    buttonBox_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttonBox_->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定"));
    buttonBox_->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    buttonBox_->button(QDialogButtonBox::Ok)->setEnabled(editable_);

    functionButton_ = new QPushButton(QStringLiteral("功能"), this);
    buttonBox_->addButton(functionButton_, QDialogButtonBox::ActionRole);

    connect(functionButton_, &QPushButton::clicked, this, &DeviceConfigDialog::showFunctionMenu);
    connect(buttonBox_, &QDialogButtonBox::accepted, this, [this]() {
        applyToConfig();
        accept();
    });
    connect(buttonBox_, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->addWidget(hint);
    rootLayout->addLayout(contentLayout, 1);
    rootLayout->addWidget(buttonBox_);

    loadFromDevice();
    setEditorEnabled(editable_);
}

DeviceConfig DeviceConfigDialog::config() const
{
    return config_;
}

QWidget* DeviceConfigDialog::createCameraPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    form->setLabelAlignment(Qt::AlignRight);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    form->addRow(QStringLiteral("设备编号"), readOnlyLineEdit(device_.id));
    form->addRow(QStringLiteral("设备名称"), readOnlyLineEdit(device_.name));
    form->addRow(QStringLiteral("相机 IP"), readOnlyLineEdit(device_.ipAddress));
    form->addRow(QStringLiteral("通信端口"), readOnlyLineEdit(QString::number(device_.port)));

    cameraModelEdit_ = editableLineEdit();
    locationEdit_ = editableLineEdit();
    laneEdit_ = editableLineEdit();
    directionCombo_ = comboBox({QStringLiteral("由北向南"), QStringLiteral("由南向北"), QStringLiteral("由东向西"), QStringLiteral("由西向东")}, QString());
    speedLimitSpin_ = spinBox(5, 240, 60, QStringLiteral(" km/h"));
    ntpCheck_ = checkBox(QStringLiteral("启用 NTP 自动时间同步"), true);
    streamResolutionCombo_ = comboBox({QStringLiteral("3840x2160"), QStringLiteral("1920x1080"), QStringLiteral("1280x720")}, QStringLiteral("1920x1080"));
    previewFrameRateSpin_ = spinBox(1, 60, 25, QStringLiteral(" fps"));

    form->addRow(QStringLiteral("相机型号"), cameraModelEdit_);
    form->addRow(QStringLiteral("卡口地点"), locationEdit_);
    form->addRow(QStringLiteral("车道名称"), laneEdit_);
    form->addRow(QStringLiteral("通行方向"), directionCombo_);
    form->addRow(QStringLiteral("道路限速"), speedLimitSpin_);
    form->addRow(QStringLiteral("时间同步"), ntpCheck_);
    form->addRow(QStringLiteral("预览分辨率"), streamResolutionCombo_);
    form->addRow(QStringLiteral("预览帧率"), previewFrameRateSpin_);
    form->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));
    return page;
}

QWidget* DeviceConfigDialog::createSecurityPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    form->setLabelAlignment(Qt::AlignRight);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    adminUserEdit_ = editableLineEdit();
    remoteAuthCheck_ = checkBox(QStringLiteral("启用远程运维鉴权"), true);
    ipWhitelistEdit_ = editableLineEdit();

    form->addRow(QStringLiteral("管理员账号"), adminUserEdit_);
    form->addRow(QStringLiteral("远程鉴权"), remoteAuthCheck_);
    form->addRow(QStringLiteral("IP 白名单"), ipWhitelistEdit_);
    form->addRow(QStringLiteral("RTSP 子账号"), readOnlyLineEdit(QStringLiteral("preview")));
    form->addRow(QStringLiteral("视频流加密"), readOnlyLineEdit(QStringLiteral("后续阶段接入")));
    form->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));
    return page;
}

QWidget* DeviceConfigDialog::createHardwarePage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    form->setLabelAlignment(Qt::AlignRight);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    radarModelEdit_ = editableLineEdit();
    speedCalibrationSpin_ = doubleSpinBox(0.50, 1.50, 1.0);
    lowSpeedFilterSpin_ = spinBox(0, 80, 10, QStringLiteral(" km/h"));
    flashCheck_ = checkBox(QStringLiteral("启用超级闪光补光"), true);

    form->addRow(QStringLiteral("雷达型号"), radarModelEdit_);
    form->addRow(QStringLiteral("绑定串口"), readOnlyLineEdit(QStringLiteral("COM2 / 9600")));
    form->addRow(QStringLiteral("测速协议"), readOnlyLineEdit(QStringLiteral("模拟协议 V1")));
    form->addRow(QStringLiteral("测速校正系数"), speedCalibrationSpin_);
    form->addRow(QStringLiteral("低速过滤阈值"), lowSpeedFilterSpin_);
    form->addRow(QStringLiteral("补光控制"), flashCheck_);
    form->addRow(QStringLiteral("硬件看门狗"), readOnlyLineEdit(QStringLiteral("每日 03:00 自动巡检")));
    form->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));
    return page;
}

QWidget* DeviceConfigDialog::createAlgorithmPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    form->setLabelAlignment(Qt::AlignRight);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    recognitionLeftSpin_ = spinBox(0, 2000, 120, QStringLiteral(" px"));
    recognitionTopSpin_ = spinBox(0, 2000, 160, QStringLiteral(" px"));
    recognitionRightSpin_ = spinBox(0, 2000, 120, QStringLiteral(" px"));
    recognitionBottomSpin_ = spinBox(0, 2000, 160, QStringLiteral(" px"));
    overspeedAlertCheck_ = checkBox(QStringLiteral("启用超速违法检测"), true);
    saveUnknownPlateCheck_ = checkBox(QStringLiteral("保存未知车牌记录"), true);
    blacklistCheck_ = checkBox(QStringLiteral("启用关注车辆归档"), true);
    violationNameEdit_ = editableLineEdit();
    violationCodeEdit_ = editableLineEdit();

    form->addRow(QStringLiteral("识别区域左边距"), recognitionLeftSpin_);
    form->addRow(QStringLiteral("识别区域上边距"), recognitionTopSpin_);
    form->addRow(QStringLiteral("识别区域右边距"), recognitionRightSpin_);
    form->addRow(QStringLiteral("识别区域下边距"), recognitionBottomSpin_);
    form->addRow(QStringLiteral("超速检测"), overspeedAlertCheck_);
    form->addRow(QStringLiteral("未知车牌"), saveUnknownPlateCheck_);
    form->addRow(QStringLiteral("黑名单关注"), blacklistCheck_);
    form->addRow(QStringLiteral("违法名称"), violationNameEdit_);
    form->addRow(QStringLiteral("违法代码"), violationCodeEdit_);
    form->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));
    return page;
}

QWidget* DeviceConfigDialog::createAdvancedPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    form->setLabelAlignment(Qt::AlignRight);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    preRecordSpin_ = spinBox(0, 30, 3, QStringLiteral(" s"));
    postRecordSpin_ = spinBox(0, 30, 5, QStringLiteral(" s"));
    jpegQualitySpin_ = spinBox(10, 100, 90, QStringLiteral(" %"));
    savePlateImageCheck_ = checkBox(QStringLiteral("保存车牌小图"), true);
    rtspCheck_ = checkBox(QStringLiteral("启用 RTSP 服务输出"), true);
    uploadCheck_ = checkBox(QStringLiteral("启用第三方平台上传"), false);
    uploadServerEdit_ = editableLineEdit();
    uploadPortSpin_ = spinBox(1, 65535, 9000);

    form->addRow(QStringLiteral("抓拍前预录"), preRecordSpin_);
    form->addRow(QStringLiteral("抓拍后延录"), postRecordSpin_);
    form->addRow(QStringLiteral("图片压缩质量"), jpegQualitySpin_);
    form->addRow(QStringLiteral("车牌小图"), savePlateImageCheck_);
    form->addRow(QStringLiteral("次码流 RTSP"), rtspCheck_);
    form->addRow(QStringLiteral("平台上传"), uploadCheck_);
    form->addRow(QStringLiteral("服务器 IP"), uploadServerEdit_);
    form->addRow(QStringLiteral("服务器端口"), uploadPortSpin_);
    form->addRow(QStringLiteral("本地命名模板"), readOnlyLineEdit(QStringLiteral("后续存储阶段实现")));
    form->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));
    return page;
}

void DeviceConfigDialog::addPage(const QString& title, QWidget* page)
{
    const int index = stack_->addWidget(page);
    auto* item = new QTreeWidgetItem(tree_, QStringList{title});
    item->setData(0, Qt::UserRole, index);
    tree_->addTopLevelItem(item);
}

void DeviceConfigDialog::loadFromDevice()
{
    cameraModelEdit_->setText(config_.cameraModel);
    locationEdit_->setText(config_.location);
    laneEdit_->setText(config_.laneName);
    directionCombo_->setCurrentText(config_.direction);
    speedLimitSpin_->setValue(config_.speedLimitKmh);
    ntpCheck_->setChecked(config_.ntpEnabled);
    streamResolutionCombo_->setCurrentText(config_.streamResolution);
    previewFrameRateSpin_->setValue(config_.previewFrameRate);

    adminUserEdit_->setText(config_.adminUser);
    remoteAuthCheck_->setChecked(config_.remoteAuthEnabled);
    ipWhitelistEdit_->setText(config_.ipWhitelist);

    radarModelEdit_->setText(config_.radarModel);
    speedCalibrationSpin_->setValue(config_.speedCalibration);
    lowSpeedFilterSpin_->setValue(config_.lowSpeedFilterKmh);
    flashCheck_->setChecked(config_.flashEnabled);

    recognitionLeftSpin_->setValue(config_.recognitionMarginLeft);
    recognitionTopSpin_->setValue(config_.recognitionMarginTop);
    recognitionRightSpin_->setValue(config_.recognitionMarginRight);
    recognitionBottomSpin_->setValue(config_.recognitionMarginBottom);
    overspeedAlertCheck_->setChecked(config_.enableOverspeedAlert);
    saveUnknownPlateCheck_->setChecked(config_.saveUnknownPlate);
    blacklistCheck_->setChecked(config_.enableBlacklist);
    violationNameEdit_->setText(config_.overspeedViolationName);
    violationCodeEdit_->setText(config_.overspeedViolationCode);

    preRecordSpin_->setValue(config_.preRecordSeconds);
    postRecordSpin_->setValue(config_.postRecordSeconds);
    jpegQualitySpin_->setValue(config_.jpegQuality);
    savePlateImageCheck_->setChecked(config_.savePlateImage);
    rtspCheck_->setChecked(config_.rtspEnabled);
    uploadCheck_->setChecked(config_.uploadEnabled);
    uploadServerEdit_->setText(config_.uploadServer);
    uploadPortSpin_->setValue(config_.uploadPort);
}

void DeviceConfigDialog::applyToConfig()
{
    config_.cameraModel = cameraModelEdit_->text().trimmed();
    config_.location = locationEdit_->text().trimmed();
    config_.laneName = laneEdit_->text().trimmed();
    config_.direction = directionCombo_->currentText();
    config_.speedLimitKmh = speedLimitSpin_->value();
    config_.ntpEnabled = ntpCheck_->isChecked();
    config_.streamResolution = streamResolutionCombo_->currentText();
    config_.previewFrameRate = previewFrameRateSpin_->value();

    config_.adminUser = adminUserEdit_->text().trimmed();
    config_.remoteAuthEnabled = remoteAuthCheck_->isChecked();
    config_.ipWhitelist = ipWhitelistEdit_->text().trimmed();

    config_.radarModel = radarModelEdit_->text().trimmed();
    config_.speedCalibration = speedCalibrationSpin_->value();
    config_.lowSpeedFilterKmh = lowSpeedFilterSpin_->value();
    config_.flashEnabled = flashCheck_->isChecked();

    config_.recognitionMarginLeft = recognitionLeftSpin_->value();
    config_.recognitionMarginTop = recognitionTopSpin_->value();
    config_.recognitionMarginRight = recognitionRightSpin_->value();
    config_.recognitionMarginBottom = recognitionBottomSpin_->value();
    config_.enableOverspeedAlert = overspeedAlertCheck_->isChecked();
    config_.saveUnknownPlate = saveUnknownPlateCheck_->isChecked();
    config_.enableBlacklist = blacklistCheck_->isChecked();
    config_.overspeedViolationName = violationNameEdit_->text().trimmed();
    config_.overspeedViolationCode = violationCodeEdit_->text().trimmed();

    config_.preRecordSeconds = preRecordSpin_->value();
    config_.postRecordSeconds = postRecordSpin_->value();
    config_.jpegQuality = jpegQualitySpin_->value();
    config_.savePlateImage = savePlateImageCheck_->isChecked();
    config_.rtspEnabled = rtspCheck_->isChecked();
    config_.uploadEnabled = uploadCheck_->isChecked();
    config_.uploadServer = uploadServerEdit_->text().trimmed();
    config_.uploadPort = uploadPortSpin_->value();
}

void DeviceConfigDialog::setEditorEnabled(bool enabled)
{
    for (QWidget* editor : editors_) {
        editor->setEnabled(enabled);
    }
}

void DeviceConfigDialog::showFunctionMenu()
{
    QMessageBox::information(
        this,
        QStringLiteral("功能"),
        QStringLiteral("高级调试、配置导入导出、恢复出厂参数将在后续阶段继续补齐。"));
}

QLineEdit* DeviceConfigDialog::readOnlyLineEdit(const QString& text)
{
    auto* edit = new QLineEdit(text, this);
    edit->setReadOnly(true);
    return edit;
}

QLineEdit* DeviceConfigDialog::editableLineEdit(const QString& text)
{
    auto* edit = new QLineEdit(text, this);
    editors_.append(edit);
    return edit;
}

QSpinBox* DeviceConfigDialog::spinBox(int min, int max, int value, const QString& suffix)
{
    auto* box = new QSpinBox(this);
    box->setRange(min, max);
    box->setValue(value);
    box->setSuffix(suffix);
    editors_.append(box);
    return box;
}

QDoubleSpinBox* DeviceConfigDialog::doubleSpinBox(double min, double max, double value, const QString& suffix)
{
    auto* box = new QDoubleSpinBox(this);
    box->setRange(min, max);
    box->setDecimals(2);
    box->setSingleStep(0.01);
    box->setValue(value);
    box->setSuffix(suffix);
    editors_.append(box);
    return box;
}

QComboBox* DeviceConfigDialog::comboBox(const QStringList& values, const QString& current)
{
    auto* box = new QComboBox(this);
    box->addItems(values);
    if (!current.isEmpty()) {
        box->setCurrentText(current);
    }
    editors_.append(box);
    return box;
}

QCheckBox* DeviceConfigDialog::checkBox(const QString& text, bool checked)
{
    auto* box = new QCheckBox(text, this);
    box->setChecked(checked);
    editors_.append(box);
    return box;
}
