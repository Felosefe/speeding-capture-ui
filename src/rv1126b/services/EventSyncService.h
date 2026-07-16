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

signals:
    void eventChanged(const rv1126b::EventIdentity& identity);
    void initialCatchUpFinished(const QString& deviceId);
    void syncError(const QString& deviceId, const rv1126b::ApiError& error);
};

} // namespace rv1126b
