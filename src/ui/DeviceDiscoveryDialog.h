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
    int ensureRow(const QString& deviceId);
    int rowForDevice(const QString& deviceId) const;
    QString selectedDeviceId() const;
    void selectInitialDevice();
    // 在"当前"列打 ● 标记并给整行加底色，让多设备场景一眼看出正在预览哪一台。
    void refreshCurrentDeviceColumn();
    void setMessage(const QString& message, bool error = false);

    rv1126b::DeviceIntegrationController* controller_ = nullptr;
    QTableWidget* deviceTable_ = nullptr;
    QLineEdit* tokenEdit_ = nullptr;
    QLabel* tokenHint_ = nullptr;
    QLabel* messageLabel_ = nullptr;
    QPushButton* searchButton_ = nullptr;
    QPushButton* connectButton_ = nullptr;
    QPushButton* forgetButton_ = nullptr;
    QLineEdit* manualIpEdit_ = nullptr;
    QSpinBox* manualPortSpin_ = nullptr;
    QString initialDeviceId_;
    // 用户主动发起连接后才有值；用来区分"用户点了连接"和"设备本来就在线"，
    // 后者不允许自动关闭对话框。
    QString awaitingConnectDeviceId_;
    bool userPickedRow_ = false;
    QHash<QString, rv1126b::DeviceSessionState> sessionStates_;
};
