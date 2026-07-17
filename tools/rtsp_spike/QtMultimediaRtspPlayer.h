#pragma once

#include "../../src/rv1126b/ports/IRtspPlayer.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QSize>
#include <QTimer>

class QMediaPlayer;
class QVideoFrame;
class QVideoWidget;

class QtMultimediaRtspPlayer final : public rv1126b::IRtspPlayer
{
    Q_OBJECT

public:
    explicit QtMultimediaRtspPlayer(QObject* parent = nullptr);
    ~QtMultimediaRtspPlayer() override;

    void open(const rv1126b::RtspStreamSpec& stream) override;
    void stop() override;
    rv1126b::RtspPlayerState state() const override;
    QWidget* outputWidget() const override;

signals:
    void playbackAttempted(int attemptNumber, bool reconnecting);
    void firstFrameReceived(qint64 elapsedMs, QSize frameSize, qreal declaredFrameRate);
    void streamMetadataReceived(QString codec, QSize resolution, qreal frameRate);
    void frameReceived();
    void reconnectScheduled(int attemptNumber, int delayMs);

private slots:
    void handleFrame(const QVideoFrame& frame);
    void handleMediaError(int error, const QString& message);
    void handleMediaStatus(int status);
    void handleOpenTimeout();
    void checkFrameWatchdog();
    void reconnectNow();

private:
    void startPlayback(bool reconnecting);
    void scheduleReconnect(const QString& reason);
    void setState(rv1126b::RtspPlayerState state);
    void emitPlayerError(const QString& code,
                         const QString& message,
                         rv1126b::ApiErrorCategory category,
                         bool retryable);

    QMediaPlayer* mediaPlayer_ = nullptr;
    QPointer<QVideoWidget> videoWidget_;
    QTimer openTimer_;
    QTimer reconnectTimer_;
    QTimer watchdogTimer_;
    QElapsedTimer openElapsed_;
    QElapsedTimer lastFrameElapsed_;
    rv1126b::RtspStreamSpec stream_;
    rv1126b::RtspPlayerState state_ = rv1126b::RtspPlayerState::Idle;
    bool stopRequested_ = false;
    bool firstFramePending_ = false;
    bool reconnectPending_ = false;
    int reconnectAttempt_ = 0;
    int playbackAttempt_ = 0;
};
