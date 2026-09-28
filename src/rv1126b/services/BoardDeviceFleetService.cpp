#include "BoardDeviceFleetService.h"

#include "BoardDeviceSession.h"
#include "BoardEventSyncService.h"
#include "BoardFtpService.h"
#include "BoardFtpTaskSnapshotService.h"
#include "../network/BoardApiClient.h"
#include "../protocol/BoardApiCodec.h"
#include "../ports/IEventRepository.h"
#include "../ports/ISecretStore.h"

#include <QPointer>

#include <algorithm>
#include <utility>

namespace rv1126b {

struct BoardDeviceFleetService::DeviceResources final : QObject
{
    explicit DeviceResources(QObject* parent) : QObject(parent) {}

    BoardApiClient* api = nullptr;
    BoardDeviceSession* session = nullptr;
    BoardEventSyncService* sync = nullptr;
    BoardFtpService* ftp = nullptr;
    BoardFtpTaskSnapshotService* ftpSnapshot = nullptr;
};

BoardDeviceFleetService::BoardDeviceFleetService(
    IEventRepository* repository,
    ISecretStore* secretStore,
    const BoardApiCodec* codec,
    QObject* parent)
    : DeviceFleetService(parent)
    , repository_(repository)
    , secretStore_(secretStore)
    , codec_(codec)
{
}

BoardDeviceFleetService::~BoardDeviceFleetService()
{
    shutdown();
}

RequestId BoardDeviceFleetService::initialize(QObject* context, ApiCompletion<void> completion)
{
    const RequestId requestId = RequestId::createUuid();
    if (shutdown_ || !repository_ || !secretStore_ || !codec_) {
        if (completion) completion(ApiResult<void>::failure(error(
            QStringLiteral("fleet_invalid_dependencies"),
            QStringLiteral("设备运行时依赖不完整"), ApiErrorCategory::Validation)));
        return requestId;
    }
    repository_->loadDeviceProfiles(context, [this, completion = std::move(completion)](
                                                ApiResult<QVector<DeviceProfile>> result) mutable {
        if (!result) {
            if (completion) completion(ApiResult<void>::failure(result.error()));
            return;
        }
        for (const DeviceProfile& profile : result.value()) {
            if (validProfile(profile)) createResources(profile);
        }
        initialized_ = true;
        if (completion) completion(ApiResult<void>::success());
    });
    return requestId;
}

QVector<DeviceSessionSnapshot> BoardDeviceFleetService::sessions() const
{
    QVector<DeviceSessionSnapshot> result;
    result.reserve(resources_.size());
    for (DeviceResources* resources : resources_) {
        if (resources && resources->session) result.append(resources->session->snapshot());
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.profile.deviceId < right.profile.deviceId;
    });
    return result;
}

bool BoardDeviceFleetService::upsertDevice(const DeviceProfile& profile)
{
    if (shutdown_ || !validProfile(profile)) return false;
    DeviceResources* resources = resources_.value(profile.deviceId);
    if (!resources) {
        resources = createResources(profile);
        if (!resources) return false;
    } else {
        resources->api->setProfile(profile);
        if (!resources->session->updateProfile(profile)) return false;
    }
    persistProfile(profile);
    return true;
}

bool BoardDeviceFleetService::connectDevice(const QString& deviceId)
{
    DeviceResources* resources = resources_.value(deviceId);
    if (shutdown_ || !resources || !resources->session) return false;
    const DeviceSessionState state = resources->session->snapshot().state;
    const bool alreadyActive = state == DeviceSessionState::Connecting
        || state == DeviceSessionState::Online || state == DeviceSessionState::Degraded
        || state == DeviceSessionState::Disconnecting;
    if (!alreadyActive && activeSessionCount() >= MaxConcurrentDataSessions) {
        emit fleetError(error(QStringLiteral("too_many_active_sessions"),
                              QStringLiteral("最多同时连接 8 台设备"),
                              ApiErrorCategory::Validation));
        return false;
    }
    resources->session->connectSession();
    return true;
}

void BoardDeviceFleetService::disconnectDevice(const QString& deviceId)
{
    DeviceResources* resources = resources_.value(deviceId);
    if (!resources) return;
    if (resources->sync) resources->sync->stop();
    if (resources->api) resources->api->cancelAll();
    if (resources->session) resources->session->disconnectSession();
}

void BoardDeviceFleetService::disconnectAll()
{
    const QStringList ids = resources_.keys();
    for (const QString& id : ids) disconnectDevice(id);
}

bool BoardDeviceFleetService::selectVideoDevice(const QString& deviceId)
{
    if (!resources_.contains(deviceId)) return false;
    if (selectedVideoDeviceId_ == deviceId) return true;
    selectedVideoDeviceId_ = deviceId;
    emit selectedVideoDeviceChanged(deviceId);
    return true;
}

QString BoardDeviceFleetService::selectedVideoDeviceId() const
{
    return selectedVideoDeviceId_;
}

IBoardApiClient* BoardDeviceFleetService::boardApiForDevice(const QString& deviceId) const
{
    DeviceResources* resources = resources_.value(deviceId);
    return resources ? resources->api : nullptr;
}

FtpService* BoardDeviceFleetService::ftpServiceForDevice(const QString& deviceId) const
{
    DeviceResources* resources = resources_.value(deviceId);
    return resources ? resources->ftp : nullptr;
}

EventSyncService* BoardDeviceFleetService::eventSyncForDevice(const QString& deviceId) const
{
    DeviceResources* resources = resources_.value(deviceId);
    return resources ? resources->sync : nullptr;
}

BoardFtpTaskSnapshotService* BoardDeviceFleetService::ftpSnapshotForDevice(
    const QString& deviceId) const
{
    DeviceResources* resources = resources_.value(deviceId);
    return resources ? resources->ftpSnapshot : nullptr;
}

void BoardDeviceFleetService::forgetDevice(
    const QString& deviceId, QObject* context, ApiCompletion<void> completion)
{
    DeviceResources* resources = resources_.value(deviceId);
    if (!resources || !repository_) {
        if (completion) completion(ApiResult<void>::failure(error(
            QStringLiteral("device_not_known"), QStringLiteral("设备档案不存在"),
            ApiErrorCategory::NotFound)));
        return;
    }
    disconnectDevice(deviceId);
    const QString credentialRef = resources->session->profile().credentialRef;
    if (!credentialRef.isEmpty() && secretStore_) {
        ApiResult<void> removed = secretStore_->removeToken(credentialRef);
        if (!removed) {
            if (completion) completion(ApiResult<void>::failure(removed.error()));
            return;
        }
    }
    repository_->deleteDeviceProfile(deviceId, context,
        [this, deviceId, completion = std::move(completion)](ApiResult<void> result) mutable {
            if (!result) {
                if (completion) completion(ApiResult<void>::failure(result.error()));
                return;
            }
            removeResources(deviceId);
            emit deviceRemoved(deviceId);
            if (completion) completion(ApiResult<void>::success());
        });
}

void BoardDeviceFleetService::shutdown()
{
    if (shutdown_) return;
    shutdown_ = true;
    disconnectAll();
    const QStringList ids = resources_.keys();
    for (const QString& id : ids) removeResources(id);
}

bool BoardDeviceFleetService::validProfile(const DeviceProfile& profile) const
{
    return !profile.deviceId.trimmed().isEmpty()
        && !profile.endpoint.ipv4.trimmed().isEmpty()
        && profile.endpoint.apiBaseUrl.isValid()
        && profile.endpoint.apiBaseUrl.scheme() == QStringLiteral("http")
        && !profile.endpoint.apiBaseUrl.host().isEmpty();
}

BoardDeviceFleetService::DeviceResources* BoardDeviceFleetService::createResources(
    const DeviceProfile& profile)
{
    if (resources_.contains(profile.deviceId)) return resources_.value(profile.deviceId);
    auto* resources = new DeviceResources(this);
    resources->api = new BoardApiClient(profile, secretStore_, codec_, resources);
    resources->session = new BoardDeviceSession(profile, resources->api, {}, resources);
    resources->sync = new BoardEventSyncService(profile.deviceId, resources->api,
                                                 repository_, resources);
    resources->ftp = new BoardFtpService(resources->api, resources);
    resources->ftpSnapshot = new BoardFtpTaskSnapshotService(profile.deviceId,
                                                              repository_, resources);
    resources_.insert(profile.deviceId, resources);

    connect(resources->session, &DeviceSession::stateChanged, this,
            [this](const DeviceSessionSnapshot& snapshot) {
                if (DeviceResources* current = resources_.value(snapshot.profile.deviceId)) {
                    current->api->setProfile(snapshot.profile);
                }
                if (snapshot.state == DeviceSessionState::Online) persistProfile(snapshot.profile);
                emit sessionChanged(snapshot);
            });
    connect(resources->session, &DeviceSession::healthUpdated, this,
            [this](const QString& deviceId, const HealthDto& health) {
                DeviceResources* current = resources_.value(deviceId);
                if (!current) return;
                DeviceProfile profile = current->session->profile();
                profile.deviceModel = health.deviceModel;
                profile.releaseVersion = health.releaseVersion;
                profile.endpoint.httpPort = health.applicationApi.httpPort;
                profile.endpoint.discoveryPort = health.applicationApi.discoveryPort;
                profile.endpoint.apiBaseUrl.setPort(health.applicationApi.httpPort);
                profile.endpoint.apiBaseUrl.setPath(QStringLiteral("/api/v1"));
                current->api->setProfile(profile);
                current->session->updateProfile(profile);
                persistProfile(profile);
            });
    connect(resources->session, &DeviceSession::sessionError, this,
            [this](const QString&, const ApiError& sessionError) { emit fleetError(sessionError); });

    emit deviceAdded(profile);
    emit eventSyncServiceAdded(resources->sync);
    return resources;
}

void BoardDeviceFleetService::persistProfile(const DeviceProfile& profile)
{
    if (!repository_ || shutdown_) return;
    repository_->upsertDevice(profile, this, [this](ApiResult<void> result) {
        if (!result) emit fleetError(result.error());
    });
}

void BoardDeviceFleetService::removeResources(const QString& deviceId)
{
    DeviceResources* resources = resources_.take(deviceId);
    if (!resources) return;
    if (selectedVideoDeviceId_ == deviceId) {
        selectedVideoDeviceId_.clear();
        emit selectedVideoDeviceChanged({});
    }
    delete resources;
}

int BoardDeviceFleetService::activeSessionCount() const
{
    int count = 0;
    for (DeviceResources* resources : resources_) {
        if (!resources || !resources->session) continue;
        const DeviceSessionState state = resources->session->snapshot().state;
        if (state == DeviceSessionState::Connecting || state == DeviceSessionState::Online
            || state == DeviceSessionState::Degraded
            || state == DeviceSessionState::Disconnecting) ++count;
    }
    return count;
}

ApiError BoardDeviceFleetService::error(
    const QString& code, const QString& message, ApiErrorCategory category) const
{
    ApiError result;
    result.code = code;
    result.message = message;
    result.category = category;
    return result;
}

} // namespace rv1126b
