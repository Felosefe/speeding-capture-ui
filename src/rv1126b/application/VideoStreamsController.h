#pragma once

#include "../ports/IBoardApiClient.h"
#include "../ports/IRtspPlayer.h"

#include <QPointer>
#include <optional>

namespace rv1126b {

class VideoStreamsController final : public QObject
{
    Q_OBJECT
public:
    explicit VideoStreamsController(QObject* parent = nullptr);
    void setDevice(const QString& deviceId, IBoardApiClient* api);
    void refresh();
    void setCodec(RtspStreamRole role, const QString& codec);
    void apply();

    bool busy() const { return busy_; }
    bool available() const { return api_ && !deviceId_.isEmpty(); }
    bool editable() const { return available() && !busy_ && snapshot_ && snapshot_->writeEnabled; }
    bool canApply() const;
    bool hasConfig() const { return snapshot_.has_value(); }
    QString codec(RtspStreamRole role) const;
    QString status() const { return status_; }
    bool hasError() const { return error_; }

signals:
    void changed();
    void previewRestartRequested(const QString& deviceId);

private:
    bool current(quint64 generation) const;
    bool dirty() const;
    void setStatus(const QString& status, bool error = false);
    void adopt(const VideoStreamsConfigDto& config, bool preserveDraft);
    void read(bool preserveDraft, bool conflict = false);
    void applySaved();
    void verify(int retry = 0);
    bool confirm(const VideoStreamsConfigDto& config);
    void readFailure(const ApiError& error);
    static bool transient(const ApiError& error);
    static QString errorText(const ApiError& error);

    QString deviceId_;
    QPointer<IBoardApiClient> api_;
    QPointer<QObject> requestContext_;
    QMetaObject::Connection destroyedConnection_;
    quint64 generation_ = 0;
    std::optional<VideoStreamsConfigDto> snapshot_;
    VideoStreamsUpdate draft_;
    std::optional<VideoStreamsUpdate> confirmationTarget_;
    bool busy_ = false;
    bool error_ = false;
    QString status_;
};

} // namespace rv1126b
