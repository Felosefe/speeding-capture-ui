#pragma once

#include "../domain/Models.h"
#include "../ports/IBoardApiClient.h"
#include "../ports/IEventRepository.h"
#include "DetectionResultWriter.h"

#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QVector>

namespace rv1126b {

// 「一键把板端已有数据拉回本机某个文件夹」。
//
// 用户需求（2026-09-28）：局域网内一键把板端的检测数据拉回指定电脑的某个文件夹，
// 文件夹不存在就新建。本机是唯一目标（用户已确认），所以不需要对端装任何东西。
//
// 做法：按板端 cursor 分页把事件读回来 → 写本地库 → 顺带把证据图落到本机缓存目录
// （走既有 EvidenceCache，重复的不会重下）→ 每拿到一条就按「资料包」格式写进目标文件夹。
//
// 与后台每秒轮询的 EventSyncService 不冲突：那个只拉新事件并写库；这个只在用户点按钮时
// 跑一次，且自己翻页，因此不需要动同步锚点。
class BoardDataPullService final : public QObject
{
    Q_OBJECT

public:
    static constexpr int PageSize = 100;

    BoardDataPullService(IEventRepository* repository,
                         DetectionResultWriter* writer,
                         QObject* parent = nullptr);

    bool isRunning() const { return running_; }
    // apiClient 由调用方按设备解析后传入（应用里是 boardApiForDevice(deviceId)）。
    // limit 是本次最多拉多少条，避免误点一下把板端翻个底朝天。
    // cutoffEpochMs 是时间范围（2026-10-07 加）：只拉 source_epoch_ms >= 它的数据，
    // 0 = 不限时间（全部）。板端按时间降序返回，所以翻到第一条更早的即可停。
    void start(IBoardApiClient* apiClient, const QString& deviceId, int limit = 5000,
               qint64 cutoffEpochMs = 0);
    void cancel();

    int seenCount() const { return seenCount_; }
    int writtenCount() const { return writtenCount_; }
    int failedCount() const { return failedCount_; }

signals:
    void progress(int seen, int written, int failed);
    void finished(int seen, int written, int failed, bool cancelled);
    void failed(const QString& message);

private:
    void requestNextPage();
    void handlePage(ApiResult<EventPageDto> result);
    void processNextEvent();
    void handleEventLoaded(ApiResult<std::optional<VehicleEvent>> result);

    IEventRepository* repository_ = nullptr;
    DetectionResultWriter* writer_ = nullptr;
    IBoardApiClient* apiClient_ = nullptr;
    QString deviceId_;
    QString cursor_;
    bool hasCursor_ = false;
    int limit_ = 5000;
    // 本次拉取的时间下界（source epoch ms），0 = 不限。
    qint64 cutoffEpochMs_ = 0;
    int seenCount_ = 0;
    int writtenCount_ = 0;
    int failedCount_ = 0;
    QVector<EventIdentity> pending_;
    bool running_ = false;
    bool cancelled_ = false;
};

} // namespace rv1126b
