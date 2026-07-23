#include "QtMultimediaRtspPlayer.h"

#include <QThread>
#include <QWidget>

#include <utility>

namespace rv1126b {

QtMultimediaRtspPlayer::QtMultimediaRtspPlayer(IMediaPlaybackBackend* backend,
                                               RtspPlayerTiming timing,
                                               QObject* parent)
    : IRtspPlayer(parent)
    , backend_(backend)
    , timing_(std::move(timing))
{
    Q_ASSERT_X(backend_, "QtMultimediaRtspPlayer", "backend must not be null");
    Q_ASSERT_X(!backend_->parent(), "QtMultimediaRtspPlayer", "backend must not already have an owner");
    backend_->setParent(this);

    timing_.frameStallTimeoutMs = qMax(1, timing_.frameStallTimeoutMs);
    timing_.watchdogIntervalMs = qMax(1, timing_.watchdogIntervalMs);
    if (timing_.reconnectDelaysMs.isEmpty()) {
        timing_.reconnectDelaysMs = {1000, 2000, 4000, 8000, 10000};
    }
    for (int& delayMs : timing_.reconnectDelaysMs) {
        delayMs = qMax(1, delayMs);
    }

    openTimer_.setSingleShot(true);
    reconnectTimer_.setSingleShot(true);
    watchdogTimer_.setInterval(timing_.watchdogIntervalMs);

    connect(&openTimer_, &QTimer::timeout, this, &QtMultimediaRtspPlayer::handleOpenTimeout);
    connect(&reconnectTimer_, &QTimer::timeout, this, &QtMultimediaRtspPlayer::reconnectNow);
    connect(&watchdogTimer_, &QTimer::timeout, this, &QtMultimediaRtspPlayer::checkFrameWatchdog);
    connect(backend_, &IMediaPlaybackBackend::frameReady,
            this, &QtMultimediaRtspPlayer::handleFrame);
    connect(backend_, &IMediaPlaybackBackend::metadataReady,
            this, &QtMultimediaRtspPlayer::handleMetadata);
    connect(backend_, &IMediaPlaybackBackend::playbackFailed,
            this, &QtMultimediaRtspPlayer::handleBackendFailure);
    connect(backend_, &IMediaPlaybackBackend::streamEnded,
            this, &QtMultimediaRtspPlayer::handleStreamEnded);
}

QtMultimediaRtspPlayer::~QtMultimediaRtspPlayer()
{
    assertOwnerThread("destructor");
    stopInternal(false);
}

void QtMultimediaRtspPlayer::open(const RtspStreamSpec& stream)
{
    assertOwnerThread("open");
    stopInternal(state_ != RtspPlayerState::Idle);
    stream_ = stream;
    reconnectAttempt_ = 0;
    playbackAttempt_ = 0;

    if (!isValidStream(stream_)) {
        setState(RtspPlayerState::Error);
        emitPlayerError(QStringLiteral("invalid_rtsp_spec"),
                        QStringLiteral("RTSP 播放参数无效"),
                        ApiErrorCategory::Validation,
                        false);
        return;
    }

    stopRequested_ = false;
    startPlayback(false);
}

void QtMultimediaRtspPlayer::stop()
{
    assertOwnerThread("stop");
    stopInternal(true);
}

RtspPlayerState QtMultimediaRtspPlayer::state() const
{
    assertOwnerThread("state");
    return state_;
}

QWidget* QtMultimediaRtspPlayer::outputWidget() const
{
    assertOwnerThread("outputWidget");
    return backend_->outputWidget();
}

void QtMultimediaRtspPlayer::handleFrame(quint64 attemptToken,
                                         QSize frameSize,
                                         qreal declaredFrameRate)
{
    if (stopRequested_ || attemptToken == 0 || attemptToken != activeAttemptToken_) {
        return;
    }

    lastFrameElapsed_.restart();
    emit frameReceived();
    if (frameSize.isValid()) {
        emit videoFrameReceived(frameSize);
    }

    if (!firstFramePending_) {
        return;
    }

    firstFramePending_ = false;
    openTimer_.stop();
    reconnectPending_ = false;
    reconnectAttempt_ = 0;
    setState(RtspPlayerState::Playing);
    emit firstFrameReceived(openElapsed_.elapsed(), frameSize, declaredFrameRate);
}

void QtMultimediaRtspPlayer::handleMetadata(quint64 attemptToken,
                                            QString codec,
                                            QSize resolution,
                                            qreal frameRate)
{
    if (!stopRequested_ && attemptToken != 0 && attemptToken == activeAttemptToken_) {
        emit streamMetadataReceived(std::move(codec), resolution, frameRate);
    }
}

void QtMultimediaRtspPlayer::handleBackendFailure(quint64 attemptToken,
                                                  MediaPlaybackFailure failure)
{
    if (stopRequested_ || attemptToken == 0 || attemptToken != activeAttemptToken_) {
        return;
    }

    if (failure == MediaPlaybackFailure::Authentication) {
        openTimer_.stop();
        watchdogTimer_.stop();
        firstFramePending_ = false;
        activeAttemptToken_ = 0;
        backend_->stop();
        setState(RtspPlayerState::Error);
        emitPlayerError(QStringLiteral("rtsp_authentication_failed"),
                        QStringLiteral("RTSP 鉴权失败"),
                        ApiErrorCategory::Authentication,
                        false);
        return;
    }

    if (failure == MediaPlaybackFailure::Format) {
        openTimer_.stop();
        watchdogTimer_.stop();
        firstFramePending_ = false;
        activeAttemptToken_ = 0;
        backend_->stop();
        setState(RtspPlayerState::Error);
        emitPlayerError(QStringLiteral("rtsp_format_unsupported"),
                        QStringLiteral("RTSP 视频格式不受支持"),
                        ApiErrorCategory::Protocol,
                        false);
        return;
    }

    emitPlayerError(QStringLiteral("rtsp_backend_error"),
                    QStringLiteral("RTSP 媒体后端播放失败"),
                    failure == MediaPlaybackFailure::Network
                        ? ApiErrorCategory::Network : ApiErrorCategory::Temporary,
                    true);
    scheduleReconnect();
}

void QtMultimediaRtspPlayer::handleStreamEnded(quint64 attemptToken)
{
    if (stopRequested_ || attemptToken == 0 || attemptToken != activeAttemptToken_) {
        return;
    }
    emitPlayerError(QStringLiteral("rtsp_backend_error"),
                    QStringLiteral("RTSP 码流意外结束"),
                    ApiErrorCategory::Network,
                    true);
    scheduleReconnect();
}

void QtMultimediaRtspPlayer::handleOpenTimeout()
{
    if (!stopRequested_ && firstFramePending_ && activeAttemptToken_ != 0) {
        emitPlayerError(QStringLiteral("rtsp_open_timeout"),
                        QStringLiteral("RTSP 首帧等待超时"),
                        ApiErrorCategory::Network,
                        true);
        scheduleReconnect();
    }
}

void QtMultimediaRtspPlayer::checkFrameWatchdog()
{
    if (!stopRequested_
        && state_ == RtspPlayerState::Playing
        && activeAttemptToken_ != 0
        && lastFrameElapsed_.isValid()
        && lastFrameElapsed_.elapsed() > timing_.frameStallTimeoutMs) {
        emitPlayerError(QStringLiteral("rtsp_frame_stalled"),
                        QStringLiteral("RTSP 视频帧已停止更新"),
                        ApiErrorCategory::Network,
                        true);
        scheduleReconnect();
    }
}

void QtMultimediaRtspPlayer::reconnectNow()
{
    if (!reconnectPending_ || stopRequested_) {
        return;
    }
    reconnectPending_ = false;
    startPlayback(true);
}

bool QtMultimediaRtspPlayer::isValidStream(const RtspStreamSpec& stream) const
{
    return !stream.deviceId.trimmed().isEmpty()
        && stream.url.isValid()
        && stream.url.scheme().compare(QStringLiteral("rtsp"), Qt::CaseInsensitive) == 0
        && !stream.url.host().isEmpty()
        && stream.openTimeoutMs > 0;
}

void QtMultimediaRtspPlayer::assertOwnerThread(const char* operation) const
{
    Q_ASSERT_X(QThread::currentThread() == thread(), operation,
               "IRtspPlayer must be used from its owning GUI thread");
}

void QtMultimediaRtspPlayer::startPlayback(bool reconnecting)
{
    ++playbackAttempt_;
    activeAttemptToken_ = ++nextAttemptToken_;
    firstFramePending_ = true;
    reconnectPending_ = false;
    openElapsed_.restart();
    lastFrameElapsed_.invalidate();
    setState(reconnecting ? RtspPlayerState::Reconnecting : RtspPlayerState::Opening);
    emit playbackAttempted(playbackAttempt_, reconnecting);

    backend_->play(stream_.url, activeAttemptToken_);
    openTimer_.start(stream_.openTimeoutMs);
    watchdogTimer_.start();
}

void QtMultimediaRtspPlayer::scheduleReconnect()
{
    if (stopRequested_ || reconnectPending_ || activeAttemptToken_ == 0) {
        return;
    }

    openTimer_.stop();
    watchdogTimer_.stop();
    firstFramePending_ = false;
    activeAttemptToken_ = 0;
    backend_->stop();

    const int delayIndex = qMin(reconnectAttempt_, timing_.reconnectDelaysMs.size() - 1);
    const int delayMs = timing_.reconnectDelaysMs.at(delayIndex);
    ++reconnectAttempt_;
    reconnectPending_ = true;
    setState(RtspPlayerState::Reconnecting);
    emit reconnectScheduled(reconnectAttempt_, delayMs);
    reconnectTimer_.start(delayMs);
}

void QtMultimediaRtspPlayer::stopInternal(bool updateState)
{
    stopRequested_ = true;
    activeAttemptToken_ = 0;
    reconnectPending_ = false;
    firstFramePending_ = false;
    openTimer_.stop();
    reconnectTimer_.stop();
    watchdogTimer_.stop();
    openElapsed_.invalidate();
    lastFrameElapsed_.invalidate();
    if (backend_) {
        backend_->stop();
    }

    if (updateState) {
        setState(RtspPlayerState::Stopped);
    }
}

void QtMultimediaRtspPlayer::setState(RtspPlayerState state)
{
    if (state_ == state) {
        return;
    }
    state_ = state;
    emit stateChanged(state_);
}

void QtMultimediaRtspPlayer::emitPlayerError(const QString& code,
                                             const QString& message,
                                             ApiErrorCategory category,
                                             bool retryable)
{
    ApiError error;
    error.code = code;
    error.message = message;
    error.category = category;
    error.retryable = retryable;
    emit errorOccurred(error);
}

} // namespace rv1126b
