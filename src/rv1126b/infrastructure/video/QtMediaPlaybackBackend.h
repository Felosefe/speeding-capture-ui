#pragma once

#include "IMediaPlaybackBackend.h"

#include <QElapsedTimer>
#include <QMetaObject>
#include <QPointer>
#include <QSize>
#include <QVector>

class QMediaPlayer;
class QVideoFrame;
class QVideoSink;
class QWidget;

namespace rv1126b {

class QtMediaPlaybackBackend final : public IMediaPlaybackBackend
{
    Q_OBJECT

public:
    explicit QtMediaPlaybackBackend(QObject* parent = nullptr);
    ~QtMediaPlaybackBackend() override;

    void play(const QUrl& url, quint64 attemptToken) override;
    void stop() override;
    QWidget* outputWidget() const override;

private:
    void disconnectAttemptSignals();
    void handleVideoFrame(const QVideoFrame& frame, quint64 attemptToken);

    QMediaPlayer* mediaPlayer_ = nullptr;
    QVideoSink* videoSink_ = nullptr;
    QPointer<QWidget> videoWidget_;
    QElapsedTimer renderThrottle_;
    // 上一次通知给上层的帧尺寸：窗口不可见时靠它判断"是否来了新分辨率"，
    // 保证车道线 overlay 的坐标系在任何情况下都能拿到尺寸。
    QSize lastNotifiedFrameSize_;
    QVector<QMetaObject::Connection> attemptConnections_;
};

} // namespace rv1126b
