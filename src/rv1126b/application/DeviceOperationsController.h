#pragma once

#include "../ports/IBoardApiClient.h"
#include "../services/FtpService.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>

#include <functional>
#include <optional>

namespace rv1126b {

using BoardApiResolver = std::function<IBoardApiClient*(const QString& deviceId)>;
using FtpServiceResolver = std::function<FtpService*(const QString& deviceId)>;

struct DeviceOperationsDependencies {
    BoardApiResolver boardApiForDevice;
    FtpServiceResolver ftpServiceForDevice;
};

class DeviceOperationsController final : public QObject
{
    Q_OBJECT

public:
    static constexpr int FtpTaskPageSize = 50;
    static constexpr int MaxFtpTargets = 8;

    explicit DeviceOperationsController(DeviceOperationsDependencies dependencies,
                                        QObject* parent = nullptr);
    ~DeviceOperationsController() override;

    QString deviceId() const;
    bool boardApiAvailable() const;
    bool ftpServiceAvailable() const;
    bool isBusy(const QString& operation) const;

    void selectDevice(const QString& deviceId);
    void loadAll();
    void loadEvidenceConfig();
    void saveEvidenceConfig(const EvidenceConfigUpdate& update);
    void loadTime();
    void setTimeUtc(qint64 utcEpochMs);
    void loadFtpConfig();
    void saveFtpConfigAndEnableNewEvents(const FtpConfigUpdate& update);
    void loadFtpControl();
    void updateFtpControl(bool enabled, FtpControlScope scope);
    void rollbackFtpConfig();
    void listFtpTasks(const std::optional<QString>& cursor = std::nullopt);
    void loadFtpTask(const QString& taskId);
    void createFtpTask(const FtpTaskCreate& request);
    void retryFtpTask(const QString& taskId);
    void cancelPending();
    void shutdown();

signals:
    void selectedDeviceChanged(const QString& deviceId);
    void operationBusyChanged(const QString& operation, bool busy);
    void evidenceConfigLoaded(const rv1126b::EvidenceConfigDto& config);
    void evidenceConfigSaved(const rv1126b::EvidenceConfigDto& config);
    void timeLoaded(const rv1126b::TimeStatusDto& time);
    void timeSaved(const rv1126b::TimeStatusDto& time);
    void ftpConfigLoaded(const rv1126b::FtpConfigSnapshotDto& config);
    void ftpActivationFinished(const rv1126b::FtpActivationResult& result);
    void ftpControlLoaded(const rv1126b::FtpControlDto& control);
    void ftpControlSaved(const rv1126b::FtpControlDto& control);
    void ftpConfigRolledBack(const rv1126b::FtpConfigSnapshotDto& config);
    void ftpRevisionConflict(const rv1126b::FtpConfigSnapshotDto& remote,
                             const rv1126b::FtpConfigUpdate& local,
                             const QStringList& passwordTargetIds);
    void ftpTasksLoaded(const rv1126b::FtpTaskPageDto& page, const QString& requestedCursor);
    void ftpTaskLoaded(const rv1126b::FtpTaskDetailDto& task);
    void ftpTaskCreated(const rv1126b::FtpTaskDetailDto& task);
    void ftpTaskRetried(const rv1126b::FtpTaskDetailDto& task);
    void userError(const QString& code, const QString& message);

private:
    int beginOperation(const QString& operation);
    void rememberBoardRequest(const QString& operation, int generation, const RequestId& requestId);
    bool finishOperation(const QString& operation, int generation);
    void invalidateOperations();
    void emitUnavailable(const QString& service);
    void reportError(const ApiError& error, const QString& fallback);
    std::optional<ApiError> validateEvidence(const EvidenceConfigUpdate& update) const;
    std::optional<ApiError> validateFtpConfig(const FtpConfigUpdate& update) const;
    std::optional<ApiError> validateTask(const FtpTaskCreate& request) const;
    static bool containsControlCharacter(const QString& value);
    static ApiError validationError(const QString& code, const QString& message);
    static FtpConfigUpdate withoutPasswords(FtpConfigUpdate update,
                                            QStringList* passwordTargetIds = nullptr);

    DeviceOperationsDependencies dependencies_;
    QString deviceId_;
    IBoardApiClient* boardApi_ = nullptr;
    FtpService* ftpService_ = nullptr;
    QHash<QString, int> generations_;
    QHash<QString, RequestId> boardRequests_;
    QSet<QString> busyOperations_;
    std::optional<FtpConfigSnapshotDto> ftpConfig_;
    std::optional<FtpControlDto> ftpControl_;
    std::optional<FtpConfigUpdate> conflictUpdate_;
    QStringList conflictPasswordTargets_;
    bool awaitingConflictReload_ = false;
    bool shutdown_ = false;
};

} // namespace rv1126b
