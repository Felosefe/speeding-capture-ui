#pragma once

#include "DeviceFleetService.h"

#include "../core/Result.h"

#include <QHash>

namespace rv1126b {

class BoardApiCodec;
class BoardApiClient;
class BoardDeviceSession;
class BoardEventSyncService;
class BoardFtpService;
class BoardFtpTaskSnapshotService;
class EventSyncService;
class FtpService;
class IBoardApiClient;
class IEventRepository;
class ISecretStore;

class BoardDeviceFleetService final : public DeviceFleetService
{
    Q_OBJECT

public:
    BoardDeviceFleetService(IEventRepository* repository,
                            ISecretStore* secretStore,
                            const BoardApiCodec* codec,
                            QObject* parent = nullptr);
    ~BoardDeviceFleetService() override;

    RequestId initialize(QObject* context, ApiCompletion<void> completion);

    QVector<DeviceSessionSnapshot> sessions() const override;
    bool upsertDevice(const DeviceProfile& profile) override;
    bool connectDevice(const QString& deviceId) override;
    void disconnectDevice(const QString& deviceId) override;
    void disconnectAll() override;
    bool selectVideoDevice(const QString& deviceId) override;
    QString selectedVideoDeviceId() const override;

    IBoardApiClient* boardApiForDevice(const QString& deviceId) const;
    FtpService* ftpServiceForDevice(const QString& deviceId) const;
    EventSyncService* eventSyncForDevice(const QString& deviceId) const;
    BoardFtpTaskSnapshotService* ftpSnapshotForDevice(const QString& deviceId) const;

    void forgetDevice(const QString& deviceId,
                      QObject* context,
                      ApiCompletion<void> completion);
    void shutdown();

signals:
    void eventSyncServiceAdded(rv1126b::EventSyncService* service);
    void deviceRemoved(const QString& deviceId);

private:
    struct DeviceResources;

    bool validProfile(const DeviceProfile& profile) const;
    DeviceResources* createResources(const DeviceProfile& profile);
    void persistProfile(const DeviceProfile& profile);
    void removeResources(const QString& deviceId);
    int activeSessionCount() const;
    ApiError error(const QString& code, const QString& message,
                   ApiErrorCategory category) const;

    IEventRepository* repository_ = nullptr;
    ISecretStore* secretStore_ = nullptr;
    const BoardApiCodec* codec_ = nullptr;
    QHash<QString, DeviceResources*> resources_;
    QString selectedVideoDeviceId_;
    bool initialized_ = false;
    bool shutdown_ = false;
};

} // namespace rv1126b
