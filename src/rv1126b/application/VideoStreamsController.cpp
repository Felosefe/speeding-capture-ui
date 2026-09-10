#include "VideoStreamsController.h"

#include <QTimer>

namespace rv1126b {

VideoStreamsController::VideoStreamsController(QObject* parent) : QObject(parent) {}

void VideoStreamsController::setDevice(const QString& deviceId, IBoardApiClient* api)
{
    if (deviceId_ == deviceId && api_ == api && requestContext_) return;
    ++generation_;
    disconnect(destroyedConnection_);
    delete requestContext_.data();
    requestContext_ = new QObject(this);
    deviceId_ = deviceId;
    api_ = api;
    snapshot_.reset();
    confirmationTarget_.reset();
    draft_ = VideoStreamsUpdate{};
    busy_ = false;
    if (!available()) {
        setStatus(QStringLiteral("连接设备后可配置码流"));
        return;
    }
    destroyedConnection_ = connect(api, &QObject::destroyed, this, [this] {
        delete requestContext_.data();
        setDevice(deviceId_, nullptr);
    });
    refresh();
}

bool VideoStreamsController::current(quint64 generation) const
{
    return generation == generation_ && available();
}

bool VideoStreamsController::dirty() const
{
    return snapshot_ && (!(snapshot_->main == draft_.main) || !(snapshot_->sub == draft_.sub));
}

bool VideoStreamsController::canApply() const
{
    return editable() && (dirty() || snapshot_->restartRequired);
}

QString VideoStreamsController::codec(RtspStreamRole role) const
{
    return role == RtspStreamRole::Main ? draft_.main.codec : draft_.sub.codec;
}

void VideoStreamsController::setStatus(const QString& status, bool error)
{
    status_ = status;
    error_ = error;
    emit changed();
}

void VideoStreamsController::adopt(const VideoStreamsConfigDto& config, bool preserveDraft)
{
    snapshot_ = config;
    draft_.expectedRevision = config.revision;
    if (!preserveDraft) {
        draft_.main = mainVideoStreamDefaults();
        draft_.sub = subVideoStreamDefaults();
        draft_.main.codec = config.main.codec;
        draft_.sub.codec = config.sub.codec;
    }
}

void VideoStreamsController::refresh()
{
    if (!available() || busy_) return;
    read(false);
}

void VideoStreamsController::read(bool preserveDraft, bool conflict)
{
    busy_ = true;
    setStatus(QStringLiteral("读取码流配置…"));
    const auto generation = generation_;
    api_->getVideoStreamsConfig(requestContext_, [this, generation, preserveDraft, conflict](ApiResult<VideoStreamsConfigDto> result) {
        if (!current(generation)) return;
        busy_ = false;
        if (!result) {
            readFailure(result.error());
            return;
        }
        adopt(result.value(), preserveDraft);
        if (confirm(result.value())) return;
        if (!result.value().writeEnabled) setStatus(QStringLiteral("板端禁止写入码流配置"));
        else if (conflict) setStatus(QStringLiteral("配置已被其他客户端修改，已刷新版本，请检查后重新应用"), true);
        else if (result.value().restartRequired) setStatus(QStringLiteral("配置已保存，待应用"));
        else if (dirty()) setStatus(QStringLiteral("板端分辨率尚未更新，点击应用设置主码流2K、辅码流1080p"));
        else setStatus(QStringLiteral("码流配置已读取"));
    });
}

void VideoStreamsController::setCodec(RtspStreamRole role, const QString& codec)
{
    if (!editable() || (codec != QLatin1String("h264") && codec != QLatin1String("h265"))) return;
    (role == RtspStreamRole::Main ? draft_.main : draft_.sub).codec = codec;
    setStatus(dirty() ? QStringLiteral("码流设置未提交")
                     : snapshot_->restartRequired ? QStringLiteral("配置已保存，待应用")
                                                  : QStringLiteral("码流配置已读取"));
}

void VideoStreamsController::apply()
{
    if (!canApply()) return;
    busy_ = true;
    if (!dirty()) {
        confirmationTarget_ = draft_;
        applySaved();
        return;
    }
    setStatus(QStringLiteral("保存码流配置…"));
    const auto generation = generation_;
    api_->putVideoStreamsConfig(draft_, requestContext_, [this, generation](ApiResult<VideoStreamsConfigDto> result) {
        if (!current(generation)) return;
        if (!result) {
            if (result.error().category == ApiErrorCategory::Conflict || result.error().httpStatus == 409) {
                confirmationTarget_.reset();
                read(true, true);
                return;
            }
            busy_ = false;
            if (result.error().category == ApiErrorCategory::CapabilityDisabled
                || result.error().httpStatus == 404 || result.error().httpStatus == 501) {
                snapshot_->writeEnabled = false;
            }
            setStatus(QStringLiteral("码流保存失败：%1").arg(errorText(result.error())), true);
            return;
        }
        adopt(result.value(), true);
        if (dirty()) {
            busy_ = false;
            setStatus(QStringLiteral("板端保存结果与请求不一致，请刷新确认"), true);
            return;
        }
        confirmationTarget_ = draft_;
        applySaved();
    });
}

void VideoStreamsController::applySaved()
{
    if (!snapshot_->restartRequired) {
        verify();
        return;
    }
    setStatus(QStringLiteral("配置已保存，正在应用，视频可能短暂中断…"));
    RuntimeApplyUpdate update;
    update.expectedRevision = snapshot_->runtimeRevision;
    if (update.expectedRevision.isEmpty()) {
        busy_ = false;
        setStatus(QStringLiteral("已保存，待应用：缺少运行版本，请刷新"), true);
        return;
    }
    const auto generation = generation_;
    api_->applyRuntimeConfig(update, requestContext_, [this, generation](ApiResult<RuntimeApplyDto> result) {
        if (!current(generation)) return;
        if (!result && !transient(result.error())) {
            if (result.error().category == ApiErrorCategory::Conflict || result.error().httpStatus == 409) {
                read(true, true);
                return;
            }
            busy_ = false;
            setStatus(QStringLiteral("已保存，待应用：%1").arg(errorText(result.error())), true);
            return;
        }
        // A lost apply response is ambiguous. Read back; never automatically repeat POST.
        verify();
    });
}

bool VideoStreamsController::confirm(const VideoStreamsConfigDto& config)
{
    if (!confirmationTarget_ || config.restartRequired
        || !(config.main == confirmationTarget_->main) || !(config.sub == confirmationTarget_->sub)) return false;
    confirmationTarget_.reset();
    busy_ = false;
    const QString deviceId = deviceId_;
    const auto generation = generation_;
    setStatus(config.writeEnabled ? QStringLiteral("码流设置已生效")
                                 : QStringLiteral("码流设置已生效；板端禁止写入码流配置"));
    if (current(generation)) emit previewRestartRequested(deviceId);
    return true;
}

void VideoStreamsController::verify(int retry)
{
    setStatus(QStringLiteral("配置已保存，正在读回确认…"));
    const auto generation = generation_;
    api_->getVideoStreamsConfig(requestContext_, [this, generation, retry](ApiResult<VideoStreamsConfigDto> result) {
        if (!current(generation)) return;
        if (result) {
            adopt(result.value(), true);
            if (confirm(result.value())) return;
        } else if (!transient(result.error())) {
            busy_ = false;
            readFailure(result.error());
            return;
        }
        static constexpr int delays[] = {1000, 2000, 4000, 8000};
        if (retry < 4) {
            setStatus(QStringLiteral("等待板端应用，稍后自动读回…"));
            QTimer::singleShot(delays[retry], this, [this, generation, retry] {
                if (current(generation)) verify(retry + 1);
            });
            return;
        }
        busy_ = false;
        setStatus(QStringLiteral("配置已保存，生效状态未确认，请刷新；仍待应用时可重试应用"), true);
    });
}

bool VideoStreamsController::transient(const ApiError& error)
{
    return error.category == ApiErrorCategory::Network || error.category == ApiErrorCategory::Temporary;
}

QString VideoStreamsController::errorText(const ApiError& error)
{
    if (error.httpStatus == 404 || error.httpStatus == 501
        || error.code == QLatin1String("video_streams_config_unsupported"))
        return QStringLiteral("设备不支持码流配置，请升级板端接口");
    switch (error.category) {
    case ApiErrorCategory::Authentication: return QStringLiteral("认证失败，请检查设备Token");
    case ApiErrorCategory::CapabilityDisabled: return QStringLiteral("板端禁止此配置操作");
    case ApiErrorCategory::Network:
    case ApiErrorCategory::Temporary: return QStringLiteral("设备暂时不可用，请稍后刷新");
    case ApiErrorCategory::Validation: return QStringLiteral("板端拒绝码流参数，请检查接口适配");
    case ApiErrorCategory::Protocol: return QStringLiteral("板端返回的码流配置不兼容");
    default: return QStringLiteral("设备操作失败，请刷新后重试");
    }
}

void VideoStreamsController::readFailure(const ApiError& error)
{
    // No writes based on a stale revision after a failed refresh.
    snapshot_.reset();
    setStatus((confirmationTarget_ ? QStringLiteral("配置已保存，生效状态未确认：") : QString())
                  + errorText(error), true);
}

} // namespace rv1126b
