#include "QtMediaPlaybackBackend.h"

#include <QImage>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QPainter>
#include <QPaintEvent>
#include <QPlaybackOptions>
#include <QVideoFrame>
#include <QVideoSink>
#include <QWidget>

#include <chrono>

namespace {

class VideoFrameWidget final : public QWidget
{
public:
    explicit VideoFrameWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(640, 360);
        setAutoFillBackground(false);
        setAttribute(Qt::WA_OpaquePaintEvent);
    }

    void setFrame(const QImage& frame)
    {
        frame_ = frame;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(0, 0, 0));
        if (frame_.isNull()) {
            return;
        }

        QSize targetSize = frame_.size();
        targetSize.scale(size(), Qt::KeepAspectRatio);
        const QRect target(
            (width() - targetSize.width()) / 2,
            (height() - targetSize.height()) / 2,
            targetSize.width(),
            targetSize.height());
        painter.drawImage(target, frame_);
    }

private:
    QImage frame_;
};

} // namespace

namespace rv1126b {

QtMediaPlaybackBackend::QtMediaPlaybackBackend(QObject* parent)
    : IMediaPlaybackBackend(parent)
    , mediaPlayer_(new QMediaPlayer(this))
    , videoSink_(new QVideoSink(this))
    , videoWidget_(new VideoFrameWidget)
{
    mediaPlayer_->setVideoOutput(videoSink_);
}

QtMediaPlaybackBackend::~QtMediaPlaybackBackend()
{
    stop();
    mediaPlayer_->setVideoOutput(nullptr);
    if (videoWidget_ && !videoWidget_->parent()) {
        delete videoWidget_;
    }
}

void QtMediaPlaybackBackend::play(const QUrl& url, quint64 attemptToken)
{
    stop();

    QPlaybackOptions options;
    options.setPlaybackIntent(QPlaybackOptions::PlaybackIntent::LowLatencyStreaming);
    options.setProbeSize(64 * 1024);
    options.setNetworkTimeout(std::chrono::milliseconds(5000));
    mediaPlayer_->setPlaybackOptions(options);

    attemptConnections_.append(connect(
        videoSink_, &QVideoSink::videoFrameChanged, this,
        [this, attemptToken](const QVideoFrame& frame) {
            handleVideoFrame(frame, attemptToken);
        }));

    attemptConnections_.append(connect(
        mediaPlayer_, &QMediaPlayer::errorOccurred, this,
        [this, attemptToken](QMediaPlayer::Error error, const QString&) {
            if (error == QMediaPlayer::NoError) {
                return;
            }

            MediaPlaybackFailure failure = MediaPlaybackFailure::Backend;
            switch (error) {
            case QMediaPlayer::AccessDeniedError:
                failure = MediaPlaybackFailure::Authentication;
                break;
            case QMediaPlayer::FormatError:
                failure = MediaPlaybackFailure::Format;
                break;
            case QMediaPlayer::NetworkError:
            case QMediaPlayer::ResourceError:
                failure = MediaPlaybackFailure::Network;
                break;
            case QMediaPlayer::NoError:
                return;
            }
            emit playbackFailed(attemptToken, failure);
        }));

    attemptConnections_.append(connect(
        mediaPlayer_, &QMediaPlayer::mediaStatusChanged, this,
        [this, attemptToken](QMediaPlayer::MediaStatus status) {
            if (status == QMediaPlayer::EndOfMedia) {
                emit streamEnded(attemptToken);
            }
        }));

    attemptConnections_.append(connect(
        mediaPlayer_, &QMediaPlayer::metaDataChanged, this,
        [this, attemptToken]() {
            const QMediaMetaData metadata = mediaPlayer_->metaData();
            emit metadataReady(
                attemptToken,
                metadata.stringValue(QMediaMetaData::VideoCodec),
                metadata.value(QMediaMetaData::Resolution).toSize(),
                metadata.value(QMediaMetaData::VideoFrameRate).toReal());
        }));

    mediaPlayer_->setSource(url);
    mediaPlayer_->play();
}

void QtMediaPlaybackBackend::stop()
{
    disconnectAttemptSignals();
    mediaPlayer_->stop();
    mediaPlayer_->setSource(QUrl());
}

QWidget* QtMediaPlaybackBackend::outputWidget() const
{
    return videoWidget_;
}

void QtMediaPlaybackBackend::handleVideoFrame(const QVideoFrame& frame, quint64 attemptToken)
{
    if (!frame.isValid()) {
        return;
    }
    emit frameReady(attemptToken, frame.size(), frame.surfaceFormat().streamFrameRate());
    if (!videoWidget_) {
        return;
    }
    if (renderThrottle_.isValid() && renderThrottle_.elapsed() < 33) {
        return;
    }
    renderThrottle_.restart();
    static_cast<VideoFrameWidget*>(videoWidget_.data())->setFrame(frame.toImage());
}

void QtMediaPlaybackBackend::disconnectAttemptSignals()
{
    for (const QMetaObject::Connection& connection : attemptConnections_) {
        disconnect(connection);
    }
    attemptConnections_.clear();
}

} // namespace rv1126b
