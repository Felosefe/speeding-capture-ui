#pragma once

#include "../models/SystemSettings.h"

#include <QDialog>
#include <QMap>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QFontComboBox;
class QLineEdit;
class QSpinBox;
class QStackedWidget;
class QTimeEdit;
class QTreeWidget;
class QWidget;

class SystemSettingsDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit SystemSettingsDialog(const SystemSettings& settings, QWidget* parent = nullptr);

    SystemSettings settings() const;

private:
    QWidget* createUiPage();
    QWidget* createStoragePage();
    QWidget* createMaintenancePage();
    void addPage(const QString& title, QWidget* page);
    void loadFromSettings();
    void applyToSettings();
    QLineEdit* lineEdit(const QString& text = QString());
    QSpinBox* spinBox(int min, int max, int value, const QString& suffix = QString());
    QCheckBox* checkBox(const QString& text, bool checked);
    QComboBox* comboBox(const QStringList& values, const QString& current);

    SystemSettings settings_;
    QTreeWidget* tree_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QDialogButtonBox* buttonBox_ = nullptr;

    QCheckBox* startMaximizedCheck_ = nullptr;
    QCheckBox* autoListenCheck_ = nullptr;
    QCheckBox* autoConnectCheck_ = nullptr;
    QFontComboBox* fontCombo_ = nullptr;
    QSpinBox* fontSizeSpin_ = nullptr;
    QSpinBox* cacheRowsSpin_ = nullptr;
    QMap<QString, QCheckBox*> columnChecks_;
    QSpinBox* frameRateSpin_ = nullptr;
    QCheckBox* onlyVehicleFramesCheck_ = nullptr;
    QCheckBox* overlaySpeedCheck_ = nullptr;
    QCheckBox* calibrationLinesCheck_ = nullptr;
    QComboBox* platePositionCombo_ = nullptr;
    QCheckBox* multiSplitCheck_ = nullptr;
    QCheckBox* stopVideoWhenMinimizedCheck_ = nullptr;
    QCheckBox* successPopupCheck_ = nullptr;

    QLineEdit* rootPathEdit_ = nullptr;
    QLineEdit* normalImageTemplateEdit_ = nullptr;
    QLineEdit* overspeedImageTemplateEdit_ = nullptr;
    QLineEdit* watchedImageTemplateEdit_ = nullptr;
    QLineEdit* violationVideoTemplateEdit_ = nullptr;
    QLineEdit* plateCloseupTemplateEdit_ = nullptr;
    QLineEdit* testAssetTemplateEdit_ = nullptr;
    QLineEdit* regularVideoTemplateEdit_ = nullptr;
    QSpinBox* indexDigitsSpin_ = nullptr;
    QCheckBox* textInfoCheck_ = nullptr;
    QCheckBox* savePlateCloseupCheck_ = nullptr;
    QCheckBox* mergeImagesCheck_ = nullptr;
    QCheckBox* autoRecordCheck_ = nullptr;
    QCheckBox* vehiclePassRecordCheck_ = nullptr;
    QSpinBox* maxVideoSegmentSpin_ = nullptr;

    QCheckBox* startWithSystemCheck_ = nullptr;
    QCheckBox* dailySyncCheck_ = nullptr;
    QTimeEdit* dailySyncTimeEdit_ = nullptr;
    QCheckBox* shutdownCheck_ = nullptr;
    QTimeEdit* shutdownTimeEdit_ = nullptr;
    QCheckBox* diskMaintenanceCheck_ = nullptr;
    QSpinBox* expireCaptureDaysSpin_ = nullptr;
    QSpinBox* expireVideoDaysSpin_ = nullptr;
    QSpinBox* minFreeSpaceSpin_ = nullptr;
    QCheckBox* deleteOldestCheck_ = nullptr;
};
