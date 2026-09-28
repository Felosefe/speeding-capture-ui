#pragma once

#include "../domain/Models.h"

#include <QObject>
#include <QString>

namespace rv1126b {

class EvidenceCache : public QObject
{
    Q_OBJECT

public:
    static constexpr int MaxConcurrentDownloads = 4;
    static constexpr int MaxConcurrentDownloadsPerDevice = 1;

    explicit EvidenceCache(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    ~EvidenceCache() override = default;

    virtual void enqueue(const VehicleEvent& event) = 0;
    virtual RequestId removeLocal(
        const VehicleEvent& event,
        QObject* context,
        ApiCompletion<void> completion) = 0;
    virtual void cancel(const EventIdentity& identity) = 0;
    virtual void cancelDevice(const QString& deviceId) = 0;
    virtual void cancelAll() = 0;
    virtual QString finalPathFor(const VehicleEvent& event) const = 0;

signals:
    void stateChanged(const rv1126b::EvidenceCacheEntry& entry);
    void cacheError(const rv1126b::EventIdentity& identity, const rv1126b::ApiError& error);
};

} // namespace rv1126b
