#pragma once

#include "../core/Result.h"

#include <QMetaType>
#include <QObject>
#include <QString>
#include <QUrl>

class QWidget;

namespace rv1126b {

enum class RtspStreamRole {
    Main,
    Sub
};

enum class RtspPlayerState {
    Idle,
    Opening,
    Playing,
    Reconnecting,
    Stopped,
    Error
};

struct RtspStreamSpec {
    QString deviceId;
    QUrl url;
    RtspStreamRole role = RtspStreamRole::Sub;
    int openTimeoutMs = 3000;
};

class IRtspPlayer : public QObject
{
    Q_OBJECT

public:
    explicit IRtspPlayer(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    ~IRtspPlayer() override = default;

    virtual void open(const RtspStreamSpec& stream) = 0;
    virtual void stop() = 0;
    virtual RtspPlayerState state() const = 0;
    virtual QWidget* outputWidget() const = 0;

signals:
    void stateChanged(rv1126b::RtspPlayerState state);
    void errorOccurred(const rv1126b::ApiError& error);
};

} // namespace rv1126b

Q_DECLARE_METATYPE(rv1126b::RtspPlayerState)
Q_DECLARE_METATYPE(rv1126b::RtspStreamSpec)

