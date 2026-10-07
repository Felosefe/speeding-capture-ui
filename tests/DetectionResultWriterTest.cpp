#include "../src/rv1126b/services/DetectionResultWriter.h"
#include "../src/rv1126b/storage/SqliteEventRepository.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace rv1126b;

namespace {

VehicleEvent makeEvent(const QString& deviceId, qint64 eventId, qint64 trackId)
{
    VehicleEvent event;
    event.identity.deviceId = deviceId;
    event.identity.eventId = eventId;
    event.identity.trackId = trackId;
    event.eventTime.epochMs = 1758998400000LL;  // 2026-09-28 前后，只用于目录命名
    event.eventTime.quality.value = TimeQuality::BoardEpochUnverified;
    event.motionDirection = QStringLiteral("up");
    event.speedKmh = 37;
    event.speedValid = true;
    event.speedStatus = QStringLiteral("matched");
    event.plateText = QStringLiteral("京A12345");
    event.plateColor = QStringLiteral("blue");
    event.ocrStatus.rawValue = QStringLiteral("matched");
    event.ocrStatus.value = OcrStatus::Matched;
    return event;
}

EventDetailSnapshot makeDetail(const EventIdentity& identity)
{
    EventDetailSnapshot detail;
    detail.identity = identity;
    detail.rawJson = QJsonObject{{QStringLiteral("note"), QStringLiteral("detail")}};
    detail.ocr = QJsonObject{{QStringLiteral("plate_text"), QStringLiteral("京A12345")}};
    detail.rawJson.insert(QStringLiteral("formal"),
                          QJsonObject{{QStringLiteral("event_id"), identity.eventId}});
    return detail;
}

} // namespace

class DetectionResultWriterTest final : public QObject
{
    Q_OBJECT

private slots:
    void writesReadableBundleAndRecordRow();
    void autoModeRespectsSwitchAndDeduplicates();
    void createsMissingTargetFolder();
};

void DetectionResultWriterTest::writesReadableBundleAndRecordRow()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString databasePath = QDir(temp.path()).filePath(QStringLiteral("events.sqlite"));
    SqliteEventRepository repository(databasePath);

    bool initialized = false;
    repository.initialize(this, [&initialized](ApiResult<void> result) { initialized = result.isSuccess(); });
    QVERIFY(initialized);

    const VehicleEvent event = makeEvent(QStringLiteral("rv1126b_001"), 42, 7);
    // 详情表以外键指向事件表，先写事件行（真实链路里由事件同步先写）。
    bool upserted = false;
    repository.upsertEvents({event}, this,
                            [&upserted](ApiResult<void> result) { upserted = result.isSuccess(); });
    QVERIFY(upserted);

    EventDetailSnapshot detail = makeDetail(event.identity);
    detail.triggerMode = QStringLiteral("dual_line");
    detail.captureReason = QStringLiteral("test");
    bool saved = false;
    QString saveError;
    repository.saveDetail(detail, this, [&saved, &saveError](ApiResult<void> result) {
        saved = result.isSuccess();
        if (!saved) saveError = result.error().code + QStringLiteral(": ") + result.error().message;
    });
    QVERIFY2(saved, qPrintable(saveError));

    const QString target = QDir(temp.path()).filePath(QStringLiteral("检测结果"));
    DetectionResultWriter writer(&repository);
    writer.setTargetRoot(target);
    QVERIFY(!QFileInfo::exists(target));  // 还没写，目录不该存在

    bool wrote = false;
    writer.writeBundleNow(event, QString(), true, &wrote);
    QVERIFY(wrote);
    QVERIFY(QFileInfo::exists(target));

    // 目录结构：<目标>/<设备>/<日期>/<时间_车牌_event..>/
    const QString deviceDir = QDir(target).filePath(QStringLiteral("rv1126b_001"));
    QVERIFY(QFileInfo(deviceDir).isDir());
    const QStringList days = QDir(deviceDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QCOMPARE(days.size(), 1);
    const QStringList eventDirs = QDir(QDir(deviceDir).filePath(days.first()))
                                      .entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QCOMPARE(eventDirs.size(), 1);
    const QString eventDir = QDir(QDir(deviceDir).filePath(days.first())).filePath(eventDirs.first());

    // 车牌在目录名里，简体中文摘要与三份 JSON 都在
    // 2026-10-07: the folder name no longer contains the plate - plate text comes
    // from OCR and changes between runs, which made a repeated sync recreate the
    // bundle under a new name and never recognise "already written". Assert the
    // stable identity (and that the plate is gone) instead.
    QVERIFY(eventDirs.first().contains(QStringLiteral("_event")));
    QVERIFY(!eventDirs.first().contains(QStringLiteral("A12345")));
    QVERIFY(QFileInfo::exists(QDir(eventDir).filePath(QStringLiteral("summary.txt"))));
    QVERIFY(QFileInfo::exists(QDir(eventDir).filePath(QStringLiteral("detail.json"))));
    QVERIFY(QFileInfo::exists(QDir(eventDir).filePath(QStringLiteral("ocr.json"))));
    QVERIFY(QFileInfo::exists(QDir(eventDir).filePath(QStringLiteral("event.json"))));

    QFile summary(QDir(eventDir).filePath(QStringLiteral("summary.txt")));
    QVERIFY(summary.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(summary.readAll());
    QVERIFY(text.contains(QStringLiteral("车牌：京A12345")));
    QVERIFY(text.contains(QStringLiteral("速度：37 km/h")));

    // 根目录的 records.csv 应该有一行表头 + 一行数据
    QFile csv(QDir(target).filePath(QStringLiteral("records.csv")));
    QVERIFY(csv.open(QIODevice::ReadOnly));
    const QStringList lines = QString::fromUtf8(csv.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(lines.size(), 2);
    QVERIFY(lines.first().startsWith(QStringLiteral("设备,时间")));
    QVERIFY(lines.at(1).contains(QStringLiteral("京A12345")));
}

void DetectionResultWriterTest::autoModeRespectsSwitchAndDeduplicates()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    SqliteEventRepository repository(QDir(temp.path()).filePath(QStringLiteral("events.sqlite")));
    bool initialized = false;
    repository.initialize(this, [&initialized](ApiResult<void> result) { initialized = result.isSuccess(); });
    QVERIFY(initialized);

    const VehicleEvent event = makeEvent(QStringLiteral("dev-a"), 1, 1);
    const QString target = QDir(temp.path()).filePath(QStringLiteral("out"));
    DetectionResultWriter writer(&repository);
    writer.setTargetRoot(target);

    int written = 0;
    connect(&writer, &DetectionResultWriter::bundleWritten, this, [&written](const EventIdentity&, const QString&) {
        ++written;
    });

    // 自动导出默认关闭：什么都不写
    writer.writeAutoBundle(event, QString());
    QCOMPARE(written, 0);
    QVERIFY(!QFileInfo::exists(target));

    writer.setAutoEnabled(true);
    writer.writeAutoBundle(event, QString());
    QCOMPARE(written, 1);
    // 同一条事件重复通知不应该重复写
    writer.writeAutoBundle(event, QString());
    QCOMPARE(written, 1);
    QVERIFY(writer.alreadyWritten(event.identity));

    // force 覆盖（一键拉取用）
    bool forced = false;
    writer.writeBundleNow(event, QString(), true, &forced);
    QVERIFY(forced);
}

void DetectionResultWriterTest::createsMissingTargetFolder()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    SqliteEventRepository repository(QDir(temp.path()).filePath(QStringLiteral("events.sqlite")));
    bool initialized = false;
    repository.initialize(this, [&initialized](ApiResult<void> result) { initialized = result.isSuccess(); });
    QVERIFY(initialized);

    const QString nested = QDir(temp.path()).filePath(QStringLiteral("a/b/c/检测结果"));
    DetectionResultWriter writer(&repository);
    writer.setTargetRoot(nested);
    QString errorMessage;
    QVERIFY(writer.ensureTargetRoot(&errorMessage));
    QVERIFY(QFileInfo(nested).isDir());

    writer.setTargetRoot(QString());
    QVERIFY(!writer.ensureTargetRoot(&errorMessage));
    QVERIFY(!errorMessage.isEmpty());
}

QTEST_MAIN(DetectionResultWriterTest)
#include "DetectionResultWriterTest.moc"
