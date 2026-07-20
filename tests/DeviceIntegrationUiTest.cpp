#include "../src/rv1126b/application/DeviceIntegrationController.h"
#include "../src/ui/DeviceDiscoveryDialog.h"
#include "../src/ui/LivePreviewPanel.h"

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QWidget>
#include <QtTest>

using namespace rv1126b;

class UiFakeDiscovery final : public DeviceDiscoveryService
{
public:
    RequestId startScan(int) override
    {
        ++startCount;
        scanId = QUuid::createUuid();
        return scanId;
    }

    void cancelScan(const RequestId&) override { ++cancelCount; }
    void cancelAll() override { ++cancelAllCount; }

    void send(const DiscoveredDeviceDto& device) { emit deviceFound(scanId, device); }
    void finish() { emit scanFinished(scanId); }

    RequestId scanId;
    int startCount = 0;
    int cancelCount = 0;
    int cancelAllCount = 0;
};

class UiFakeSecrets final : public ISecretStore
{
public:
    QString credentialKeyForDevice(const QString& deviceId) const override
    {
        return QStringLiteral("RV1126B/%1").arg(deviceId);
    }

    ApiResult<QString> storeToken(const QString& deviceId, SecretValue token) override
    {
        lastToken = QByteArray(token.view().data(), token.view().size());
        return ApiResult<QString>::success(credentialKeyForDevice(deviceId));
    }

    ApiResult<SecretValue> loadToken(const QString&) const override
    {
        return ApiResult<SecretValue>::success(SecretValue {});
    }

    ApiResult<void> removeToken(const QString&) override
    {
        return ApiResult<void>::success();
    }

    QByteArray lastToken;
};

class UiFakeFleet final : public DeviceFleetService
{
public:
    QVector<DeviceSessionSnapshot> sessions() const override { return snapshots.values(); }

    bool upsertDevice(const DeviceProfile& profile) override
    {
        lastProfile = profile;
        DeviceSessionSnapshot value;
        value.profile = profile;
        snapshots.insert(profile.deviceId, value);
        return true;
    }

    bool connectDevice(const QString&) override
    {
        ++connectCount;
        return true;
    }

    void disconnectDevice(const QString&) override {}
    void disconnectAll() override { ++disconnectAllCount; }

    bool selectVideoDevice(const QString& deviceId) override
    {
        selectedId = deviceId;
        emit selectedVideoDeviceChanged(deviceId);
        return true;
    }

    QString selectedVideoDeviceId() const override { return selectedId; }

    void sendState(DeviceSessionState state)
    {
        DeviceSessionSnapshot value;
        value.profile = lastProfile;
        value.state = state;
        snapshots.insert(value.profile.deviceId, value);
        emit sessionChanged(value);
    }

    QHash<QString, DeviceSessionSnapshot> snapshots;
    DeviceProfile lastProfile;
    QString selectedId;
    int connectCount = 0;
    int disconnectAllCount = 0;
};

class UiFakePlayer final : public IRtspPlayer
{
public:
    UiFakePlayer()
        : widget(new QWidget)
    {
    }

    ~UiFakePlayer() override
    {
        if (widget && !widget->parent()) {
            delete widget;
        }
    }

    void open(const RtspStreamSpec& value) override
    {
        stream = value;
        stateValue = RtspPlayerState::Opening;
        emit stateChanged(stateValue);
    }

    void stop() override
    {
        stateValue = RtspPlayerState::Stopped;
        emit stateChanged(stateValue);
    }

    RtspPlayerState state() const override { return stateValue; }
    QWidget* outputWidget() const override { return widget; }

    QPointer<QWidget> widget;
    RtspStreamSpec stream;
    RtspPlayerState stateValue = RtspPlayerState::Idle;
};

class DeviceIntegrationUiTest final : public QObject
{
    Q_OBJECT

private slots:
    void discoveryDialogManagesBusySelectionAndSecretInput();
    void discoveryDialogShowsAuthenticationFailure();
    void unavailableDialogDisablesNetworkActions();
    void livePanelEmbedsOutputAndSelectsStreams();

private:
    static DiscoveredDeviceDto discovered();
};

void DeviceIntegrationUiTest::discoveryDialogManagesBusySelectionAndSecretInput()
{
    UiFakeDiscovery discoveryService;
    UiFakeFleet fleet;
    UiFakeSecrets secrets;
    UiFakePlayer player;
    DeviceIntegrationController controller({&discoveryService, &fleet, &secrets, &player});
    DeviceDiscoveryDialog dialog(&controller);
    dialog.show();

    auto* searchButton = dialog.findChild<QPushButton*>(QStringLiteral("searchDevicesButton"));
    auto* connectButton = dialog.findChild<QPushButton*>(QStringLiteral("connectDiscoveredDeviceButton"));
    auto* tokenEdit = dialog.findChild<QLineEdit*>(QStringLiteral("bearerTokenEdit"));
    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("discoveredDeviceTable"));
    QVERIFY(searchButton);
    QVERIFY(connectButton);
    QVERIFY(tokenEdit);
    QVERIFY(table);

    QTRY_COMPARE(discoveryService.startCount, 1);
    QCOMPARE(searchButton->text(), QStringLiteral("正在搜索…"));
    QVERIFY(!searchButton->isEnabled());
    QCOMPARE(tokenEdit->echoMode(), QLineEdit::Password);

    discoveryService.send(discovered());
    QTRY_COMPARE(table->rowCount(), 1);
    table->selectRow(0);
    QTRY_VERIFY(connectButton->isEnabled());
    tokenEdit->setText(QStringLiteral("ui-secret-token"));
    QTest::mouseClick(connectButton, Qt::LeftButton);

    QCOMPARE(tokenEdit->text(), QString());
    QCOMPARE(secrets.lastToken, QByteArrayLiteral("ui-secret-token"));
    QCOMPARE(fleet.connectCount, 1);

    discoveryService.finish();
    QTRY_VERIFY(searchButton->isEnabled());
    QCOMPARE(searchButton->text(), QStringLiteral("重新搜索"));
}

void DeviceIntegrationUiTest::discoveryDialogShowsAuthenticationFailure()
{
    UiFakeDiscovery discoveryService;
    UiFakeFleet fleet;
    UiFakeSecrets secrets;
    UiFakePlayer player;
    DeviceIntegrationController controller({&discoveryService, &fleet, &secrets, &player});
    DeviceDiscoveryDialog dialog(&controller);
    dialog.show();

    QTRY_COMPARE(discoveryService.startCount, 1);
    discoveryService.send(discovered());
    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("discoveredDeviceTable"));
    auto* tokenEdit = dialog.findChild<QLineEdit*>(QStringLiteral("bearerTokenEdit"));
    auto* connectButton = dialog.findChild<QPushButton*>(QStringLiteral("connectDiscoveredDeviceButton"));
    auto* message = dialog.findChild<QLabel*>(QStringLiteral("discoveryMessageLabel"));
    table->selectRow(0);
    tokenEdit->setText(QStringLiteral("wrong-token-must-not-appear"));
    QTest::mouseClick(connectButton, Qt::LeftButton);
    fleet.sendState(DeviceSessionState::AuthenticationFailed);

    QTRY_COMPARE(message->text(), QStringLiteral("认证失败，请重新配置 Token"));
    QVERIFY(connectButton->isEnabled());
    QVERIFY(!message->text().contains(QStringLiteral("wrong-token-must-not-appear")));
}

void DeviceIntegrationUiTest::unavailableDialogDisablesNetworkActions()
{
    UiFakePlayer player;
    DeviceIntegrationController controller({nullptr, nullptr, nullptr, &player});
    DeviceDiscoveryDialog dialog(&controller);

    auto* searchButton = dialog.findChild<QPushButton*>(QStringLiteral("searchDevicesButton"));
    auto* connectButton = dialog.findChild<QPushButton*>(QStringLiteral("connectDiscoveredDeviceButton"));
    auto* message = dialog.findChild<QLabel*>(QStringLiteral("discoveryMessageLabel"));
    QVERIFY(!searchButton->isEnabled());
    QVERIFY(!connectButton->isEnabled());
    QVERIFY(message->text().contains(QStringLiteral("尚未装配")));
}

void DeviceIntegrationUiTest::livePanelEmbedsOutputAndSelectsStreams()
{
    UiFakePlayer player;
    QPointer<QWidget> output = player.widget;
    {
        LivePreviewPanel panel(&player);
        panel.show();
        QCOMPARE(panel.findChild<QWidget*>(QStringLiteral("rtspOutputWidget")), output.data());

        auto* combo = panel.findChild<QComboBox*>(QStringLiteral("rtspStreamRoleCombo"));
        auto* state = panel.findChild<QLabel*>(QStringLiteral("livePlaybackStateLabel"));
        QVERIFY(combo);
        QCOMPARE(combo->currentData().toInt(), static_cast<int>(RtspStreamRole::Sub));

        int roleSignalCount = 0;
        RtspStreamRole emittedRole = RtspStreamRole::Sub;
        connect(&panel, &LivePreviewPanel::streamRoleChanged, &panel,
                [&](RtspStreamRole role) {
                    ++roleSignalCount;
                    emittedRole = role;
                });
        combo->setCurrentIndex(1);
        QCOMPARE(roleSignalCount, 1);
        QCOMPARE(emittedRole, RtspStreamRole::Main);

        panel.setPlaybackState(RtspPlayerState::Reconnecting);
        QCOMPARE(state->text(), QStringLiteral("视频重连中…"));
        panel.setCurrentDevice(QStringLiteral("device-a"));
        QVERIFY(panel.findChild<QLabel*>(QStringLiteral("liveDeviceLabel"))
                    ->text().contains(QStringLiteral("device-a")));
    }
    QVERIFY(output.isNull());
}

DiscoveredDeviceDto DeviceIntegrationUiTest::discovered()
{
    DiscoveredDeviceDto value;
    value.deviceId = QStringLiteral("device-a");
    value.deviceModel = QStringLiteral("RV1126B");
    value.ipv4 = QStringLiteral("192.0.2.10");
    value.apiVersion = QStringLiteral("v1");
    value.apiUrl = QUrl(QStringLiteral("http://192.0.2.10:18080/api/v1"));
    value.releaseVersion = QStringLiteral("test-release");
    value.authRequired = true;
    return value;
}

QTEST_MAIN(DeviceIntegrationUiTest)

#include "DeviceIntegrationUiTest.moc"
