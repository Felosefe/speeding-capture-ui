#pragma once

#include "IMediaPlaybackBackend.h"

#include <QMetaObject>
#include <QPointer>
#include <QVector>

class QMediaPlayer;
class QVideoWidget;

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

    QMediaPlayer* mediaPlayer_ = nullptr;
    QPointer<QVideoWidget> videoWidget_;
    QVector<QMetaObject::Connection> attemptConnections_;
};

} // namespace rv1126b
