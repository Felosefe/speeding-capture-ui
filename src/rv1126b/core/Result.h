#pragma once

#include <QString>
#include <QMetaType>
#include <QUuid>

#include <functional>
#include <utility>
#include <variant>

namespace rv1126b {

using RequestId = QUuid;

enum class ApiErrorCategory {
    None,
    Authentication,
    CapabilityDisabled,
    Validation,
    Conflict,
    NotFound,
    Temporary,
    Network,
    Protocol,
    Cancelled,
    Storage,
    Unknown
};

struct ApiError {
    int httpStatus = 0;
    QString code;
    QString message;
    ApiErrorCategory category = ApiErrorCategory::Unknown;
    bool retryable = false;
};

template<typename T>
class ApiResult final
{
public:
    static ApiResult success(T value)
    {
        return ApiResult(std::move(value));
    }

    static ApiResult failure(ApiError error)
    {
        return ApiResult(std::move(error));
    }

    bool isSuccess() const noexcept
    {
        return std::holds_alternative<T>(data_);
    }

    explicit operator bool() const noexcept
    {
        return isSuccess();
    }

    T& value()
    {
        return std::get<T>(data_);
    }

    const T& value() const
    {
        return std::get<T>(data_);
    }

    ApiError& error()
    {
        return std::get<ApiError>(data_);
    }

    const ApiError& error() const
    {
        return std::get<ApiError>(data_);
    }

private:
    explicit ApiResult(T value)
        : data_(std::move(value))
    {
    }

    explicit ApiResult(ApiError error)
        : data_(std::move(error))
    {
    }

    std::variant<T, ApiError> data_;
};

template<>
class ApiResult<void> final
{
public:
    static ApiResult success()
    {
        return ApiResult(std::monostate {});
    }

    static ApiResult failure(ApiError error)
    {
        return ApiResult(std::move(error));
    }

    bool isSuccess() const noexcept
    {
        return std::holds_alternative<std::monostate>(data_);
    }

    explicit operator bool() const noexcept
    {
        return isSuccess();
    }

    ApiError& error()
    {
        return std::get<ApiError>(data_);
    }

    const ApiError& error() const
    {
        return std::get<ApiError>(data_);
    }

private:
    explicit ApiResult(std::monostate value)
        : data_(value)
    {
    }

    explicit ApiResult(ApiError error)
        : data_(std::move(error))
    {
    }

    std::variant<std::monostate, ApiError> data_;
};

template<typename T>
using Completion = std::function<void(T)>;

template<typename T>
using ApiCompletion = Completion<ApiResult<T>>;

} // namespace rv1126b

Q_DECLARE_METATYPE(rv1126b::ApiError)
