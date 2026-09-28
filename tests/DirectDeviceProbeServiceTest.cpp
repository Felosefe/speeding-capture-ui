#include "../src/rv1126b/network/DirectDeviceProbeService.h"
#include "../src/rv1126b/protocol/BoardApiCodec.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

#include <optional>

using namespace rv1126b;

namespace {

QByteArray healthPayload()
{
    return QByteArrayLiteral(R"({
        "api_version":"v1","device_id":"rv1126b_manual","device_model":"RV1126B",
        "release_version":"test","server_time":{"epoch_ms":1784002847389,
        "source_epoch_ms":1784002847389,"offset_applied_ms":0,"quality":"native_utc"},
        "pipeline_health_available":true,"pipeline":{},
        "application_api":{"alive":true,"http_port":18080,"discovery_port":18081,
        "auth_required":true}})");
}

class ProbeHttpServer final : public QTcpServer
{
public:
    struct Response { int status = 200; QByteArray body = healthPayload(); };

    ProbeHttpServer()
    {
        connect(this, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* socket = nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
                    buffers_[socket] += socket->readAll();
                    if (!buffers_[socket].contains("\r\n\r\n")) return;
                    requests.append(buffers_.take(socket));
                    const Response response = responses.isEmpty() ? Response {} : responses.takeFirst();
                    const QByteArray statusText = response.status == 200 ? QByteArrayLiteral("OK")
                        : response.status == 401 ? QByteArrayLiteral("Unauthorized")
                        : QByteArrayLiteral("Found");
                    QByteArray header = QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(response.status)
                        + ' ' + statusText + QByteArrayLiteral("\r\nContent-Type: application/json\r\nContent-Length: ")
                        + QByteArray::number(response.body.size())
                        + QByteArrayLiteral("\r\nConnection: close\r\n\r\n");
                    socket->write(header + response.body);
                    socket->disconnectFromHost();
                });
            }
        });
    }

    bool start() { return listen(QHostAddress::LocalHost, 0); }
    QList<Response> responses;
    QList<QByteArray> requests;
    QHash<QTcpSocket*, QByteArray> buffers_;
};

} // namespace

class DirectDeviceProbeServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void requiresStrictIpv4();
    void supportsTokenRetryAndRejectsIdentityMismatch();
};

void DirectDeviceProbeServiceTest::requiresStrictIpv4()
{
    BoardApiCodec codec;
    DirectDeviceProbeService service(&codec);
    std::optional<ApiResult<DirectProbeResult>> result;
    service.probe(QStringLiteral("localhost"), 18080, SecretValue {}, {}, this,
                  [&](ApiResult<DirectProbeResult> value) { result = std::move(value); });
    QTRY_VERIFY(result.has_value());
    QVERIFY(!result->isSuccess());
    QCOMPARE(result->error().code, QStringLiteral("invalid_manual_endpoint"));
}

void DirectDeviceProbeServiceTest::supportsTokenRetryAndRejectsIdentityMismatch()
{
    ProbeHttpServer server;
    QVERIFY(server.start());
    server.responses.append({401, QByteArrayLiteral(
        R"({"error":{"code":"invalid_token","message":"secret must not surface"}})")});
    server.responses.append({200, healthPayload()});
    server.responses.append({200, healthPayload()});

    BoardApiCodec codec;
    DirectDeviceProbeService service(&codec);
    std::optional<ApiResult<DirectProbeResult>> first;
    service.probe(QStringLiteral("127.0.0.1"), server.serverPort(), SecretValue {}, {}, this,
                  [&](ApiResult<DirectProbeResult> value) { first = std::move(value); });
    QTRY_VERIFY(first.has_value());
    QVERIFY(!first->isSuccess());
    QCOMPARE(first->error().category, ApiErrorCategory::Authentication);
    QVERIFY(!first->error().message.contains(QStringLiteral("secret must not surface")));

    std::optional<ApiResult<DirectProbeResult>> second;
    service.probe(QStringLiteral("127.0.0.1"), server.serverPort(),
                  SecretValue(QByteArrayLiteral("manual-token")), {}, this,
                  [&](ApiResult<DirectProbeResult> value) { second = std::move(value); });
    QTRY_VERIFY(second.has_value());
    QVERIFY(second->isSuccess());
    QCOMPARE(second->value().device.deviceId, QStringLiteral("rv1126b_manual"));
    QCOMPARE(second->value().device.apiUrl.path(), QStringLiteral("/api/v1"));
    QVERIFY(server.requests.at(1).startsWith("GET /api/v1/health HTTP/1.1\r\n"));
    QVERIFY(server.requests.at(1).toLower().contains("authorization: bearer manual-token\r\n"));

    std::optional<ApiResult<DirectProbeResult>> mismatch;
    service.probe(QStringLiteral("127.0.0.1"), server.serverPort(), SecretValue {},
                  QStringLiteral("different-device"), this,
                  [&](ApiResult<DirectProbeResult> value) { mismatch = std::move(value); });
    QTRY_VERIFY(mismatch.has_value());
    QVERIFY(!mismatch->isSuccess());
    QCOMPARE(mismatch->error().code, QStringLiteral("manual_probe_identity_mismatch"));
}

QTEST_MAIN(DirectDeviceProbeServiceTest)
#include "DirectDeviceProbeServiceTest.moc"
