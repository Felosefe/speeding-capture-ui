#include "DirectDeviceProbeService.h"

#include "../protocol/ApiCodec.h"

#include <QHostAddress>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <utility>

namespace rv1126b {

DirectDeviceProbeService::DirectDeviceProbeService(const IApiCodec* codec, QObject* parent)
    : QObject(parent), codec_(codec)
{
}

RequestId DirectDeviceProbeService::probe(
    const QString& ipv4, quint16 port, SecretValue token,
    const QString& expectedDeviceId, QObject* context,
    ApiCompletion<DirectProbeResult> completion)
{
    const RequestId id = RequestId::createUuid();
    QHostAddress address;
    if (!codec_ || !context || port == 0 || !address.setAddress(ipv4)
        || address.protocol() != QAbstractSocket::IPv4Protocol) {
        QTimer::singleShot(0, context, [completion = std::move(completion), this]() mutable {
            completion(ApiResult<DirectProbeResult>::failure(localError(
                QStringLiteral("invalid_manual_endpoint"),
                QStringLiteral("请输入有效的 IPv4 地址和 HTTP 端口"),
                ApiErrorCategory::Validation)));
        });
        return id;
    }

    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(ipv4);
    url.setPort(port);
    url.setPath(QStringLiteral("/api/v1/health"));
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!token.isEmpty()) {
        request.setRawHeader("Authorization", QByteArray("Bearer ")
                                              + QByteArray(token.view().data(), token.view().size()));
    }

    auto pending = std::make_shared<Pending>();
    pending->reply = network_.get(request);
    pending->timer = new QTimer(this);
    pending->timer->setSingleShot(true);
    pending->context = context;
    pending->completion = std::move(completion);
    pending->token = std::move(token);
    pending->ipv4 = ipv4;
    pending->port = port;
    pending->expectedDeviceId = expectedDeviceId;
    pending_.insert(id, pending);

    connect(pending->reply, &QNetworkReply::finished, this, [this, id] { finish(id); });
    connect(pending->timer, &QTimer::timeout, this, [this, id] {
        fail(id, localError(QStringLiteral("manual_probe_timeout"),
                            QStringLiteral("设备 health 请求超时"),
                            ApiErrorCategory::Network, true));
    });
    pending->timer->start(10000);
    return id;
}

void DirectDeviceProbeService::cancel(const RequestId& requestId)
{
    auto it = pending_.find(requestId);
    if (it == pending_.end()) return;
    const std::shared_ptr<Pending> pending = it.value();
    if (pending->reply) pending->reply->abort();
    if (pending->timer) pending->timer->stop();
    if (pending->reply) pending->reply->deleteLater();
    if (pending->timer) pending->timer->deleteLater();
    pending_.erase(it);
}

void DirectDeviceProbeService::cancelAll()
{
    const QList<RequestId> ids = pending_.keys();
    for (const RequestId& id : ids) cancel(id);
}

void DirectDeviceProbeService::finish(const RequestId& requestId)
{
    auto it = pending_.find(requestId);
    if (it == pending_.end() || !it.value()->reply) return;
    const std::shared_ptr<Pending> pending = it.value();
    pending_.erase(it);
    pending->timer->stop();

    const int status = pending->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray payload = pending->reply->readAll();
    const QNetworkReply::NetworkError networkError = pending->reply->error();
    pending->reply->deleteLater();
    pending->timer->deleteLater();
    if (!pending->context) return;

    if (status >= 300 && status < 400) {
        pending->completion(ApiResult<DirectProbeResult>::failure(localError(
            QStringLiteral("manual_probe_redirect_rejected"),
            QStringLiteral("手工连接不允许 HTTP 重定向"), ApiErrorCategory::Protocol)));
        return;
    }
    if (status < 200 || status >= 300 || networkError != QNetworkReply::NoError) {
        ApiError error = status > 0 ? codec_->parseError(status, payload)
                                    : localError(QStringLiteral("manual_probe_network_error"),
                                                 QStringLiteral("无法连接该 IPv4 endpoint"),
                                                 ApiErrorCategory::Network, true);
        pending->completion(ApiResult<DirectProbeResult>::failure(std::move(error)));
        return;
    }
    ApiResult<HealthDto> healthResult = codec_->parseHealth(payload);
    if (!healthResult) {
        pending->completion(ApiResult<DirectProbeResult>::failure(healthResult.error()));
        return;
    }
    const HealthDto& health = healthResult.value();
    if (!pending->expectedDeviceId.isEmpty() && health.deviceId != pending->expectedDeviceId) {
        pending->completion(ApiResult<DirectProbeResult>::failure(localError(
            QStringLiteral("manual_probe_identity_mismatch"),
            QStringLiteral("health 返回的设备 ID 与所选历史设备不一致"),
            ApiErrorCategory::Protocol)));
        return;
    }

    DirectProbeResult result;
    result.device.deviceId = health.deviceId;
    result.device.deviceModel = health.deviceModel;
    result.device.releaseVersion = health.releaseVersion;
    result.device.apiVersion = health.apiVersion;
    result.device.ipv4 = pending->ipv4;
    result.device.authRequired = health.applicationApi.authRequired;
    result.device.magic = QStringLiteral("DIRECT_HEALTH");
    result.device.version = 1;
    result.device.type = QStringLiteral("direct_probe");
    result.device.apiUrl.setScheme(QStringLiteral("http"));
    result.device.apiUrl.setHost(pending->ipv4);
    result.device.apiUrl.setPort(pending->port);
    result.device.apiUrl.setPath(QStringLiteral("/api/v1"));
    result.token = std::move(pending->token);
    pending->completion(ApiResult<DirectProbeResult>::success(std::move(result)));
}

void DirectDeviceProbeService::fail(const RequestId& requestId, ApiError error)
{
    auto it = pending_.find(requestId);
    if (it == pending_.end()) return;
    const std::shared_ptr<Pending> pending = it.value();
    pending_.erase(it);
    if (pending->reply) {
        pending->reply->abort();
        pending->reply->deleteLater();
    }
    if (pending->timer) {
        pending->timer->stop();
        pending->timer->deleteLater();
    }
    if (pending->context) pending->completion(ApiResult<DirectProbeResult>::failure(std::move(error)));
}

ApiError DirectDeviceProbeService::localError(
    const QString& code, const QString& message, ApiErrorCategory category, bool retryable) const
{
    ApiError result;
    result.code = code;
    result.message = message;
    result.category = category;
    result.retryable = retryable;
    return result;
}

} // namespace rv1126b
