#include "BoardDeviceSession.h"

#include <QDateTime>

#include <array>

namespace rv1126b
{
    namespace
    {

        constexpr std::array<int, 5> RetryDelaysMs{1000, 2000, 6000, 30000, 60000};

        bool shouldRetry(const ApiError &error)
        {
            return error.retryable || error.category == ApiErrorCategory::Network || error.category == ApiErrorCategory::Temporary;
        }

    } // namespace

    BoardDeviceSession::BoardDeviceSession(
        DeviceProfile profile,
        IBoardApiClient *apiClient,
        ProfileUpdater profileUpdater,
        QObject *parent)
        : DeviceSession(parent), apiClient_(apiClient), profileUpdater_(std::move(profileUpdater))
    {
        snapshot_.profile = std::move(profile);
        retryTimer_.setSingleShot(true);
        connect(&retryTimer_, &QTimer::timeout, this, [this]
                { startHealthCheck(); });
    }

    BoardDeviceSession::~BoardDeviceSession()
    {
        retryTimer_.stop();
        if (apiClient_ && !activeHealthRequest_.isNull())
        {
            apiClient_->cancel(activeHealthRequest_);
        }
    }

    DeviceProfile BoardDeviceSession::profile() const
    {
        return snapshot_.profile;
    }

    DeviceSessionSnapshot BoardDeviceSession::snapshot() const
    {
        return snapshot_;
    }

    bool BoardDeviceSession::updateProfile(DeviceProfile profile)
    {
        if (profile.deviceId.isEmpty() || profile.deviceId != snapshot_.profile.deviceId || !profile.endpoint.apiBaseUrl.isValid() || profile.endpoint.apiBaseUrl.scheme() != QStringLiteral("http") || profile.endpoint.apiBaseUrl.host().isEmpty())
        {
            return false;
        }
        if (profile.credentialRef.isEmpty())
        {
            profile.credentialRef = snapshot_.profile.credentialRef;
        }
        snapshot_.profile = std::move(profile);
        if (profileUpdater_)
        {
            profileUpdater_(snapshot_.profile);
        }
        emit stateChanged(snapshot_);
        return true;
    }

    void BoardDeviceSession::connectSession()
    {
        if (!apiClient_)
        {
            const ApiError error = localError(
                QStringLiteral("rv1126b.session.missing_api_client"),
                QStringLiteral("The board API client is unavailable."),
                ApiErrorCategory::Network);
            setState(DeviceSessionState::Degraded, error);
            emit sessionError(snapshot_.profile.deviceId, error);
            return;
        }
        if (healthInFlight_ || snapshot_.state == DeviceSessionState::Online)
        {
            return;
        }

        disconnectRequested_ = false;
        retryTimer_.stop();
        retryAttempt_ = 0;
        setState(DeviceSessionState::Connecting);
        startHealthCheck();
    }

    void BoardDeviceSession::disconnectSession()
    {
        if (snapshot_.state == DeviceSessionState::Disconnected)
        {
            return;
        }
        disconnectRequested_ = true;
        retryTimer_.stop();
        setState(DeviceSessionState::Disconnecting);
        if (apiClient_ && !activeHealthRequest_.isNull())
        {
            apiClient_->cancel(activeHealthRequest_);
        }
        activeHealthRequest_ = RequestId{};
        healthInFlight_ = false;
        retryAttempt_ = 0;
        setState(DeviceSessionState::Disconnected);
    }

    void BoardDeviceSession::startHealthCheck()
    {
        if (disconnectRequested_ || healthInFlight_ || !apiClient_)
        {
            return;
        }
        healthInFlight_ = true;
        activeHealthRequest_ = apiClient_->getHealth(this, [this](ApiResult<HealthDto> result)
                                                     { handleHealthResult(std::move(result)); });
    }

    void BoardDeviceSession::handleHealthResult(ApiResult<HealthDto> result)
    {
        if (disconnectRequested_ || snapshot_.state == DeviceSessionState::Disconnected)
        {
            return;
        }
        activeHealthRequest_ = RequestId{};
        healthInFlight_ = false;

        if (result.isSuccess())
        {
            const HealthDto &health = result.value();
            if (health.deviceId != snapshot_.profile.deviceId)
            {
                const ApiError error = localError(
                    QStringLiteral("rv1126b.session.identity_mismatch"),
                    QStringLiteral("Health response device id does not match the selected device."),
                    ApiErrorCategory::Protocol);
                setState(DeviceSessionState::Degraded, error);
                emit sessionError(snapshot_.profile.deviceId, error);
                return;
            }
            retryAttempt_ = 0;
            snapshot_.lastHealthEpochMs = QDateTime::currentMSecsSinceEpoch();
            snapshot_.profile.lastOnlineEpochMs = snapshot_.lastHealthEpochMs;
            setState(DeviceSessionState::Online);
            emit healthUpdated(snapshot_.profile.deviceId, health);
            return;
        }

        const ApiError error = result.error();
        emit sessionError(snapshot_.profile.deviceId, error);
        if (error.category == ApiErrorCategory::Authentication)
        {
            setState(DeviceSessionState::AuthenticationFailed, error);
            return;
        }
        setState(DeviceSessionState::Degraded, error);
        if (shouldRetry(error))
        {
            scheduleRetry();
        }
    }

    void BoardDeviceSession::scheduleRetry()
    {
        const int delayIndex = std::min(retryAttempt_, static_cast<int>(RetryDelaysMs.size()) - 1);
        ++retryAttempt_;
        retryTimer_.start(RetryDelaysMs[delayIndex]);
    }

    void BoardDeviceSession::setState(DeviceSessionState state, std::optional<ApiError> error)
    {
        snapshot_.state = state;
        snapshot_.lastError = std::move(error);
        emit stateChanged(snapshot_);
    }

    ApiError BoardDeviceSession::localError(
        const QString &code,
        const QString &message,
        ApiErrorCategory category) const
    {
        ApiError error;
        error.code = code;
        error.message = message;
        error.category = category;
        return error;
    }

} // namespace rv1126b
