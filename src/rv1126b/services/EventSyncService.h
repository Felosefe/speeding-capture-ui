#pragma once

#include "../domain/Models.h"

#include <QObject>
#include <QString>

#include <array>

namespace rv1126b {

class EventSyncService : public QObject
{
    Q_OBJECT

public:
    static constexpr int DefaultPollIntervalMs = 1000;
    static constexpr int MinimumPollIntervalMs = 500;
    static constexpr int MaximumPollIntervalMs = 60000;
    static constexpr int DefaultPageSize = 50;
    inline static constexpr std::array<int, 6> RetryBackoffSeconds {1, 2, 4, 8, 16, 30};

    explicit EventSyncService(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    ~EventSyncService() override = default;

    virtual QString deviceId() const = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
    virtual void setPollIntervalMs(int intervalMs) = 0;
    virtual int pollIntervalMs() const = 0;
    virtual void pollNow() = 0;
    virtual void syncAllExisting() = 0;
    /*
     * One-shot backfill limited to a time range: walk pages from the newest event
     * backwards and stop as soon as an event older than cutoffEpochMs is reached.
     * 0 means "no cutoff", i.e. everything - what syncAllExisting() does.
     *
     * This is cheap by construction: the board returns events newest-first, so the
     * walk stops at the boundary instead of paging through the whole history.  The
     * stored sync anchor still ends up at the current head, so the normal 1 Hz poll
     * resumes incrementally and never re-scans the range.
     *
     * Used by the event-sync settings page: "today / last week / last month / all".
     */
    virtual void syncRange(qint64 cutoffEpochMs) = 0;
    virtual void markCurrentHeadAsSynced() = 0;

signals:
    void eventChanged(const rv1126b::EventIdentity& identity);
    void initialCatchUpFinished(const QString& deviceId);
    void syncError(const QString& deviceId, const rv1126b::ApiError& error);
    void syncActionFinished(const QString& deviceId, const QString& action, const QString& message);
};

} // namespace rv1126b
