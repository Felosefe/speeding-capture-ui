#pragma once

#include "../rv1126b/application/DeviceIntegrationController.h"

#include <QDialog>
#include <QHash>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QSpinBox;

class DeviceDiscoveryDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit DeviceDiscoveryDialog(rv1126b::DeviceIntegrationController* controller,
                                   QString initialDeviceId = {},
                                   QWidget* parent = nullptr);
    ~DeviceDiscoveryDialog() override;

private slots:
    void startScan();
    void connectSelectedDevice();
    void connectManualEndpoint();
    void forgetSelectedDevice();
    void updateSelection();
    void resetDevices();
    void upsertDevice(const rv1126b::DiscoveredDeviceDto& device);
    void upsertKnownDevice(const rv1126b::DeviceSessionSnapshot& snapshot);
    void updateScanState(bool scanning);
    void updateSession(const rv1126b::DeviceSessionSnapshot& snapshot);
    void showControllerError(const QString& code, const QString& message);

private:
    int rowForDevice(const QString& deviceId) const;
    QString selectedDeviceId() const;
    void selectInitialDevice();
    void setMessage(const QString& message, bool error = false);

    rv1126b::DeviceIntegrationController* controller_ = nullptr;
    QTableWidget* deviceTable_ = nullptr;
    QLineEdit* tokenEdit_ = nullptr;
    QLabel* messageLabel_ = nullptr;
    QPushButton* searchButton_ = nullptr;
    QPushButton* connectButton_ = nullptr;
    QPushButton* forgetButton_ = nullptr;
    QLineEdit* manualIpEdit_ = nullptr;
    QSpinBox* manualPortSpin_ = nullptr;
    QString initialDeviceId_;
    QHash<QString, rv1126b::DeviceSessionState> sessionStates_;
};

