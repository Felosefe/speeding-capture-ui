#pragma once

#include "../../ports/IRtspPlayer.h"
#include "IMediaPlaybackBackend.h"

#include <QElapsedTimer>
#include <QSize>
#include <QTimer>
#include <QVector>

namespace rv1126b {

struct RtspPlayerTiming {
    int frameStallTimeoutMs = 5000;
    int watchdogIntervalMs = 1000;
    QVector<int> reconnectDelaysMs {1000, 2000, 4000, 8000, 10000};
};

class QtMultimediaRtspPlayer final : public IRtspPlayer
{
    Q_OBJECT

public:
    explicit QtMultimediaRtspPlayer(QObject* parent);
    QtMultimediaRtspPlayer(IMediaPlaybackBackend* backend,
                           RtspPlayerTiming timing,
                           QObject* parent = nullptr);
    ~QtMultimediaRtspPlayer() override;

    void open(const RtspStreamSpec& stream) override;
    void stop() override;
    RtspPlayerState state() const override;
    QWidget* outputWidget() const override;

signals:
    void playbackAttempted(int attemptNumber, bool reconnecting);
    void firstFrameReceived(qint64 elapsedMs, QSize frameSize, qreal declaredFrameRate);
    void streamMetadataReceived(QString codec, QSize resolution, qreal frameRate);
    void frameReceived();
    void reconnectScheduled(int attemptNumber, int delayMs);

private slots:
    void handleFrame(quint64 attemptToken, QSize frameSize, qreal declaredFrameRate);
    void handleMetadata(quint64 attemptToken, QString codec, QSize resolution, qreal frameRate);
    void handleBackendFailure(quint64 attemptToken, rv1126b::MediaPlaybackFailure failure);
    void handleStreamEnded(quint64 attemptToken);
    void handleOpenTimeout();
    void checkFrameWatchdog();
    void reconnectNow();

private:
    bool isValidStream(const RtspStreamSpec& stream) const;
    void assertOwnerThread(const char* operation) const;
    void startPlayback(bool reconnecting);
    void scheduleReconnect();
    void stopInternal(bool updateState);
    void setState(RtspPlayerState state);
    void emitPlayerError(const QString& code,
                         const QString& message,
                         ApiErrorCategory category,
                         bool retryable);

    IMediaPlaybackBackend* backend_ = nullptr;
    QTimer openTimer_;
    QTimer reconnectTimer_;
    QTimer watchdogTimer_;
    QElapsedTimer openElapsed_;
    QElapsedTimer lastFrameElapsed_;
    RtspStreamSpec stream_;
    RtspPlayerTiming timing_;
    RtspPlayerState state_ = RtspPlayerState::Idle;
    quint64 nextAttemptToken_ = 0;
    quint64 activeAttemptToken_ = 0;
    bool stopRequested_ = false;
    bool firstFramePending_ = false;
    bool reconnectPending_ = false;
    int reconnectAttempt_ = 0;
    int playbackAttempt_ = 0;
};

} // namespace rv1126b

Q_DECLARE_METATYPE(rv1126b::RtspPlayerTiming)
