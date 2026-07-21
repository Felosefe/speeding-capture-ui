#pragma once

#include "../ports/IBoardApiClient.h"
#include "DeviceSession.h"

#include <QPointer>
#include <QTimer>

#include <functional>

namespace rv1126b {

class BoardDeviceSession final : public DeviceSession
{
    Q_OBJECT

public:
    using ProfileUpdater = std::function<void(const DeviceProfile&)>;

    explicit BoardDeviceSession(
        DeviceProfile profile,
        IBoardApiClient* apiClient,
        ProfileUpdater profileUpdater = {},
        QObject* parent = nullptr);
    ~BoardDeviceSession() override;

    DeviceProfile profile() const override;
    DeviceSessionSnapshot snapshot() const override;
    bool updateProfile(DeviceProfile profile);
    void connectSession() override;
    void disconnectSession() override;

private:
    void startHealthCheck();
    void handleHealthResult(ApiResult<HealthDto> result);
    void scheduleRetry();
    void setState(DeviceSessionState state, std::optional<ApiError> error = std::nullopt);
    ApiError localError(const QString& code, const QString& message, ApiErrorCategory category) const;

    DeviceSessionSnapshot snapshot_;
    QPointer<IBoardApiClient> apiClient_;
    ProfileUpdater profileUpdater_;
    QTimer retryTimer_;
    RequestId activeHealthRequest_;
    int retryAttempt_ = 0;
    bool healthInFlight_ = false;
    bool disconnectRequested_ = false;
};

} // namespace rv1126b
