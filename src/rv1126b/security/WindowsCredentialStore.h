#pragma once

#include "../ports/ISecretStore.h"

namespace rv1126b {

class WindowsCredentialStore final : public ISecretStore
{
    Q_OBJECT

public:
    explicit WindowsCredentialStore(QObject* parent = nullptr);

    QString credentialKeyForDevice(const QString& deviceId) const override;
    ApiResult<QString> storeToken(const QString& deviceId, SecretValue token) override;
    ApiResult<SecretValue> loadToken(const QString& credentialRef) const override;
    ApiResult<void> removeToken(const QString& credentialRef) override;

private:
    ApiError storageError(const QString& code, const QString& message) const;
};

} // namespace rv1126b
