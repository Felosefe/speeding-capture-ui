#include "QtMediaPlaybackBackend.h"

#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QVideoFrame>
#include <QVideoSink>
#include <QVideoWidget>

namespace rv1126b {

QtMediaPlaybackBackend::QtMediaPlaybackBackend(QObject* parent)
    : IMediaPlaybackBackend(parent)
    , mediaPlayer_(new QMediaPlayer(this))
    , videoWidget_(new QVideoWidget)
{
    videoWidget_->setMinimumSize(640, 360);
    videoWidget_->setAspectRatioMode(Qt::KeepAspectRatio);
    mediaPlayer_->setVideoOutput(videoWidget_);
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

    if (videoWidget_) {
        attemptConnections_.append(connect(
            videoWidget_->videoSink(), &QVideoSink::videoFrameChanged, this,
            [this, attemptToken](const QVideoFrame& frame) {
                if (frame.isValid()) {
                    emit frameReady(attemptToken, frame.size(), frame.surfaceFormat().streamFrameRate());
                }
            }));
    }

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

void QtMediaPlaybackBackend::disconnectAttemptSignals()
{
    for (const QMetaObject::Connection& connection : attemptConnections_) {
        disconnect(connection);
    }
    attemptConnections_.clear();
}

} // namespace rv1126b
