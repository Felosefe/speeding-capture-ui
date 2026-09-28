#pragma once

#include "../core/Result.h"
#include "../ports/ISecretStore.h"
#include "../protocol/ApiDtos.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QPointer>

#include <memory>

class QNetworkReply;
class QTimer;

namespace rv1126b {

class IApiCodec;

struct DirectProbeResult {
    DiscoveredDeviceDto device;
    SecretValue token;
};

class DirectDeviceProbeService final : public QObject
{
    Q_OBJECT

public:
    explicit DirectDeviceProbeService(const IApiCodec* codec, QObject* parent = nullptr);

    RequestId probe(const QString& ipv4, quint16 port, SecretValue token,
                    const QString& expectedDeviceId, QObject* context,
                    ApiCompletion<DirectProbeResult> completion);
    void cancel(const RequestId& requestId);
    void cancelAll();

private:
    struct Pending {
        QPointer<QNetworkReply> reply;
        QPointer<QTimer> timer;
        QPointer<QObject> context;
        ApiCompletion<DirectProbeResult> completion;
        SecretValue token;
        QString ipv4;
        quint16 port = 0;
        QString expectedDeviceId;
    };

    void finish(const RequestId& requestId);
    void fail(const RequestId& requestId, ApiError error);
    ApiError localError(const QString& code, const QString& message,
                        ApiErrorCategory category, bool retryable = false) const;

    const IApiCodec* codec_ = nullptr;
    QNetworkAccessManager network_;
    QHash<RequestId, std::shared_ptr<Pending>> pending_;
};

} // namespace rv1126b

Q_DECLARE_METATYPE(rv1126b::DirectProbeResult)
