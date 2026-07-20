#pragma once

#include "EventSyncService.h"

#include "../ports/IBoardApiClient.h"
#include "../ports/IEventRepository.h"

#include <QTimer>
#include <QVector>

#include <optional>

namespace rv1126b {

class BoardEventSyncService final : public EventSyncService
{
    Q_OBJECT

public:
    BoardEventSyncService(
        QString deviceId,
        IBoardApiClient* apiClient,
        IEventRepository* repository,
        QObject* parent = nullptr);

    QString deviceId() const override;
    void start() override;
    void stop() override;
    bool isRunning() const override;
    void setPollIntervalMs(int intervalMs) override;
    int pollIntervalMs() const override;

    void pollNow();

private:
    void beginPoll();
    void handleSyncAnchorLoaded(int generation, ApiResult<std::optional<SyncAnchor>> result);
    void requestEventPage(int generation, const std::optional<QString>& cursor);
    void handleEventPage(int generation, ApiResult<EventPageDto> result);
    void handlePagePersisted(
        int generation,
        const QVector<VehicleEvent>& events,
        bool shouldContinuePaging,
        std::optional<QString> nextCursor,
        ApiResult<void> result);
    void saveCycleAnchorOrRefreshDetails(int generation);
    void handleSyncAnchorSaved(int generation, ApiResult<void> result);
    void handleNonTerminalEvents(int generation, ApiResult<QVector<VehicleEvent>> result);
    void refreshNextDetail(int generation);
    void handleEventDetail(int generation, const EventIdentity& identity, ApiResult<EventDetailDto> result);
    void handleDetailSaved(
        int generation,
        const VehicleEvent& updatedEvent,
        const EventIdentity& identity,
        ApiResult<void> result);
    void handleDetailEventPersisted(int generation, const EventIdentity& identity, ApiResult<void> result);
    void completeCycle(int generation);
    void failCycle(int generation, const ApiError& error);
    void reportRecoverableError(int generation, const ApiError& error);
    void scheduleNextPoll(int delayMs);
    int nextRetryDelayMs();
    bool isActiveGeneration(int generation) const;
    ApiError serviceError(const QString& code, const QString& message) const;

    QString deviceId_;
    IBoardApiClient* apiClient_ = nullptr;
    IEventRepository* repository_ = nullptr;
    QTimer pollTimer_;
    int pollIntervalMs_ = DefaultPollIntervalMs;
    int retryAttempt_ = 0;
    int generation_ = 0;
    bool running_ = false;
    bool syncInFlight_ = false;
    bool initialCatchUpPending_ = true;
    std::optional<SyncAnchor> loadedAnchor_;
    std::optional<EventSortKey> cycleHead_;
    QVector<EventIdentity> pendingDetailRefresh_;
};

} // namespace rv1126b
