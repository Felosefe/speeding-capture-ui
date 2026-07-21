#pragma once

#include "../../models/SystemSettings.h"
#include "../../ui/MainWindow.h"
#include "../core/Result.h"

#include <QObject>

#include <memory>

namespace rv1126b {

class BoardApiCodec;
class BoardDeviceFleetService;
class BoardEvidenceCache;
class EvidenceCacheMaintenanceService;
class DirectDeviceProbeService;
class QtMultimediaRtspPlayer;
class SqliteEventRepository;
class UdpDeviceDiscoveryService;
class WindowsCredentialStore;

class Rv1126bApplicationRuntime final : public QObject
{
    Q_OBJECT

public:
    explicit Rv1126bApplicationRuntime(SystemSettings settings, QObject* parent = nullptr);
    Rv1126bApplicationRuntime(SystemSettings settings,
                              QString appDataRootPath,
                              QObject* parent = nullptr);
    ~Rv1126bApplicationRuntime() override;

    void initialize(QObject* context, ApiCompletion<void> completion);
    MainWindowDependencies mainWindowDependencies() const;

    BoardDeviceFleetService* fleet() const;
    SqliteEventRepository* repository() const;
    BoardEvidenceCache* evidenceCache() const;
    EvidenceCacheMaintenanceService* evidenceMaintenance() const;
    QtMultimediaRtspPlayer* player() const;
    QString evidenceRootPath() const;
    void shutdown();

private:
    void importLegacyDataIfNeeded();

    SystemSettings settings_;
    std::unique_ptr<BoardApiCodec> codec_;
    UdpDeviceDiscoveryService* discovery_ = nullptr;
    DirectDeviceProbeService* directProbe_ = nullptr;
    WindowsCredentialStore* secretStore_ = nullptr;
    SqliteEventRepository* repository_ = nullptr;
    BoardDeviceFleetService* fleet_ = nullptr;
    BoardEvidenceCache* evidenceCache_ = nullptr;
    EvidenceCacheMaintenanceService* evidenceMaintenance_ = nullptr;
    QtMultimediaRtspPlayer* player_ = nullptr;
    QString databasePath_;
    QString evidenceRootPath_;
    bool initialized_ = false;
    bool shutdown_ = false;
};

} // namespace rv1126b
