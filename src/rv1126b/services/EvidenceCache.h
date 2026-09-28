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
    // 证据图真正落盘成功。带上事件本身和最终文件路径，供"把检测结果写进文件夹"使用
    // （stateChanged 只有缓存条目，拿不到车牌/速度这些结果字段）。
    void evidenceStored(const rv1126b::VehicleEvent& event,
                        const rv1126b::EvidenceCacheEntry& entry);
};

} // namespace rv1126b
