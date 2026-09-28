#include "../src/rv1126b/core/Result.h"
#include "../src/rv1126b/core/ValueTypes.h"
#include "../src/rv1126b/domain/Models.h"
#include "../src/rv1126b/ports/IBoardApiClient.h"
#include "../src/rv1126b/ports/IEventRepository.h"
#include "../src/rv1126b/ports/IRtspPlayer.h"
#include "../src/rv1126b/ports/ISecretStore.h"
#include "../src/rv1126b/protocol/ApiCodec.h"
#include "../src/rv1126b/protocol/ApiDtos.h"
#include "../src/rv1126b/services/DeviceDiscoveryService.h"
#include "../src/rv1126b/services/DeviceFleetService.h"
#include "../src/rv1126b/services/DeviceSession.h"
#include "../src/rv1126b/services/EventSyncService.h"
#include "../src/rv1126b/services/EvidenceCache.h"
#include "../src/rv1126b/services/FtpService.h"

#include <QHash>
#include <QtTest>

#include <type_traits>
#include <utility>

class Rv1126bArchitectureTypesTest final : public QObject
{
    Q_OBJECT

private slots:
    void apiResultHasExactlyOneState();
    void eventIdentityUsesTheFullCompositeKey();
    void secretValueIsMoveOnlyAndClearable();
    void architectureLimitsAreFrozen();
    void serviceHeadersRemainAbstractContracts();
};

void Rv1126bArchitectureTypesTest::apiResultHasExactlyOneState()
{
    auto success = rv1126b::ApiResult<int>::success(42);
    QVERIFY(success.isSuccess());
    QCOMPARE(success.value(), 42);

    rv1126b::ApiError error;
    error.httpStatus = 401;
    error.code = QStringLiteral("unauthorized");
    error.category = rv1126b::ApiErrorCategory::Authentication;
    error.retryable = false;

    auto failure = rv1126b::ApiResult<int>::failure(error);
    QVERIFY(!failure.isSuccess());
    QCOMPARE(failure.error().code, QStringLiteral("unauthorized"));
}

void Rv1126bArchitectureTypesTest::eventIdentityUsesTheFullCompositeKey()
{
    const rv1126b::EventIdentity first {QStringLiteral("device-a"), 131, 1994};
    const rv1126b::EventIdentity same {QStringLiteral("device-a"), 131, 1994};
    const rv1126b::EventIdentity otherTrack {QStringLiteral("device-a"), 131, 1995};
    const rv1126b::EventIdentity otherDevice {QStringLiteral("device-b"), 131, 1994};

    QHash<rv1126b::EventIdentity, QString> events;
    events.insert(first, QStringLiteral("stored"));

    QCOMPARE(events.value(same), QStringLiteral("stored"));
    QVERIFY(!events.contains(otherTrack));
    QVERIFY(!events.contains(otherDevice));
}

void Rv1126bArchitectureTypesTest::secretValueIsMoveOnlyAndClearable()
{
    static_assert(!std::is_copy_constructible_v<rv1126b::SecretValue>);
    static_assert(std::is_move_constructible_v<rv1126b::SecretValue>);

    rv1126b::SecretValue source(QByteArrayLiteral("test-token"));
    rv1126b::SecretValue moved(std::move(source));

    QVERIFY(source.isEmpty());
    QVERIFY(!moved.isEmpty());
    moved.clear();
    QVERIFY(moved.isEmpty());
}

void Rv1126bArchitectureTypesTest::architectureLimitsAreFrozen()
{
    QCOMPARE(rv1126b::DeviceFleetService::MaxConcurrentDataSessions, 8);
    QCOMPARE(rv1126b::EventSyncService::DefaultPollIntervalMs, 1000);
    QCOMPARE(rv1126b::EventSyncService::DefaultPageSize, 50);
    QCOMPARE(rv1126b::EventSyncService::RetryBackoffSeconds.back(), 30);
    QCOMPARE(rv1126b::EvidenceCache::MaxConcurrentDownloads, 4);
    QCOMPARE(rv1126b::EvidenceCache::MaxConcurrentDownloadsPerDevice, 1);
}

void Rv1126bArchitectureTypesTest::serviceHeadersRemainAbstractContracts()
{
    static_assert(std::is_abstract_v<rv1126b::IBoardApiClient>);
    static_assert(std::is_abstract_v<rv1126b::IEventRepository>);
    static_assert(std::is_abstract_v<rv1126b::ISecretStore>);
    static_assert(std::is_abstract_v<rv1126b::IRtspPlayer>);
    static_assert(std::is_abstract_v<rv1126b::DeviceDiscoveryService>);
    static_assert(std::is_abstract_v<rv1126b::DeviceSession>);
    static_assert(std::is_abstract_v<rv1126b::DeviceFleetService>);
    static_assert(std::is_abstract_v<rv1126b::EventSyncService>);
    static_assert(std::is_abstract_v<rv1126b::EvidenceCache>);
    static_assert(std::is_abstract_v<rv1126b::FtpService>);
    QVERIFY(true);
}

QTEST_MAIN(Rv1126bArchitectureTypesTest)

#include "Rv1126bArchitectureTypesTest.moc"
