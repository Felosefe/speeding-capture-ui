#include "BoardEventSyncService.h"

#include <QDateTime>

#include <algorithm>
#include <optional>

namespace rv1126b {
namespace {

qint64 nowEpochMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

bool isEmptyIdentity(const EventIdentity& identity)
{
    return identity.deviceId.isEmpty() && identity.eventId == 0 && identity.trackId == 0;
}

EventSortKey sortKeyFromSummary(const EventSummaryDto& summary)
{
    EventSortKey key;
    key.sourceEpochMs = summary.eventTime.sourceEpochMs;
    key.eventId = summary.eventId;
    key.trackId = summary.trackId;
    return key;
}

EventSortKey sortKeyFromEvent(const VehicleEvent& event)
{
    EventSortKey key;
    key.sourceEpochMs = event.eventTime.sourceEpochMs;
    key.eventId = event.identity.eventId;
    key.trackId = event.identity.trackId;
    return key;
}

VehicleEvent eventFromSummary(
    const QString& deviceId,
    const EventSummaryDto& summary,
    qint64 observedEpochMs,
    const std::optional<EventIdentity>& fallbackIdentity = std::nullopt)
{
    VehicleEvent event;
    event.identity.deviceId = deviceId;
    event.identity.eventId = summary.eventId;
    event.identity.trackId = summary.trackId;
    if (fallbackIdentity && summary.eventId == 0 && summary.trackId == 0) {
        event.identity = *fallbackIdentity;
    }
    event.eventTime = summary.eventTime;
    event.motionDirection = summary.motionDirection;
    event.speedKmh = summary.speedKmh;
    event.speedValid = summary.speedValid;
    event.speedStatus = summary.speedStatus;
    event.ocrStatus = summary.ocrStatus;
    event.plateText = summary.plateText;
    event.plateAscii = summary.plateAscii;
    event.plateColor = summary.plateColor;
    event.evidenceStatus = summary.evidenceStatus;
    event.evidenceAvailable = summary.evidenceAvailable;
    event.detailRelativeUrl = summary.detailRelativeUrl;
    event.evidenceRelativeUrl = summary.evidenceRelativeUrl;
    event.firstSeenEpochMs = observedEpochMs;
    event.lastUpdatedEpochMs = observedEpochMs;
    return event;
}

EventDetailSnapshot detailFromDto(
    const QString& deviceId,
    const EventIdentity& identity,
    const EventDetailDto& dto,
    qint64 fetchedEpochMs)
{
    EventDetailSnapshot detail;
    detail.identity = identity;
    if (dto.summary.eventId != 0 || dto.summary.trackId != 0) {
        detail.identity.deviceId = deviceId;
        detail.identity.eventId = dto.summary.eventId;
        detail.identity.trackId = dto.summary.trackId;
    }
    detail.triggerMode = dto.triggerMode;
    detail.captureReason = dto.captureReason;
    detail.vehicle = dto.vehicle;
    detail.lineRegion = dto.lineRegion;
    detail.radar = dto.radar;
    detail.ocr = dto.ocr;
    detail.images = dto.images;
    detail.rawJson = dto.rawJson;
    detail.fetchedEpochMs = fetchedEpochMs;
    return detail;
}

} // namespace

BoardEventSyncService::BoardEventSyncService(
    QString deviceId,
    IBoardApiClient* apiClient,
    IEventRepository* repository,
    QObject* parent)
    : EventSyncService(parent)
    , deviceId_(std::move(deviceId))
    , apiClient_(apiClient)
    , repository_(repository)
{
    pollTimer_.setSingleShot(true);
    connect(&pollTimer_, &QTimer::timeout, this, &BoardEventSyncService::beginPoll);
}

QString BoardEventSyncService::deviceId() const
{
    return deviceId_;
}

void BoardEventSyncService::start()
{
    if (running_) {
        return;
    }

    running_ = true;
    initialCatchUpPending_ = true;
    retryAttempt_ = 0;
    ++generation_;
    scheduleNextPoll(0);
}

void BoardEventSyncService::stop()
{
    if (!running_ && !syncInFlight_) {
        return;
    }

    running_ = false;
    syncInFlight_ = false;
    pendingDetailRefresh_.clear();
    loadedAnchor_.reset();
    cycleHead_.reset();
    pollTimer_.stop();
    ++generation_;

    if (apiClient_) {
        apiClient_->cancelAll();
    }
    if (repository_) {
        repository_->cancelAll();
    }
}

bool BoardEventSyncService::isRunning() const
{
    return running_;
}

void BoardEventSyncService::setPollIntervalMs(int intervalMs)
{
    pollIntervalMs_ = std::clamp(intervalMs, MinimumPollIntervalMs, MaximumPollIntervalMs);
}

int BoardEventSyncService::pollIntervalMs() const
{
    return pollIntervalMs_;
}

void BoardEventSyncService::pollNow()
{
    if (!running_ || syncInFlight_) {
        return;
    }

    pollTimer_.stop();
    scheduleNextPoll(0);
}

void BoardEventSyncService::beginPoll()
{
    if (!running_ || syncInFlight_) {
        return;
    }

    const int generation = generation_;
    if (deviceId_.isEmpty() || !apiClient_ || !repository_) {
        failCycle(generation, serviceError(
            QStringLiteral("rv1126b.sync.invalid_dependencies"),
            QStringLiteral("Event sync service is missing device id, API client or repository.")));
        return;
    }

    syncInFlight_ = true;
    loadedAnchor_.reset();
    cycleHead_.reset();
    repository_->loadSyncAnchor(
        deviceId_,
        this,
        [this, generation](ApiResult<std::optional<SyncAnchor>> result) {
            handleSyncAnchorLoaded(generation, std::move(result));
        });
}

void BoardEventSyncService::handleSyncAnchorLoaded(
    int generation,
    ApiResult<std::optional<SyncAnchor>> result)
{
    if (!isActiveGeneration(generation)) {
        return;
    }

    if (!result.isSuccess()) {
        failCycle(generation, result.error());
        return;
    }

    loadedAnchor_ = result.value();
    requestEventPage(generation, std::nullopt);
}

void BoardEventSyncService::requestEventPage(int generation, const std::optional<QString>& cursor)
{
    if (!isActiveGeneration(generation)) {
        return;
    }

    apiClient_->listEvents(
        DefaultPageSize,
        cursor,
        this,
        [this, generation](ApiResult<EventPageDto> result) {
            handleEventPage(generation, std::move(result));
        });
}

void BoardEventSyncService::handleEventPage(int generation, ApiResult<EventPageDto> result)
{
    if (!isActiveGeneration(generation)) {
        return;
    }

    if (!result.isSuccess()) {
        failCycle(generation, result.error());
        return;
    }

    const EventPageDto page = result.value();
    const qint64 observedEpochMs = nowEpochMs();
    QVector<VehicleEvent> events;
    events.reserve(page.items.size());

    bool reachedStoredAnchor = false;
    for (const EventSummaryDto& summary : page.items) {
        VehicleEvent event = eventFromSummary(deviceId_, summary, observedEpochMs);
        if (!cycleHead_ && !isEmptyIdentity(event.identity)) {
            cycleHead_ = sortKeyFromEvent(event);
        }
        events.append(std::move(event));
        if (loadedAnchor_ && sortKeyFromSummary(summary) == loadedAnchor_->previousHead) {
            reachedStoredAnchor = true;
            break;
        }
    }

    const bool shouldContinuePaging = page.hasMore && page.nextCursor.has_value() && !reachedStoredAnchor;
    const std::optional<QString> nextCursor = shouldContinuePaging ? page.nextCursor : std::nullopt;

    repository_->upsertEvents(
        events,
        this,
        [this, generation, events, shouldContinuePaging, nextCursor](ApiResult<void> saveResult) {
            handlePagePersisted(
                generation,
                events,
                shouldContinuePaging,
                nextCursor,
                std::move(saveResult));
        });
}

void BoardEventSyncService::handlePagePersisted(
    int generation,
    const QVector<VehicleEvent>& events,
    bool shouldContinuePaging,
    std::optional<QString> nextCursor,
    ApiResult<void> result)
{
    if (!isActiveGeneration(generation)) {
        return;
    }

    if (!result.isSuccess()) {
        failCycle(generation, result.error());
        return;
    }

    for (const VehicleEvent& event : events) {
        emit eventChanged(event.identity);
    }

    if (shouldContinuePaging) {
        requestEventPage(generation, nextCursor);
        return;
    }

    saveCycleAnchorOrRefreshDetails(generation);
}

void BoardEventSyncService::saveCycleAnchorOrRefreshDetails(int generation)
{
    if (!isActiveGeneration(generation)) {
        return;
    }

    if (!cycleHead_) {
        repository_->loadNonTerminalEvents(
            deviceId_,
            this,
            [this, generation](ApiResult<QVector<VehicleEvent>> pendingResult) {
                handleNonTerminalEvents(generation, std::move(pendingResult));
            });
        return;
    }

    SyncAnchor anchor;
    anchor.deviceId = deviceId_;
    anchor.previousHead = *cycleHead_;
    anchor.savedEpochMs = nowEpochMs();
    repository_->saveSyncAnchor(
        anchor,
        this,
        [this, generation](ApiResult<void> result) {
            handleSyncAnchorSaved(generation, std::move(result));
        });
}

void BoardEventSyncService::handleSyncAnchorSaved(int generation, ApiResult<void> result)
{
    if (!isActiveGeneration(generation)) {
        return;
    }

    if (!result.isSuccess()) {
        reportRecoverableError(generation, result.error());
    }

    repository_->loadNonTerminalEvents(
        deviceId_,
        this,
        [this, generation](ApiResult<QVector<VehicleEvent>> pendingResult) {
            handleNonTerminalEvents(generation, std::move(pendingResult));
        });
}

void BoardEventSyncService::handleNonTerminalEvents(int generation, ApiResult<QVector<VehicleEvent>> result)
{
    if (!isActiveGeneration(generation)) {
        return;
    }

    if (!result.isSuccess()) {
        reportRecoverableError(generation, result.error());
        completeCycle(generation);
        return;
    }

    pendingDetailRefresh_.clear();
    pendingDetailRefresh_.reserve(result.value().size());
    for (const VehicleEvent& event : result.value()) {
        if (!isEmptyIdentity(event.identity)) {
            pendingDetailRefresh_.append(event.identity);
        }
    }

    refreshNextDetail(generation);
}

void BoardEventSyncService::refreshNextDetail(int generation)
{
    if (!isActiveGeneration(generation)) {
        return;
    }

    if (pendingDetailRefresh_.isEmpty()) {
        completeCycle(generation);
        return;
    }

    const EventIdentity identity = pendingDetailRefresh_.takeFirst();
    apiClient_->getEventDetail(
        identity,
        this,
        [this, generation, identity](ApiResult<EventDetailDto> result) {
            handleEventDetail(generation, identity, std::move(result));
        });
}

void BoardEventSyncService::handleEventDetail(
    int generation,
    const EventIdentity& identity,
    ApiResult<EventDetailDto> result)
{
    if (!isActiveGeneration(generation)) {
        return;
    }

    if (!result.isSuccess()) {
        reportRecoverableError(generation, result.error());
        refreshNextDetail(generation);
        return;
    }

    const qint64 fetchedEpochMs = nowEpochMs();
    const EventDetailDto detailDto = result.value();
    const EventDetailSnapshot detail = detailFromDto(deviceId_, identity, detailDto, fetchedEpochMs);
    VehicleEvent updatedEvent = eventFromSummary(deviceId_, detailDto.summary, fetchedEpochMs, identity);
    if (detailDto.evidenceRelativeUrl) {
        updatedEvent.evidenceRelativeUrl = *detailDto.evidenceRelativeUrl;
    }

    repository_->saveDetail(
        detail,
        this,
        [this, generation, updatedEvent, identity](ApiResult<void> saveResult) {
            handleDetailSaved(generation, updatedEvent, identity, std::move(saveResult));
        });
}

void BoardEventSyncService::handleDetailSaved(
    int generation,
    const VehicleEvent& updatedEvent,
    const EventIdentity& identity,
    ApiResult<void> result)
{
    if (!isActiveGeneration(generation)) {
        return;
    }

    if (!result.isSuccess()) {
        reportRecoverableError(generation, result.error());
        refreshNextDetail(generation);
        return;
    }

    repository_->upsertEvents(
        {updatedEvent},
        this,
        [this, generation, identity](ApiResult<void> persistResult) {
            handleDetailEventPersisted(generation, identity, std::move(persistResult));
        });
}

void BoardEventSyncService::handleDetailEventPersisted(
    int generation,
    const EventIdentity& identity,
    ApiResult<void> result)
{
    if (!isActiveGeneration(generation)) {
        return;
    }

    if (!result.isSuccess()) {
        reportRecoverableError(generation, result.error());
        refreshNextDetail(generation);
        return;
    }

    emit eventChanged(identity);
    refreshNextDetail(generation);
}

void BoardEventSyncService::completeCycle(int generation)
{
    if (!isActiveGeneration(generation)) {
        return;
    }

    syncInFlight_ = false;
    retryAttempt_ = 0;

    if (initialCatchUpPending_) {
        initialCatchUpPending_ = false;
        emit initialCatchUpFinished(deviceId_);
    }

    scheduleNextPoll(pollIntervalMs_);
}

void BoardEventSyncService::failCycle(int generation, const ApiError& error)
{
    if (!isActiveGeneration(generation)) {
        return;
    }

    syncInFlight_ = false;
    emit syncError(deviceId_, error);
    scheduleNextPoll(nextRetryDelayMs());
}

void BoardEventSyncService::reportRecoverableError(int generation, const ApiError& error)
{
    if (isActiveGeneration(generation)) {
        emit syncError(deviceId_, error);
    }
}

void BoardEventSyncService::scheduleNextPoll(int delayMs)
{
    if (!running_) {
        return;
    }

    pollTimer_.start(std::max(0, delayMs));
}

int BoardEventSyncService::nextRetryDelayMs()
{
    const int index = std::min<int>(retryAttempt_, static_cast<int>(RetryBackoffSeconds.size()) - 1);
    ++retryAttempt_;
    return RetryBackoffSeconds[static_cast<size_t>(index)] * 1000;
}

bool BoardEventSyncService::isActiveGeneration(int generation) const
{
    return running_ && generation == generation_;
}

ApiError BoardEventSyncService::serviceError(const QString& code, const QString& message) const
{
    ApiError error;
    error.code = code;
    error.message = message;
    error.category = ApiErrorCategory::Validation;
    error.retryable = false;
    return error;
}

} // namespace rv1126b
