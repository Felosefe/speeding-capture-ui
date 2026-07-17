#include "QtMultimediaRtspPlayer.h"

#include <QMediaPlayer>
#include <QMediaMetaData>
#include <QVideoFrame>
#include <QVideoSink>
#include <QVideoWidget>

#include <iterator>

namespace {

constexpr int kFrameStallTimeoutMs = 5000;
constexpr int kReconnectDelaysMs[] = {1000, 2000, 4000, 8000, 10000};

} // namespace

QtMultimediaRtspPlayer::QtMultimediaRtspPlayer(QObject* parent)
    : rv1126b::IRtspPlayer(parent)
    , mediaPlayer_(new QMediaPlayer(this))
    , videoWidget_(new QVideoWidget)
{
    videoWidget_->setMinimumSize(640, 360);
    videoWidget_->setAspectRatioMode(Qt::KeepAspectRatio);
    mediaPlayer_->setVideoOutput(videoWidget_);

    openTimer_.setSingleShot(true);
    reconnectTimer_.setSingleShot(true);
    watchdogTimer_.setInterval(1000);

    connect(&openTimer_, &QTimer::timeout, this, &QtMultimediaRtspPlayer::handleOpenTimeout);
    connect(&reconnectTimer_, &QTimer::timeout, this, &QtMultimediaRtspPlayer::reconnectNow);
    connect(&watchdogTimer_, &QTimer::timeout, this, &QtMultimediaRtspPlayer::checkFrameWatchdog);
    connect(videoWidget_->videoSink(), &QVideoSink::videoFrameChanged,
            this, &QtMultimediaRtspPlayer::handleFrame);
    connect(mediaPlayer_, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error error, const QString& message) {
                handleMediaError(static_cast<int>(error), message);
            });
    connect(mediaPlayer_, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus status) {
                handleMediaStatus(static_cast<int>(status));
            });
    connect(mediaPlayer_, &QMediaPlayer::metaDataChanged, this, [this]() {
        const QMediaMetaData metadata = mediaPlayer_->metaData();
        emit streamMetadataReceived(
            metadata.stringValue(QMediaMetaData::VideoCodec),
            metadata.value(QMediaMetaData::Resolution).toSize(),
            metadata.value(QMediaMetaData::VideoFrameRate).toReal());
    });
}

QtMultimediaRtspPlayer::~QtMultimediaRtspPlayer()
{
    stop();
    mediaPlayer_->setVideoOutput(nullptr);
    if (videoWidget_ && !videoWidget_->parent()) {
        delete videoWidget_;
    }
}

void QtMultimediaRtspPlayer::open(const rv1126b::RtspStreamSpec& stream)
{
    stop();
    stream_ = stream;
    reconnectAttempt_ = 0;
    playbackAttempt_ = 0;
    stopRequested_ = false;

    if (!stream_.url.isValid() || stream_.url.scheme().compare(QStringLiteral("rtsp"), Qt::CaseInsensitive) != 0) {
        setState(rv1126b::RtspPlayerState::Error);
        emitPlayerError(QStringLiteral("invalid_rtsp_url"),
                        QStringLiteral("RTSP URL 无效或协议不是 rtsp"),
                        rv1126b::ApiErrorCategory::Validation,
                        false);
        return;
    }

    startPlayback(false);
}

void QtMultimediaRtspPlayer::stop()
{
    stopRequested_ = true;
    reconnectPending_ = false;
    openTimer_.stop();
    reconnectTimer_.stop();
    watchdogTimer_.stop();
    firstFramePending_ = false;
    mediaPlayer_->stop();
    mediaPlayer_->setSource(QUrl());

    if (state_ != rv1126b::RtspPlayerState::Idle) {
        setState(rv1126b::RtspPlayerState::Stopped);
    }
}

rv1126b::RtspPlayerState QtMultimediaRtspPlayer::state() const
{
    return state_;
}

QWidget* QtMultimediaRtspPlayer::outputWidget() const
{
    return videoWidget_;
}

void QtMultimediaRtspPlayer::handleFrame(const QVideoFrame& frame)
{
    if (stopRequested_ || !frame.isValid()) {
        return;
    }

    lastFrameElapsed_.restart();
    emit frameReceived();

    if (!firstFramePending_) {
        return;
    }

    firstFramePending_ = false;
    openTimer_.stop();
    reconnectPending_ = false;
    reconnectAttempt_ = 0;
    setState(rv1126b::RtspPlayerState::Playing);
    emit firstFrameReceived(openElapsed_.elapsed(), frame.size(), frame.surfaceFormat().streamFrameRate());
}

void QtMultimediaRtspPlayer::handleMediaError(int error, const QString& message)
{
    if (stopRequested_) {
        return;
    }

    const auto mediaError = static_cast<QMediaPlayer::Error>(error);
    const bool authenticationError = mediaError == QMediaPlayer::AccessDeniedError;
    const bool formatError = mediaError == QMediaPlayer::FormatError;
    const bool retryable = !authenticationError && !formatError;
    const rv1126b::ApiErrorCategory category = authenticationError
        ? rv1126b::ApiErrorCategory::Authentication
        : (formatError ? rv1126b::ApiErrorCategory::Protocol : rv1126b::ApiErrorCategory::Network);

    emitPlayerError(QStringLiteral("qt_multimedia_%1").arg(error),
                    message.isEmpty() ? QStringLiteral("Qt Multimedia 播放失败") : message,
                    category,
                    retryable);

    if (retryable) {
        scheduleReconnect(QStringLiteral("media_error"));
    } else {
        openTimer_.stop();
        watchdogTimer_.stop();
        setState(rv1126b::RtspPlayerState::Error);
    }
}

void QtMultimediaRtspPlayer::handleMediaStatus(int status)
{
    if (stopRequested_) {
        return;
    }

    const auto mediaStatus = static_cast<QMediaPlayer::MediaStatus>(status);
    if (mediaStatus == QMediaPlayer::InvalidMedia) {
        scheduleReconnect(QStringLiteral("invalid_media"));
    } else if (mediaStatus == QMediaPlayer::EndOfMedia) {
        scheduleReconnect(QStringLiteral("unexpected_end_of_stream"));
    }
}

void QtMultimediaRtspPlayer::handleOpenTimeout()
{
    if (!stopRequested_ && firstFramePending_) {
        emitPlayerError(QStringLiteral("rtsp_open_timeout"),
                        QStringLiteral("在 %1 ms 内未收到首帧").arg(stream_.openTimeoutMs),
                        rv1126b::ApiErrorCategory::Network,
                        true);
        scheduleReconnect(QStringLiteral("open_timeout"));
    }
}

void QtMultimediaRtspPlayer::checkFrameWatchdog()
{
    if (!stopRequested_
        && state_ == rv1126b::RtspPlayerState::Playing
        && lastFrameElapsed_.isValid()
        && lastFrameElapsed_.elapsed() > kFrameStallTimeoutMs) {
        emitPlayerError(QStringLiteral("rtsp_frame_stalled"),
                        QStringLiteral("超过 %1 ms 未收到有效视频帧").arg(kFrameStallTimeoutMs),
                        rv1126b::ApiErrorCategory::Network,
                        true);
        scheduleReconnect(QStringLiteral("frame_stalled"));
    }
}

void QtMultimediaRtspPlayer::reconnectNow()
{
    reconnectPending_ = false;
    if (!stopRequested_) {
        startPlayback(true);
    }
}

void QtMultimediaRtspPlayer::startPlayback(bool reconnecting)
{
    ++playbackAttempt_;
    firstFramePending_ = true;
    reconnectPending_ = false;
    openElapsed_.restart();
    lastFrameElapsed_.invalidate();
    setState(reconnecting ? rv1126b::RtspPlayerState::Reconnecting
                          : rv1126b::RtspPlayerState::Opening);
    emit playbackAttempted(playbackAttempt_, reconnecting);

    mediaPlayer_->stop();
    mediaPlayer_->setSource(stream_.url);
    mediaPlayer_->play();
    openTimer_.start(qMax(1, stream_.openTimeoutMs));
    watchdogTimer_.start();
}

void QtMultimediaRtspPlayer::scheduleReconnect(const QString& reason)
{
    if (stopRequested_ || reconnectPending_) {
        return;
    }

    openTimer_.stop();
    watchdogTimer_.stop();
    firstFramePending_ = false;
    mediaPlayer_->stop();
    mediaPlayer_->setSource(QUrl());

    const int delayIndex = qMin(reconnectAttempt_, static_cast<int>(std::size(kReconnectDelaysMs)) - 1);
    const int delayMs = kReconnectDelaysMs[delayIndex];
    ++reconnectAttempt_;
    reconnectPending_ = true;
    setState(rv1126b::RtspPlayerState::Reconnecting);
    emit reconnectScheduled(reconnectAttempt_, delayMs);
    emitPlayerError(QStringLiteral("rtsp_reconnect_scheduled"),
                    QStringLiteral("%1；%2 ms 后执行第 %3 次重连")
                        .arg(reason)
                        .arg(delayMs)
                        .arg(reconnectAttempt_),
                    rv1126b::ApiErrorCategory::Temporary,
                    true);
    reconnectTimer_.start(delayMs);
}

void QtMultimediaRtspPlayer::setState(rv1126b::RtspPlayerState state)
{
    if (state_ == state) {
        return;
    }
    state_ = state;
    emit stateChanged(state_);
}

void QtMultimediaRtspPlayer::emitPlayerError(const QString& code,
                                             const QString& message,
                                             rv1126b::ApiErrorCategory category,
                                             bool retryable)
{
    rv1126b::ApiError error;
    error.code = code;
    error.message = message;
    error.category = category;
    error.retryable = retryable;
    emit errorOccurred(error);
}
