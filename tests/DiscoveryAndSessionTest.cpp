#include "../src/rv1126b/network/UdpDeviceDiscoveryService.h"
#include "../src/rv1126b/protocol/BoardApiCodec.h"
#include "../src/rv1126b/services/BoardDeviceSession.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkDatagram>
#include <QSignalSpy>
#include <QUdpSocket>
#include <QtTest>

#include <optional>

using namespace rv1126b;

namespace {

ApiError makeError(ApiErrorCategory category, bool retryable = false)
{
    ApiError error;
    error.code = QStringLiteral("test_error");
    error.category = category;
    error.retryable = retryable;
    return error;
}

class FakeBoardApiClient final : public IBoardApiClient
{
public:
    explicit FakeBoardApiClient(QObject* parent = nullptr)
        : IBoardApiClient(parent)
    {
    }

    RequestId getHealth(QObject*, ApiCompletion<HealthDto> completion) override
    {
        ++healthRequestCount;
        healthRequestId = RequestId::createUuid();
        healthCompletion = std::move(completion);
        return healthRequestId;
    }

    void respondHealth(ApiResult<HealthDto> result)
    {
        QVERIFY(healthCompletion.has_value());
        ApiCompletion<HealthDto> completion = std::move(*healthCompletion);
        healthCompletion.reset();
        QTimer::singleShot(0, this, [completion = std::move(completion), result = std::move(result)]() mutable {
            completion(std::move(result));
        });
    }

    RequestId listEvents(int, const std::optional<QString>&, QObject*, ApiCompletion<EventPageDto>) override { return RequestId::createUuid(); }
    RequestId getEventDetail(const EventIdentity&, QObject*, ApiCompletion<EventDetailDto>) override { return RequestId::createUuid(); }
    RequestId downloadEvidenceToPartFile(const EventIdentity&, const QString&, const QString&, QObject*, ApiCompletion<EvidenceDownloadResult>) override { return RequestId::createUuid(); }
    RequestId getEvidenceConfig(QObject*, ApiCompletion<EvidenceConfigDto>) override { return RequestId::createUuid(); }
    RequestId putEvidenceConfig(const EvidenceConfigUpdate&, QObject*, ApiCompletion<EvidenceConfigDto>) override { return RequestId::createUuid(); }
    RequestId getTime(QObject*, ApiCompletion<TimeStatusDto>) override { return RequestId::createUuid(); }
    RequestId putTime(const TimeUpdate&, QObject*, ApiCompletion<TimeStatusDto>) override { return RequestId::createUuid(); }
    RequestId getFtpConfig(QObject*, ApiCompletion<FtpConfigSnapshotDto>) override { return RequestId::createUuid(); }
    RequestId putFtpConfig(const FtpConfigUpdate&, QObject*, ApiCompletion<FtpConfigSnapshotDto>) override { return RequestId::createUuid(); }
    RequestId rollbackFtpConfig(const QString&, QObject*, ApiCompletion<FtpConfigSnapshotDto>) override { return RequestId::createUuid(); }
    RequestId getFtpControl(QObject*, ApiCompletion<FtpControlDto>) override { return RequestId::createUuid(); }
    RequestId putFtpControl(const FtpControlUpdate&, QObject*, ApiCompletion<FtpControlDto>) override { return RequestId::createUuid(); }
    RequestId createFtpTask(const FtpTaskCreate&, QObject*, ApiCompletion<FtpTaskDetailDto>) override { return RequestId::createUuid(); }
    RequestId listFtpTasks(int, const std::optional<QString>&, QObject*, ApiCompletion<FtpTaskPageDto>) override { return RequestId::createUuid(); }
    RequestId getFtpTask(const QString&, QObject*, ApiCompletion<FtpTaskDetailDto>) override { return RequestId::createUuid(); }
    RequestId retryFtpTask(const QString&, QObject*, ApiCompletion<FtpTaskDetailDto>) override { return RequestId::createUuid(); }

    void cancel(const RequestId& requestId) override
    {
        if (requestId == healthRequestId) {
            ++cancelCount;
        }
    }

    void cancelAll() override { ++cancelCount; }

    int healthRequestCount = 0;
    int cancelCount = 0;
    RequestId healthRequestId;
    std::optional<ApiCompletion<HealthDto>> healthCompletion;
};

DeviceProfile testProfile()
{
    DeviceProfile profile;
    profile.deviceId = QStringLiteral("rv1126b_001");
    profile.endpoint.apiBaseUrl = QUrl(QStringLiteral("http://127.0.0.1:18080/api/v1"));
    return profile;
}

HealthDto matchingHealth()
{
    HealthDto health;
    health.deviceId = QStringLiteral("rv1126b_001");
    return health;
}

QByteArray discoveryResponse(const QString& nonce, const QString& apiHost = QStringLiteral("127.0.0.1"))
{
    const QJsonObject response {
        { QStringLiteral("magic"), QStringLiteral("RV1126B_DISCOVERY") },
        { QStringLiteral("version"), 1 },
        { QStringLiteral("type"), QStringLiteral("discover_response") },
        { QStringLiteral("nonce"), nonce },
        { QStringLiteral("device_id"), QStringLiteral("rv1126b_001") },
        { QStringLiteral("device_model"), QStringLiteral("RV1126B") },
        { QStringLiteral("ipv4"), QStringLiteral("127.0.0.1") },
        { QStringLiteral("api_version"), QStringLiteral("v1") },
        { QStringLiteral("api_url"), QStringLiteral("http://%1:18080/api/v1").arg(apiHost) },
        { QStringLiteral("release_version"), QStringLiteral("test") },
        { QStringLiteral("auth_required"), true },
        { QStringLiteral("capabilities"), QJsonArray { QStringLiteral("health") } },
    };
    return QJsonDocument(response).toJson(QJsonDocument::Compact);
}

} // namespace

class DiscoveryAndSessionTest final : public QObject
{
    Q_OBJECT

private slots:
    void discoveryValidatesAndDeduplicatesResponses();
    void discoveryRejectsUnsafeResponse();
    void discoveryRejectsMismatchedNonce();
    void discoveryCancellationHasNoFollowupSignal();
    void sessionBecomesOnlineOnlyForMatchingHealth();
    void sessionRejectsMismatchedHealthIdentity();
    void sessionStopsRetryingAfterAuthenticationError();
    void sessionRetriesTransientFailureAndRecovers();
    void sessionUpdatesEndpointOnlyForSameDevice();
    void sessionDisconnectsAndCancelsActiveHealthRequest();
};

void DiscoveryAndSessionTest::discoveryValidatesAndDeduplicatesResponses()
{
    QUdpSocket responder;
    QVERIFY(responder.bind(QHostAddress::LocalHost, 0));
    connect(&responder, &QUdpSocket::readyRead, &responder, [&responder] {
        while (responder.hasPendingDatagrams()) {
            const QNetworkDatagram request = responder.receiveDatagram();
            const QString nonce = QJsonDocument::fromJson(request.data()).object().value(QStringLiteral("nonce")).toString();
            const QByteArray response = discoveryResponse(nonce);
            responder.writeDatagram(response, request.senderAddress(), request.senderPort());
            responder.writeDatagram(response, request.senderAddress(), request.senderPort());
        }
    });
    BoardApiCodec codec;
    UdpDeviceDiscoveryService service(
        &codec,
        [] { return QList<QHostAddress> { QHostAddress::LocalHost }; },
        responder.localPort());
    QSignalSpy foundSpy(&service, &DeviceDiscoveryService::deviceFound);
    QSignalSpy finishedSpy(&service, &DeviceDiscoveryService::scanFinished);

    service.startScan(100);

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
    QCOMPARE(foundSpy.count(), 1);
}

void DiscoveryAndSessionTest::discoveryRejectsUnsafeResponse()
{
    QUdpSocket responder;
    QVERIFY(responder.bind(QHostAddress::LocalHost, 0));
    connect(&responder, &QUdpSocket::readyRead, &responder, [&responder] {
        const QNetworkDatagram request = responder.receiveDatagram();
        const QString nonce = QJsonDocument::fromJson(request.data()).object().value(QStringLiteral("nonce")).toString();
        responder.writeDatagram(discoveryResponse(nonce, QStringLiteral("localhost")), request.senderAddress(), request.senderPort());
    });
    BoardApiCodec codec;
    UdpDeviceDiscoveryService service(
        &codec,
        [] { return QList<QHostAddress> { QHostAddress::LocalHost }; },
        responder.localPort());
    QSignalSpy foundSpy(&service, &DeviceDiscoveryService::deviceFound);
    QSignalSpy finishedSpy(&service, &DeviceDiscoveryService::scanFinished);

    service.startScan(80);

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
    QCOMPARE(foundSpy.count(), 0);
}

void DiscoveryAndSessionTest::discoveryRejectsMismatchedNonce()
{
    QUdpSocket responder;
    QVERIFY(responder.bind(QHostAddress::LocalHost, 0));
    connect(&responder, &QUdpSocket::readyRead, &responder, [&responder] {
        const QNetworkDatagram request = responder.receiveDatagram();
        const QString nonce = QJsonDocument::fromJson(request.data()).object().value(QStringLiteral("nonce")).toString();
        responder.writeDatagram(discoveryResponse(nonce + QStringLiteral("-different")), request.senderAddress(), request.senderPort());
    });
    BoardApiCodec codec;
    UdpDeviceDiscoveryService service(
        &codec,
        [] { return QList<QHostAddress> { QHostAddress::LocalHost }; },
        responder.localPort());
    QSignalSpy foundSpy(&service, &DeviceDiscoveryService::deviceFound);
    QSignalSpy finishedSpy(&service, &DeviceDiscoveryService::scanFinished);

    service.startScan(80);

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
    QCOMPARE(foundSpy.count(), 0);
}

void DiscoveryAndSessionTest::discoveryCancellationHasNoFollowupSignal()
{
    BoardApiCodec codec;
    UdpDeviceDiscoveryService service(
        &codec,
        [] { return QList<QHostAddress> { QHostAddress::LocalHost }; },
        18081);
    QSignalSpy foundSpy(&service, &DeviceDiscoveryService::deviceFound);
    QSignalSpy finishedSpy(&service, &DeviceDiscoveryService::scanFinished);

    const RequestId scanId = service.startScan(100);
    service.cancelScan(scanId);
    QTest::qWait(150);

    QCOMPARE(foundSpy.count(), 0);
    QCOMPARE(finishedSpy.count(), 0);
}

void DiscoveryAndSessionTest::sessionBecomesOnlineOnlyForMatchingHealth()
{
    FakeBoardApiClient client;
    BoardDeviceSession session(testProfile(), &client);

    session.connectSession();
    QCOMPARE(session.snapshot().state, DeviceSessionState::Connecting);
    client.respondHealth(ApiResult<HealthDto>::success(matchingHealth()));

    QTRY_COMPARE_WITH_TIMEOUT(session.snapshot().state, DeviceSessionState::Online, 500);
    QCOMPARE(session.snapshot().profile.lastOnlineEpochMs > 0, true);
}

void DiscoveryAndSessionTest::sessionRejectsMismatchedHealthIdentity()
{
    FakeBoardApiClient client;
    BoardDeviceSession session(testProfile(), &client);
    HealthDto wrongHealth = matchingHealth();
    wrongHealth.deviceId = QStringLiteral("different_device");

    session.connectSession();
    client.respondHealth(ApiResult<HealthDto>::success(std::move(wrongHealth)));

    QTRY_COMPARE_WITH_TIMEOUT(session.snapshot().state, DeviceSessionState::Degraded, 500);
    QVERIFY(session.snapshot().lastError.has_value());
    QCOMPARE(session.snapshot().lastError->category, ApiErrorCategory::Protocol);
    QTest::qWait(1100);
    QCOMPARE(client.healthRequestCount, 1);
}

void DiscoveryAndSessionTest::sessionStopsRetryingAfterAuthenticationError()
{
    FakeBoardApiClient client;
    BoardDeviceSession session(testProfile(), &client);

    session.connectSession();
    client.respondHealth(ApiResult<HealthDto>::failure(makeError(ApiErrorCategory::Authentication)));

    QTRY_COMPARE_WITH_TIMEOUT(session.snapshot().state, DeviceSessionState::AuthenticationFailed, 500);
    QTest::qWait(1100);
    QCOMPARE(client.healthRequestCount, 1);
}

void DiscoveryAndSessionTest::sessionRetriesTransientFailureAndRecovers()
{
    FakeBoardApiClient client;
    BoardDeviceSession session(testProfile(), &client);

    session.connectSession();
    client.respondHealth(ApiResult<HealthDto>::failure(makeError(ApiErrorCategory::Network, true)));

    QTRY_COMPARE_WITH_TIMEOUT(session.snapshot().state, DeviceSessionState::Degraded, 500);
    QTRY_COMPARE_WITH_TIMEOUT(client.healthRequestCount, 2, 1500);
    client.respondHealth(ApiResult<HealthDto>::success(matchingHealth()));
    QTRY_COMPARE_WITH_TIMEOUT(session.snapshot().state, DeviceSessionState::Online, 500);
}

void DiscoveryAndSessionTest::sessionUpdatesEndpointOnlyForSameDevice()
{
    FakeBoardApiClient client;
    std::optional<DeviceProfile> appliedProfile;
    BoardDeviceSession session(testProfile(), &client, [&appliedProfile](const DeviceProfile& profile) {
        appliedProfile = profile;
    });
    DeviceProfile updated = testProfile();
    updated.endpoint.apiBaseUrl = QUrl(QStringLiteral("http://127.0.0.2:18080/api/v1"));

    QVERIFY(session.updateProfile(updated));
    QVERIFY(appliedProfile.has_value());
    QCOMPARE(session.profile().endpoint.apiBaseUrl, updated.endpoint.apiBaseUrl);
    QCOMPARE(appliedProfile->deviceId, QStringLiteral("rv1126b_001"));

    updated.deviceId = QStringLiteral("another-device");
    QVERIFY(!session.updateProfile(updated));
    QCOMPARE(session.profile().deviceId, QStringLiteral("rv1126b_001"));
}

void DiscoveryAndSessionTest::sessionDisconnectsAndCancelsActiveHealthRequest()
{
    FakeBoardApiClient client;
    BoardDeviceSession session(testProfile(), &client);

    session.connectSession();
    session.disconnectSession();

    QCOMPARE(client.cancelCount, 1);
    QCOMPARE(session.snapshot().state, DeviceSessionState::Disconnected);
}

QTEST_MAIN(DiscoveryAndSessionTest)

#include "DiscoveryAndSessionTest.moc"
