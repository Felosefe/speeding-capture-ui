#pragma once

#include "../models/Device.h"
#include "../models/DeviceConfig.h"

#include <QDialog>
#include <QList>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTreeWidget;

class DeviceConfigDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit DeviceConfigDialog(const Device& device, QWidget* parent = nullptr);

    DeviceConfig config() const;

private:
    QWidget* createCameraPage();
    QWidget* createSecurityPage();
    QWidget* createHardwarePage();
    QWidget* createAlgorithmPage();
    QWidget* createAdvancedPage();

    void addPage(const QString& title, QWidget* page);
    void loadFromDevice();
    void applyToConfig();
    void setEditorEnabled(bool enabled);
    void showFunctionMenu();

    QLineEdit* readOnlyLineEdit(const QString& text);
    QLineEdit* editableLineEdit(const QString& text = QString());
    QSpinBox* spinBox(int min, int max, int value, const QString& suffix = QString());
    QDoubleSpinBox* doubleSpinBox(double min, double max, double value, const QString& suffix = QString());
    QComboBox* comboBox(const QStringList& values, const QString& current);
    QCheckBox* checkBox(const QString& text, bool checked);

    Device device_;
    DeviceConfig config_;
    bool editable_ = false;

    QTreeWidget* tree_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QDialogButtonBox* buttonBox_ = nullptr;
    QPushButton* functionButton_ = nullptr;
    QList<QWidget*> editors_;

    QLineEdit* locationEdit_ = nullptr;
    QComboBox* directionCombo_ = nullptr;
    QLineEdit* laneEdit_ = nullptr;
    QSpinBox* speedLimitSpin_ = nullptr;
    QCheckBox* savePlateImageCheck_ = nullptr;
    QCheckBox* ntpCheck_ = nullptr;
    QLineEdit* cameraModelEdit_ = nullptr;
    QComboBox* streamResolutionCombo_ = nullptr;
    QSpinBox* previewFrameRateSpin_ = nullptr;

    QLineEdit* adminUserEdit_ = nullptr;
    QCheckBox* remoteAuthCheck_ = nullptr;
    QLineEdit* ipWhitelistEdit_ = nullptr;

    QLineEdit* radarModelEdit_ = nullptr;
    QDoubleSpinBox* speedCalibrationSpin_ = nullptr;
    QSpinBox* lowSpeedFilterSpin_ = nullptr;
    QCheckBox* flashCheck_ = nullptr;

    QSpinBox* recognitionLeftSpin_ = nullptr;
    QSpinBox* recognitionTopSpin_ = nullptr;
    QSpinBox* recognitionRightSpin_ = nullptr;
    QSpinBox* recognitionBottomSpin_ = nullptr;
    QCheckBox* overspeedAlertCheck_ = nullptr;
    QCheckBox* saveUnknownPlateCheck_ = nullptr;
    QCheckBox* blacklistCheck_ = nullptr;
    QLineEdit* violationNameEdit_ = nullptr;
    QLineEdit* violationCodeEdit_ = nullptr;

    QSpinBox* preRecordSpin_ = nullptr;
    QSpinBox* postRecordSpin_ = nullptr;
    QSpinBox* jpegQualitySpin_ = nullptr;
    QCheckBox* rtspCheck_ = nullptr;
    QCheckBox* uploadCheck_ = nullptr;
    QLineEdit* uploadServerEdit_ = nullptr;
    QSpinBox* uploadPortSpin_ = nullptr;
};
