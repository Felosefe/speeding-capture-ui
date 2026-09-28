#include "../src/rv1126b/infrastructure/video/QtMultimediaRtspPlayer.h"

#include <QPointer>
#include <QSignalSpy>
#include <QWidget>
#include <QtTest>

#include <utility>

using namespace rv1126b;

class FakeMediaPlaybackBackend final : public IMediaPlaybackBackend
{
public:
    explicit FakeMediaPlaybackBackend(QObject* parent = nullptr)
        : IMediaPlaybackBackend(parent)
        , widget_(new QWidget)
    {
    }

    ~FakeMediaPlaybackBackend() override
    {
        if (widget_ && !widget_->parent()) {
            delete widget_;
        }
    }

    void play(const QUrl& url, quint64 attemptToken) override
    {
        ++playCount;
        currentUrl = url;
        currentToken = attemptToken;
        playedTokens.append(attemptToken);
    }

    void stop() override
    {
        ++stopCount;
    }

    QWidget* outputWidget() const override
    {
        return widget_;
    }

    void sendFrame(quint64 token, QSize size = QSize(1920, 1080), qreal frameRate = 25.0)
    {
        emit frameReady(token, size, frameRate);
    }

    void sendFailure(quint64 token, MediaPlaybackFailure failure)
    {
        emit playbackFailed(token, failure);
    }

    int playCount = 0;
    int stopCount = 0;
    quint64 currentToken = 0;
    QUrl currentUrl;
    QVector<quint64> playedTokens;

private:
    QPointer<QWidget> widget_;
};

class RtspPlayerTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void defaultTimingIsFrozen();
    void validOpenReachesPlayingOnFirstFrame();
    void invalidStreamIsRejected_data();
    void invalidStreamIsRejected();
    void openTimeoutSchedulesReconnect();
    void stalledFramesScheduleReconnect();
    void terminalBackendFailuresDoNotReconnect_data();
    void terminalBackendFailuresDoNotReconnect();
    void reconnectBackoffCapsAndResetsAfterFrame();
    void backendErrorsDoNotExposeRtspCredentials();
    void switchingStreamsIgnoresLateAttemptSignals();
    void stopCancelsPlaybackAndIsIdempotent();
    void stopDuringReconnectCancelsPendingAttempt();
    void destructionReleasesBackendAndUnparentedWidget();

private:
    static RtspStreamSpec stream(QString deviceId = QStringLiteral("device-a"),
                                 QString url = QStringLiteral("rtsp://192.0.2.10/live/1"),
                                 int timeoutMs = 1000);
    static RtspPlayerTiming timing(QVector<int> reconnectDelays = {20, 40, 80, 160, 200});
    static ApiError lastError(const QSignalSpy& spy);
};

void RtspPlayerTest::initTestCase()
{
    qRegisterMetaType<ApiError>();
    qRegisterMetaType<RtspPlayerState>();
    qRegisterMetaType<MediaPlaybackFailure>();
}

void RtspPlayerTest::defaultTimingIsFrozen()
{
    const RtspStreamSpec defaultStream;
    const RtspPlayerTiming defaultTiming;

    QCOMPARE(defaultStream.openTimeoutMs, 15000);
    QCOMPARE(defaultTiming.frameStallTimeoutMs, 5000);
    QCOMPARE(defaultTiming.watchdogIntervalMs, 1000);
    QCOMPARE(defaultTiming.reconnectDelaysMs, QVector<int>({1000, 2000, 4000, 8000, 10000}));
}

void RtspPlayerTest::validOpenReachesPlayingOnFirstFrame()
{
    auto* backend = new FakeMediaPlaybackBackend;
    QtMultimediaRtspPlayer player(backend, timing());
    QSignalSpy stateSpy(&player, &IRtspPlayer::stateChanged);
    QSignalSpy frameSpy(&player, &QtMultimediaRtspPlayer::firstFrameReceived);

    player.open(stream());

    QCOMPARE(player.state(), RtspPlayerState::Opening);
    QCOMPARE(player.thread(), QCoreApplication::instance()->thread());
    QCOMPARE(backend->thread(), player.thread());
    QCOMPARE(backend->playCount, 1);
    QCOMPARE(backend->currentUrl, QUrl(QStringLiteral("rtsp://192.0.2.10/live/1")));
    QCOMPARE(player.outputWidget(), backend->outputWidget());

    backend->sendFrame(backend->currentToken, QSize(2688, 1520), 30.0);

    QCOMPARE(player.state(), RtspPlayerState::Playing);
    QCOMPARE(frameSpy.size(), 1);
    QCOMPARE(frameSpy.at(0).at(1).toSize(), QSize(2688, 1520));
    QCOMPARE(stateSpy.size(), 2);
    QCOMPARE(stateSpy.at(0).at(0).value<RtspPlayerState>(), RtspPlayerState::Opening);
    QCOMPARE(stateSpy.at(1).at(0).value<RtspPlayerState>(), RtspPlayerState::Playing);
}

void RtspPlayerTest::invalidStreamIsRejected_data()
{
    QTest::addColumn<QString>("deviceId");
    QTest::addColumn<QUrl>("url");
    QTest::addColumn<int>("timeoutMs");

    QTest::newRow("missing-device") << QString() << QUrl(QStringLiteral("rtsp://192.0.2.10/live/0")) << 1000;
    QTest::newRow("wrong-scheme") << QStringLiteral("device-a") << QUrl(QStringLiteral("http://192.0.2.10/live/0")) << 1000;
    QTest::newRow("missing-host") << QStringLiteral("device-a") << QUrl(QStringLiteral("rtsp:/live/0")) << 1000;
    QTest::newRow("non-positive-timeout") << QStringLiteral("device-a") << QUrl(QStringLiteral("rtsp://192.0.2.10/live/0")) << 0;
}

void RtspPlayerTest::invalidStreamIsRejected()
{
    QFETCH(QString, deviceId);
    QFETCH(QUrl, url);
    QFETCH(int, timeoutMs);

    auto* backend = new FakeMediaPlaybackBackend;
    QtMultimediaRtspPlayer player(backend, timing());
    QSignalSpy errorSpy(&player, &IRtspPlayer::errorOccurred);

    RtspStreamSpec spec;
    spec.deviceId = deviceId;
    spec.url = url;
    spec.openTimeoutMs = timeoutMs;
    player.open(spec);

    QCOMPARE(player.state(), RtspPlayerState::Error);
    QCOMPARE(backend->playCount, 0);
    QCOMPARE(errorSpy.size(), 1);
    const ApiError error = lastError(errorSpy);
    QCOMPARE(error.code, QStringLiteral("invalid_rtsp_spec"));
    QCOMPARE(error.category, ApiErrorCategory::Validation);
    QVERIFY(!error.retryable);
}

void RtspPlayerTest::openTimeoutSchedulesReconnect()
{
    auto* backend = new FakeMediaPlaybackBackend;
    QtMultimediaRtspPlayer player(backend, timing({1000}));
    QSignalSpy errorSpy(&player, &IRtspPlayer::errorOccurred);
    QSignalSpy reconnectSpy(&player, &QtMultimediaRtspPlayer::reconnectScheduled);

    player.open(stream(QStringLiteral("device-a"), QStringLiteral("rtsp://192.0.2.10/live/0"), 20));

    QTRY_COMPARE(player.state(), RtspPlayerState::Reconnecting);
    QCOMPARE(errorSpy.size(), 1);
    QCOMPARE(lastError(errorSpy).code, QStringLiteral("rtsp_open_timeout"));
    QCOMPARE(reconnectSpy.size(), 1);
    QCOMPARE(reconnectSpy.at(0).at(1).toInt(), 1000);
}

void RtspPlayerTest::stalledFramesScheduleReconnect()
{
    auto* backend = new FakeMediaPlaybackBackend;
    RtspPlayerTiming fastTiming = timing({1000});
    fastTiming.frameStallTimeoutMs = 20;
    fastTiming.watchdogIntervalMs = 5;
    QtMultimediaRtspPlayer player(backend, fastTiming);
    QSignalSpy errorSpy(&player, &IRtspPlayer::errorOccurred);

    player.open(stream());
    backend->sendFrame(backend->currentToken);
    QCOMPARE(player.state(), RtspPlayerState::Playing);

    QTRY_COMPARE(player.state(), RtspPlayerState::Reconnecting);
    QCOMPARE(errorSpy.size(), 1);
    QCOMPARE(lastError(errorSpy).code, QStringLiteral("rtsp_frame_stalled"));
}

void RtspPlayerTest::terminalBackendFailuresDoNotReconnect_data()
{
    QTest::addColumn<MediaPlaybackFailure>("failure");
    QTest::addColumn<QString>("code");
    QTest::addColumn<ApiErrorCategory>("category");

    QTest::newRow("authentication")
        << MediaPlaybackFailure::Authentication
        << QStringLiteral("rtsp_authentication_failed")
        << ApiErrorCategory::Authentication;
    QTest::newRow("format")
        << MediaPlaybackFailure::Format
        << QStringLiteral("rtsp_format_unsupported")
        << ApiErrorCategory::Protocol;
}

void RtspPlayerTest::terminalBackendFailuresDoNotReconnect()
{
    QFETCH(MediaPlaybackFailure, failure);
    QFETCH(QString, code);
    QFETCH(ApiErrorCategory, category);

    auto* backend = new FakeMediaPlaybackBackend;
    QtMultimediaRtspPlayer player(backend, timing({10}));
    QSignalSpy errorSpy(&player, &IRtspPlayer::errorOccurred);
    QSignalSpy reconnectSpy(&player, &QtMultimediaRtspPlayer::reconnectScheduled);

    player.open(stream());
    backend->sendFailure(backend->currentToken, failure);

    QCOMPARE(player.state(), RtspPlayerState::Error);
    QCOMPARE(errorSpy.size(), 1);
    QCOMPARE(lastError(errorSpy).code, code);
    QCOMPARE(lastError(errorSpy).category, category);
    QVERIFY(!lastError(errorSpy).retryable);
    QCOMPARE(reconnectSpy.size(), 0);
    QTest::qWait(30);
    QCOMPARE(backend->playCount, 1);
}

void RtspPlayerTest::reconnectBackoffCapsAndResetsAfterFrame()
{
    const QVector<int> delays {5, 10, 15, 20, 25};
    auto* backend = new FakeMediaPlaybackBackend;
    QtMultimediaRtspPlayer player(backend, timing(delays));
    QSignalSpy reconnectSpy(&player, &QtMultimediaRtspPlayer::reconnectScheduled);

    player.open(stream());
    for (int failureIndex = 0; failureIndex < 6; ++failureIndex) {
        backend->sendFailure(backend->currentToken, MediaPlaybackFailure::Network);
        QCOMPARE(player.state(), RtspPlayerState::Reconnecting);
        QCOMPARE(reconnectSpy.size(), failureIndex + 1);
        QCOMPARE(reconnectSpy.last().at(1).toInt(), delays.at(qMin(failureIndex, delays.size() - 1)));
        QTRY_COMPARE(backend->playCount, failureIndex + 2);
    }

    backend->sendFrame(backend->currentToken);
    QCOMPARE(player.state(), RtspPlayerState::Playing);
    backend->sendFailure(backend->currentToken, MediaPlaybackFailure::Network);
    QCOMPARE(reconnectSpy.last().at(1).toInt(), delays.first());
}

void RtspPlayerTest::backendErrorsDoNotExposeRtspCredentials()
{
    auto* backend = new FakeMediaPlaybackBackend;
    QtMultimediaRtspPlayer player(backend, timing({1000}));
    QSignalSpy errorSpy(&player, &IRtspPlayer::errorOccurred);

    player.open(stream(QStringLiteral("device-a"),
                       QStringLiteral("rtsp://preview:super-secret@192.0.2.10/live/0?token=query-secret")));
    backend->sendFailure(backend->currentToken, MediaPlaybackFailure::Backend);

    QCOMPARE(errorSpy.size(), 1);
    const ApiError error = lastError(errorSpy);
    QCOMPARE(error.code, QStringLiteral("rtsp_backend_error"));
    QCOMPARE(error.category, ApiErrorCategory::Temporary);
    QVERIFY(error.retryable);
    QVERIFY(!error.message.contains(QStringLiteral("preview")));
    QVERIFY(!error.message.contains(QStringLiteral("super-secret")));
    QVERIFY(!error.message.contains(QStringLiteral("query-secret")));
}

void RtspPlayerTest::switchingStreamsIgnoresLateAttemptSignals()
{
    auto* backend = new FakeMediaPlaybackBackend;
    QtMultimediaRtspPlayer player(backend, timing({1000}));
    QSignalSpy errorSpy(&player, &IRtspPlayer::errorOccurred);

    RtspStreamSpec mainStream = stream(QStringLiteral("device-a"), QStringLiteral("rtsp://192.0.2.10/live/0"));
    mainStream.role = RtspStreamRole::Main;
    player.open(mainStream);
    const quint64 oldToken = backend->currentToken;
    backend->sendFrame(oldToken);
    QCOMPARE(player.state(), RtspPlayerState::Playing);

    RtspStreamSpec subStream = stream(QStringLiteral("device-b"), QStringLiteral("rtsp://192.0.2.20/live/1"));
    subStream.role = RtspStreamRole::Sub;
    player.open(subStream);
    const quint64 newToken = backend->currentToken;
    QVERIFY(newToken != oldToken);
    QCOMPARE(player.state(), RtspPlayerState::Opening);

    backend->sendFrame(oldToken);
    backend->sendFailure(oldToken, MediaPlaybackFailure::Authentication);
    QCOMPARE(player.state(), RtspPlayerState::Opening);
    QCOMPARE(errorSpy.size(), 0);

    backend->sendFrame(newToken, QSize(1280, 720), 20.0);
    QCOMPARE(player.state(), RtspPlayerState::Playing);
}

void RtspPlayerTest::stopCancelsPlaybackAndIsIdempotent()
{
    auto* backend = new FakeMediaPlaybackBackend;
    QtMultimediaRtspPlayer player(backend, timing({10}));
    QSignalSpy stateSpy(&player, &IRtspPlayer::stateChanged);
    QSignalSpy errorSpy(&player, &IRtspPlayer::errorOccurred);

    player.open(stream());
    const quint64 token = backend->currentToken;
    player.stop();
    QCOMPARE(player.state(), RtspPlayerState::Stopped);
    const int stateSignalCount = stateSpy.size();

    player.stop();
    QCOMPARE(stateSpy.size(), stateSignalCount);
    backend->sendFrame(token);
    backend->sendFailure(token, MediaPlaybackFailure::Network);
    QTest::qWait(30);
    QCOMPARE(player.state(), RtspPlayerState::Stopped);
    QCOMPARE(errorSpy.size(), 0);
    QCOMPARE(backend->playCount, 1);
}

void RtspPlayerTest::stopDuringReconnectCancelsPendingAttempt()
{
    auto* backend = new FakeMediaPlaybackBackend;
    QtMultimediaRtspPlayer player(backend, timing({30}));

    player.open(stream());
    backend->sendFailure(backend->currentToken, MediaPlaybackFailure::Network);
    QCOMPARE(player.state(), RtspPlayerState::Reconnecting);
    player.stop();
    QCOMPARE(player.state(), RtspPlayerState::Stopped);

    QTest::qWait(60);
    QCOMPARE(backend->playCount, 1);
    QCOMPARE(player.state(), RtspPlayerState::Stopped);
}

void RtspPlayerTest::destructionReleasesBackendAndUnparentedWidget()
{
    auto* backend = new FakeMediaPlaybackBackend;
    QPointer<IMediaPlaybackBackend> backendGuard(backend);
    QPointer<QWidget> widgetGuard(backend->outputWidget());
    auto* player = new QtMultimediaRtspPlayer(backend, timing());
    player->open(stream());

    delete player;

    QVERIFY(backendGuard.isNull());
    QVERIFY(widgetGuard.isNull());
}

RtspStreamSpec RtspPlayerTest::stream(QString deviceId, QString url, int timeoutMs)
{
    RtspStreamSpec spec;
    spec.deviceId = std::move(deviceId);
    spec.url = QUrl(std::move(url));
    spec.role = RtspStreamRole::Sub;
    spec.openTimeoutMs = timeoutMs;
    return spec;
}

RtspPlayerTiming RtspPlayerTest::timing(QVector<int> reconnectDelays)
{
    RtspPlayerTiming value;
    value.reconnectDelaysMs = std::move(reconnectDelays);
    return value;
}

ApiError RtspPlayerTest::lastError(const QSignalSpy& spy)
{
    return qvariant_cast<ApiError>(spy.last().at(0));
}

QTEST_MAIN(RtspPlayerTest)

#include "RtspPlayerTest.moc"
