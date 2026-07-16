#pragma once

#include "../core/Result.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QObject>
#include <QString>

#include <utility>

namespace rv1126b {

class SecretValue final
{
public:
    SecretValue() = default;

    explicit SecretValue(QByteArray value)
        : value_(std::move(value))
    {
    }

    ~SecretValue()
    {
        clear();
    }

    SecretValue(const SecretValue&) = delete;
    SecretValue& operator=(const SecretValue&) = delete;

    SecretValue(SecretValue&& other) noexcept
        : value_(std::move(other.value_))
    {
        other.clear();
    }

    SecretValue& operator=(SecretValue&& other) noexcept
    {
        if (this != &other) {
            clear();
            value_ = std::move(other.value_);
            other.clear();
        }
        return *this;
    }

    bool isEmpty() const noexcept
    {
        return value_.isEmpty();
    }

    QByteArrayView view() const noexcept
    {
        return QByteArrayView(value_);
    }

    void clear() noexcept
    {
        value_.fill('\0');
        value_.clear();
        value_.squeeze();
    }

private:
    QByteArray value_;
};

class ISecretStore : public QObject
{
public:
    explicit ISecretStore(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    ~ISecretStore() override = default;

    virtual QString credentialKeyForDevice(const QString& deviceId) const = 0;
    virtual ApiResult<QString> storeToken(const QString& deviceId, SecretValue token) = 0;
    virtual ApiResult<SecretValue> loadToken(const QString& credentialRef) const = 0;
    virtual ApiResult<void> removeToken(const QString& credentialRef) = 0;
};

} // namespace rv1126b

