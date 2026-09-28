#pragma once

#include <QMetaType>
#include <QObject>
#include <QSize>
#include <QString>
#include <QUrl>

class QWidget;

namespace rv1126b {

enum class MediaPlaybackFailure {
    Authentication,
    Format,
    Network,
    Backend
};

class IMediaPlaybackBackend : public QObject
{
    Q_OBJECT

public:
    explicit IMediaPlaybackBackend(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    ~IMediaPlaybackBackend() override = default;

    virtual void play(const QUrl& url, quint64 attemptToken) = 0;
    virtual void stop() = 0;
    virtual QWidget* outputWidget() const = 0;

signals:
    void frameReady(quint64 attemptToken, QSize frameSize, qreal declaredFrameRate);
    void metadataReady(quint64 attemptToken, QString codec, QSize resolution, qreal frameRate);
    void playbackFailed(quint64 attemptToken, rv1126b::MediaPlaybackFailure failure);
    void streamEnded(quint64 attemptToken);
};

} // namespace rv1126b

Q_DECLARE_METATYPE(rv1126b::MediaPlaybackFailure)
