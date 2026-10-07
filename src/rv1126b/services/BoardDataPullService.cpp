#include "BoardDataPullService.h"

#include <QPointer>

#include <utility>

namespace rv1126b {
namespace {

// 板端列表项 → 本地事件行（与 BoardEventSyncService::eventFromSummary 同一套字段映射）。
VehicleEvent eventFromSummary(const QString& deviceId, const EventSummaryDto& summary)
{
    VehicleEvent event;
    event.identity.deviceId = deviceId;
    event.identity.eventId = summary.eventId;
    event.identity.trackId = summary.trackId;
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
    event.evidenceRelativeUrl = summary.evidenceRelativeUrl;
    event.evidenceAvailable = summary.evidenceAvailable || !summary.evidenceRelativeUrl.isEmpty();
    event.captureStatus = summary.captureStatus;
    event.captureError = summary.captureError;
    event.detailRelativeUrl = summary.detailRelativeUrl;
    return event;
}

} // namespace

BoardDataPullService::BoardDataPullService(IEventRepository* repository,
                                           DetectionResultWriter* writer,
                                           QObject* parent)
    : QObject(parent)
    , repository_(repository)
    , writer_(writer)
{
}

void BoardDataPullService::start(IBoardApiClient* apiClient, const QString& deviceId, int limit,
                                qint64 cutoffEpochMs)
{
    if (running_ || !repository_ || !writer_ || !apiClient || deviceId.trimmed().isEmpty()) {
        return;
    }
    apiClient_ = apiClient;
    deviceId_ = deviceId;
    limit_ = limit > 0 ? limit : 5000;
    // 每次拉取都重置时间下界，避免上一次的范围影响这一次。
    cutoffEpochMs_ = cutoffEpochMs > 0 ? cutoffEpochMs : 0;
    cursor_.clear();
    hasCursor_ = false;
    seenCount_ = 0;
    writtenCount_ = 0;
    failedCount_ = 0;
    pending_.clear();
    cancelled_ = false;
    running_ = true;

    QString errorMessage;
    if (!writer_->ensureTargetRoot(&errorMessage)) {
        running_ = false;
        emit failed(errorMessage);
        return;
    }
    requestNextPage();
}

void BoardDataPullService::cancel()
{
    if (!running_) {
        return;
    }
    cancelled_ = true;
    if (apiClient_) {
        apiClient_->cancelAll();
    }
    running_ = false;
    emit finished(seenCount_, writtenCount_, failedCount_, true);
}

void BoardDataPullService::requestNextPage()
{
    if (!running_ || !apiClient_) {
        return;
    }
    const std::optional<QString> cursor = hasCursor_ ? std::optional<QString>(cursor_) : std::nullopt;
    QPointer<BoardDataPullService> self(this);
    apiClient_->listEvents(PageSize, cursor, this, [this, self](ApiResult<EventPageDto> result) {
        if (!self) return;
        handlePage(std::move(result));
    });
}

void BoardDataPullService::handlePage(ApiResult<EventPageDto> result)
{
    if (!running_) {
        return;
    }
    if (!result.isSuccess()) {
        running_ = false;
        emit failed(QStringLiteral("读取板端事件列表失败：%1").arg(result.error().message));
        emit finished(seenCount_, writtenCount_, failedCount_, cancelled_);
        return;
    }

    const EventPageDto page = result.value();
    QVector<VehicleEvent> batch;
    batch.reserve(page.items.size());
    bool reachedCutoff = false;
    for (const EventSummaryDto& item : page.items) {
        /*
         * 时间范围（2026-10-07）：板端按时间降序返回，所以遇到第一条早于截止时间的
         * 事件就结束本次拉取。判断放在入队之前——范围外的事件既不落库、也不写资料包。
         */
        if (cutoffEpochMs_ > 0 && item.eventTime.sourceEpochMs < cutoffEpochMs_) {
            reachedCutoff = true;
            break;
        }
        batch.append(eventFromSummary(deviceId_, item));
        pending_.append(EventIdentity{deviceId_, item.eventId, item.trackId});
    }
    /* 用 batch.size() 而不是 page.items.size()：被范围挡掉的不算"已拉取"。 */
    seenCount_ += batch.size();
    const bool hasMore = page.hasMore && page.nextCursor.has_value() && seenCount_ < limit_ &&
                         !reachedCutoff;
    const QString nextCursor = page.nextCursor.value_or(QString());
    emit progress(seenCount_, writtenCount_, failedCount_);

    // 先落库再写文件夹：中途取消也不会丢已经拉回来的事件。
    QPointer<BoardDataPullService> self(this);
    repository_->upsertEvents(batch, this, [this, self, hasMore, nextCursor](ApiResult<void> upsertResult) {
        if (!self || !running_) return;
        if (!upsertResult.isSuccess()) {
            running_ = false;
            emit failed(QStringLiteral("写入本地数据库失败：%1").arg(upsertResult.error().message));
            emit finished(seenCount_, writtenCount_, failedCount_, cancelled_);
            return;
        }
        if (hasMore && !nextCursor.isEmpty()) {
            cursor_ = nextCursor;
            hasCursor_ = true;
            requestNextPage();
            return;
        }
        hasCursor_ = false;
        cursor_.clear();
        QMetaObject::invokeMethod(this, [this]() { processNextEvent(); }, Qt::QueuedConnection);  // queued: the repository calls back synchronously, direct calls would recurse per event
    });
}

void BoardDataPullService::processNextEvent()
{
    if (!running_) {
        return;
    }
    if (pending_.isEmpty()) {
        running_ = false;
        emit finished(seenCount_, writtenCount_, failedCount_, cancelled_);
        return;
    }

    const EventIdentity identity = pending_.takeFirst();
    QPointer<BoardDataPullService> self(this);
    repository_->loadEvent(
        identity, this,
        [this, self](ApiResult<std::optional<VehicleEvent>> result) {
            if (!self || !running_) return;
            if (!result.isSuccess() || !result.value().has_value()) {
                failedCount_++;
                emit progress(seenCount_, writtenCount_, failedCount_);
                QMetaObject::invokeMethod(this, [this]() { processNextEvent(); }, Qt::QueuedConnection);  // queued: the repository calls back synchronously, direct calls would recurse per event
                return;
            }
            const VehicleEvent event = *result.value();
            // 证据图这里不下载：一键拉取只保证"能看的资料包"（详情 JSON + 中文摘要 + 总表），
            // 图片由后台自动同步按需补齐，避免一次点击卡在几百张图上。
            bool wrote = false;
            writer_->writeBundleNow(event, QString(), true, &wrote);
            if (wrote) {
                ++writtenCount_;
            } else {
                ++failedCount_;
            }
            emit progress(seenCount_, writtenCount_, failedCount_);
            QMetaObject::invokeMethod(this, [this]() { processNextEvent(); }, Qt::QueuedConnection);  // queued: the repository calls back synchronously, direct calls would recurse per event
        });
}

} // namespace rv1126b
