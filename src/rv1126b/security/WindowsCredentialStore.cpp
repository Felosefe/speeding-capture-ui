#include "WindowsCredentialStore.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#endif

namespace rv1126b {
namespace {

constexpr auto CredentialPrefix = "RV1126B/";

bool validDeviceId(const QString& deviceId)
{
    return !deviceId.trimmed().isEmpty() && !deviceId.contains(QChar::Null);
}

} // namespace

WindowsCredentialStore::WindowsCredentialStore(QObject* parent)
    : ISecretStore(parent)
{
}

QString WindowsCredentialStore::credentialKeyForDevice(const QString& deviceId) const
{
    if (!validDeviceId(deviceId)) {
        return {};
    }
    return QString::fromLatin1(CredentialPrefix) + deviceId;
}

ApiResult<QString> WindowsCredentialStore::storeToken(const QString& deviceId, SecretValue token)
{
    const QString credentialRef = credentialKeyForDevice(deviceId);
    if (credentialRef.isEmpty()) {
        return ApiResult<QString>::failure(storageError(
            QStringLiteral("credential_invalid_device_id"),
            QStringLiteral("A device id is required to store a credential.")));
    }
    if (token.isEmpty()) {
        return ApiResult<QString>::failure(storageError(
            QStringLiteral("credential_empty_token"),
            QStringLiteral("An empty credential cannot be stored.")));
    }

#ifdef Q_OS_WIN
    const QByteArrayView tokenView = token.view();
    if (tokenView.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
        return ApiResult<QString>::failure(storageError(
            QStringLiteral("credential_too_large"),
            QStringLiteral("The credential is too large for Windows Credential Manager.")));
    }

    CREDENTIALW credential {};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = reinterpret_cast<LPWSTR>(const_cast<ushort*>(credentialRef.utf16()));
    credential.CredentialBlobSize = static_cast<DWORD>(tokenView.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(tokenView.data()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;

    if (!CredWriteW(&credential, 0)) {
        return ApiResult<QString>::failure(storageError(
            QStringLiteral("credential_write_failed"),
            QStringLiteral("Windows Credential Manager could not store the credential.")));
    }
    token.clear();
    return ApiResult<QString>::success(credentialRef);
#else
    Q_UNUSED(token);
    return ApiResult<QString>::failure(storageError(
        QStringLiteral("credential_store_unavailable"),
        QStringLiteral("Windows Credential Manager is unavailable on this platform.")));
#endif
}

ApiResult<SecretValue> WindowsCredentialStore::loadToken(const QString& credentialRef) const
{
    if (credentialRef.isEmpty()) {
        return ApiResult<SecretValue>::failure(storageError(
            QStringLiteral("credential_missing"),
            QStringLiteral("A credential reference is required.")));
    }

#ifdef Q_OS_WIN
    PCREDENTIALW rawCredential = nullptr;
    if (!CredReadW(
            reinterpret_cast<LPCWSTR>(credentialRef.utf16()),
            CRED_TYPE_GENERIC,
            0,
            &rawCredential)) {
        const DWORD errorCode = GetLastError();
        return ApiResult<SecretValue>::failure(storageError(
            errorCode == ERROR_NOT_FOUND
                ? QStringLiteral("credential_not_found")
                : QStringLiteral("credential_read_failed"),
            errorCode == ERROR_NOT_FOUND
                ? QStringLiteral("The board credential was not found.")
                : QStringLiteral("Windows Credential Manager could not read the credential.")));
    }

    QByteArray tokenBytes(
        reinterpret_cast<const char*>(rawCredential->CredentialBlob),
        rawCredential->CredentialBlobSize);
    CredFree(rawCredential);
    if (tokenBytes.isEmpty()) {
        return ApiResult<SecretValue>::failure(storageError(
            QStringLiteral("credential_empty_token"),
            QStringLiteral("The stored board credential is empty.")));
    }
    return ApiResult<SecretValue>::success(SecretValue(std::move(tokenBytes)));
#else
    return ApiResult<SecretValue>::failure(storageError(
        QStringLiteral("credential_store_unavailable"),
        QStringLiteral("Windows Credential Manager is unavailable on this platform.")));
#endif
}

ApiResult<void> WindowsCredentialStore::removeToken(const QString& credentialRef)
{
    if (credentialRef.isEmpty()) {
        return ApiResult<void>::failure(storageError(
            QStringLiteral("credential_missing"),
            QStringLiteral("A credential reference is required.")));
    }

#ifdef Q_OS_WIN
    if (!CredDeleteW(reinterpret_cast<LPCWSTR>(credentialRef.utf16()), CRED_TYPE_GENERIC, 0)
        && GetLastError() != ERROR_NOT_FOUND) {
        return ApiResult<void>::failure(storageError(
            QStringLiteral("credential_delete_failed"),
            QStringLiteral("Windows Credential Manager could not remove the credential.")));
    }
    return ApiResult<void>::success();
#else
    return ApiResult<void>::failure(storageError(
        QStringLiteral("credential_store_unavailable"),
        QStringLiteral("Windows Credential Manager is unavailable on this platform.")));
#endif
}

ApiError WindowsCredentialStore::storageError(const QString& code, const QString& message) const
{
    ApiError error;
    error.code = code;
    error.message = message;
    error.category = ApiErrorCategory::Storage;
    return error;
}

} // namespace rv1126b
