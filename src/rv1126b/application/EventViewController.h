#pragma once

#include "../domain/Models.h"
#include "../ports/IEventRepository.h"
#include "../services/EvidenceCache.h"
#include "../services/EventSyncService.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVector>

namespace rv1126b {

struct EventViewDependencies {
    IEventRepository* repository = nullptr;
    EvidenceCache* evidenceCache = nullptr;
};

class EventViewController final : public QObject
{
    Q_OBJECT

public:
    static constexpr int HistoryPageSize = 100;

    explicit EventViewController(EventViewDependencies dependencies,
                                 QObject* parent = nullptr);
    ~EventViewController() override;

    bool servicesAvailable() const;
    bool isPaused() const;
    int pendingChangeCount() const;
    void attachSyncService(EventSyncService* service);
    void setDeviceSession(const DeviceSessionSnapshot& snapshot);
    void refreshRealtime(const QString& deviceId, bool allDevices, int limit);
    void queryHistory(const EventQuery& query);
    void setPaused(bool paused);
    void setSyncEnabled(bool enabled);
    bool syncEnabled() const;
    void requestEvidence(const VehicleEvent& event);
    void deleteLocalEvent(const VehicleEvent& event);
    void clearLocalHistory(const EventQuery& query);
    void exportHistory(const EventQuery& query, const QString& filePath);
    void stopDevice(const QString& deviceId);
    void shutdown();

signals:
    void eventsReset(const QVector<rv1126b::VehicleEvent>& events);
    void eventUpserted(const rv1126b::VehicleEvent& event);
    void eventDeleted(const rv1126b::EventIdentity& identity);
    void evidenceChanged(const rv1126b::EvidenceCacheEntry& entry);
    void pendingChangeCountChanged(int count);
    void queryFinished(int rowCount);
    void deleteFinished(int deletedCount, int failedCount);
    void exportFinished(const QString& filePath, int rowCount);
    void userError(const QString& code, const QString& message);

private:
    void handleEventChanged(const EventIdentity& identity);
    void loadChangedEvent(const EventIdentity& identity);
    void startClearBatch();
    void deleteNextClearEvent();
    void finishClear();
    void startExportPage();
    void finishExport();
    bool matchesCurrentQuery(const VehicleEvent& event) const;
    bool isDeviceOnline(const QString& deviceId) const;
    void reportError(const ApiError& error, const QString& fallback);
    void cancelRequestsForDevice(const QString& deviceId);

    EventViewDependencies dependencies_;
    QHash<QString, QPointer<EventSyncService>> syncServices_;
    QHash<QString, DeviceSessionState> sessionStates_;
    QHash<RequestId, QString> activeRequests_;
    EventQuery currentQuery_;
    bool realtimeMode_ = true;
    bool paused_ = false;
    bool shutdown_ = false;
    bool syncEnabled_ = true;
    int pendingChangeCount_ = 0;
    quint64 queryGeneration_ = 0;
    RequestId activeListRequest_;

    bool clearRunning_ = false;
    EventQuery clearQuery_;
    QVector<VehicleEvent> clearQueue_;
    int clearDeletedCount_ = 0;
    int clearFailedCount_ = 0;
    int clearSkippedCount_ = 0;
    QVector<QPointer<EventSyncService>> clearPausedServices_;

    bool exportRunning_ = false;
    EventQuery exportQuery_;
    QVector<VehicleEvent> exportRows_;
    QString exportPath_;
};

} // namespace rv1126b
