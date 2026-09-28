#include "../src/rv1126b/services/BoardDataPullService.h"
#include "../src/rv1126b/storage/SqliteEventRepository.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <utility>

using namespace rv1126b;

namespace {

VehicleEvent makeEvent(const QString& deviceId, qint64 eventId, qint64 trackId)
{
    VehicleEvent event;
    event.identity.deviceId = deviceId;
    event.identity.eventId = eventId;
    event.identity.trackId = trackId;
    event.eventTime.epochMs = 1758998400000LL + eventId * 1000;
    event.plateText = QStringLiteral("京A%1").arg(eventId);
    event.plateColor = QStringLiteral("blue");
    event.ocrStatus.value = OcrStatus::Matched;
    event.ocrStatus.rawValue = QStringLiteral("matched");
    event.motionDirection = QStringLiteral("down");
    event.speedKmh = 40 + static_cast<int>(eventId);
    event.speedValid = true;
    event.speedStatus = QStringLiteral("matched");
    return event;
}

EventSummaryDto summaryFrom(const VehicleEvent& event)
{
    EventSummaryDto summary;
    summary.eventId = event.identity.eventId;
    summary.trackId = event.identity.trackId;
    summary.eventTime = event.eventTime;
    summary.plateText = event.plateText;
    summary.plateColor = event.plateColor;
    summary.ocrStatus = event.ocrStatus;
    summary.motionDirection = event.motionDirection;
    summary.speedKmh = event.speedKmh;
    summary.speedValid = event.speedValid;
    summary.speedStatus = event.speedStatus;
    return summary;
}

// 只实现拉取用到的 listEvents：两页，第一页 has_more。
class FakePullApi final : public IBoardApiClient
{
public:
    QVector<EventSummaryDto> firstPage;
    QVector<EventSummaryDto> secondPage;
    int listCalls = 0;

    RequestId getHealth(QObject*, ApiCompletion<HealthDto>) override { return RequestId::createUuid(); }
    RequestId listEvents(int, const std::optional<QString>& cursor, QObject*, ApiCompletion<EventPageDto> c) override
    {
        ++listCalls;
        EventPageDto page;
        page.apiVersion = QStringLiteral("v1");
        if (!cursor.has_value()) {
            page.items = firstPage;
            page.hasMore = true;
            page.nextCursor = QStringLiteral("cursor-2");
        } else {
            page.items = secondPage;
            page.hasMore = false;
        }
        page.count = page.items.size();
        const ApiResult<EventPageDto> result = ApiResult<EventPageDto>::success(page);
        c(result);
        return RequestId::createUuid();
    }
    RequestId getEventDetail(const EventIdentity&, QObject*, ApiCompletion<EventDetailDto> c) override
    {
        c(ApiResult<EventDetailDto>::failure(ApiError{}));
        return RequestId::createUuid();
    }
    RequestId downloadEvidenceToPartFile(const EventIdentity&, const QString&, const QString&, QObject*,
                                         ApiCompletion<EvidenceDownloadResult> c) override
    {
        c(ApiResult<EvidenceDownloadResult>::failure(ApiError{}));
        return RequestId::createUuid();
    }
    RequestId getEvidenceConfig(QObject*, ApiCompletion<EvidenceConfigDto> c) override
    {
        c(ApiResult<EvidenceConfigDto>::failure(ApiError{}));
        return RequestId::createUuid();
    }
    RequestId putEvidenceConfig(const EvidenceConfigUpdate&, QObject*, ApiCompletion<EvidenceConfigDto> c) override
    {
        c(ApiResult<EvidenceConfigDto>::failure(ApiError{}));
        return RequestId::createUuid();
    }
    RequestId getTime(QObject*, ApiCompletion<TimeStatusDto> c) override
    {
        c(ApiResult<TimeStatusDto>::failure(ApiError{}));
        return RequestId::createUuid();
    }
    RequestId putTime(const TimeUpdate&, QObject*, ApiCompletion<TimeStatusDto> c) override
    {
        c(ApiResult<TimeStatusDto>::failure(ApiError{}));
        return RequestId::createUuid();
    }
    RequestId getFtpConfig(QObject*, ApiCompletion<FtpConfigSnapshotDto> c) override
    {
        c(ApiResult<FtpConfigSnapshotDto>::failure(ApiError{}));
        return RequestId::createUuid();
    }
    RequestId putFtpConfig(const FtpConfigUpdate&, QObject*, ApiCompletion<FtpConfigSnapshotDto> c) override
    {
        c(ApiResult<FtpConfigSnapshotDto>::failure(ApiError{}));
        return RequestId::createUuid();
    }
    RequestId rollbackFtpConfig(const QString&, QObject*, ApiCompletion<FtpConfigSnapshotDto> c) override
    {
        c(ApiResult<FtpConfigSnapshotDto>::failure(ApiError{}));
        return RequestId::createUuid();
    }
    RequestId getFtpControl(QObject*, ApiCompletion<FtpControlDto> c) override
    {
        c(ApiResult<FtpControlDto>::failure(ApiError{}));
        return RequestId::createUuid();
    }
    RequestId putFtpControl(const FtpControlUpdate&, QObject*, ApiCompletion<FtpControlDto> c) override
    {
        c(ApiResult<FtpControlDto>::failure(ApiError{}));
        return RequestId::createUuid();
    }
    RequestId createFtpTask(const FtpTaskCreate&, QObject*, ApiCompletion<FtpTaskDetailDto> c) override
    {
        c(ApiResult<FtpTaskDetailDto>::failure(ApiError{}));
        return RequestId::createUuid();
    }
    RequestId listFtpTasks(int, const std::optional<QString>&, QObject*, ApiCompletion<FtpTaskPageDto> c) override
    {
        c(ApiResult<FtpTaskPageDto>::failure(ApiError{}));
        return RequestId::createUuid();
    }
    RequestId getFtpTask(const QString&, QObject*, ApiCompletion<FtpTaskDetailDto> c) override
    {
        c(ApiResult<FtpTaskDetailDto>::failure(ApiError{}));
        return RequestId::createUuid();
    }
    RequestId retryFtpTask(const QString&, QObject*, ApiCompletion<FtpTaskDetailDto> c) override
    {
        c(ApiResult<FtpTaskDetailDto>::failure(ApiError{}));
        return RequestId::createUuid();
    }
    void cancel(const RequestId&) override {}
    void cancelAll() override {}
};

} // namespace

class BoardDataPullServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void pagesThroughBoardAndWritesEveryEvent();
};

void BoardDataPullServiceTest::pagesThroughBoardAndWritesEveryEvent()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    SqliteEventRepository repository(QDir(temp.path()).filePath(QStringLiteral("events.sqlite")));
    bool initialized = false;
    repository.initialize(this, [&initialized](ApiResult<void> result) { initialized = result.isSuccess(); });
    QVERIFY(initialized);

    const QString target = QDir(temp.path()).filePath(QStringLiteral("拉取结果"));
    DetectionResultWriter writer(&repository);
    writer.setTargetRoot(target);

    const VehicleEvent first = makeEvent(QStringLiteral("rv1126b_001"), 11, 1);
    const VehicleEvent second = makeEvent(QStringLiteral("rv1126b_001"), 12, 2);
    const VehicleEvent third = makeEvent(QStringLiteral("rv1126b_001"), 13, 3);
    // 详情缺失也要能写出"图 + 摘要"（真实链路里 OCR 未完成就是这样）。
    for (const VehicleEvent& event : {first, second, third}) {
        bool upserted = false;
        repository.upsertEvents({event}, this, [&upserted](ApiResult<void> r) { upserted = r.isSuccess(); });
        QVERIFY(upserted);
    }

    FakePullApi api;
    api.firstPage = {summaryFrom(first), summaryFrom(second)};
    api.secondPage = {summaryFrom(third)};

    BoardDataPullService service(&repository, &writer);
    int finishedSeen = -1;
    int finishedWritten = -1;
    int finishedFailed = -1;
    connect(&service, &BoardDataPullService::finished, this,
            [&](int seen, int written, int failed, bool) {
                finishedSeen = seen;
                finishedWritten = written;
                finishedFailed = failed;
            });

    service.start(&api, QStringLiteral("rv1126b_001"), 100);

    QTRY_COMPARE(finishedSeen, 3);      // 两页共 3 条都读到了
    QCOMPARE(finishedWritten, 3);       // 3 条都写出了资料包
    QCOMPARE(finishedFailed, 0);
    QCOMPARE(api.listCalls, 2);         // 确实翻了页
    QVERIFY(!service.isRunning());

    // records.csv：表头 + 3 行
    QFile csv(QDir(target).filePath(QStringLiteral("records.csv")));
    QVERIFY(csv.open(QIODevice::ReadOnly));
    const QStringList lines = QString::fromUtf8(csv.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(lines.size(), 4);

    // 三个事件目录都建出来了
    const QString deviceDir = QDir(target).filePath(QStringLiteral("rv1126b_001"));
    const QStringList days = QDir(deviceDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QCOMPARE(days.size(), 1);
    const QStringList eventDirs = QDir(QDir(deviceDir).filePath(days.first()))
                                      .entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QCOMPARE(eventDirs.size(), 3);
}

QTEST_MAIN(BoardDataPullServiceTest)
#include "BoardDataPullServiceTest.moc"
