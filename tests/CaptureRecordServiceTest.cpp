#include "../src/models/CaptureRecord.h"
#include "../src/services/CaptureRecordService.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class CaptureRecordServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void persistsDeletesAndExportsRecords();
    void storesNullRemarkAsEmptyText();
    void returnsMostRecentRowsWhenMaxRowsIsSet();
};

void CaptureRecordServiceTest::persistsDeletesAndExportsRecords()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = QDir(tempDir.path()).filePath(QStringLiteral("records.sqlite"));
    CaptureRecordService service(dbPath);
    QVERIFY2(service.initialize(), qPrintable(service.lastError()));

    CaptureRecord valid;
    valid.id = QStringLiteral("capture-1");
    valid.timestamp = QDateTime::fromString(QStringLiteral("2026-07-09T10:11:12.345"), Qt::ISODateWithMs);
    valid.deviceId = QStringLiteral("device-001");
    valid.deviceName = QStringLiteral("一号设备");
    valid.plateNumber = QStringLiteral("粤B12345");
    valid.plateColor = QStringLiteral("蓝牌");
    valid.eventType = QStringLiteral("抓拍");
    valid.direction = QStringLiteral("由北向南");
    valid.coordinateText = QStringLiteral("w=1920,c=(720,360,240,80)");
    valid.remark = QStringLiteral("台账测试");
    valid.plateState = CapturePlateState::Valid;
    valid.speedKmh = 61;
    valid.speedLimitKmh = 60;
    valid.type = CaptureType::Overspeed;
    valid.filePath = QStringLiteral("capture-1.jpg");

    CaptureRecord unknown = valid;
    unknown.id = QStringLiteral("capture-2");
    unknown.plateNumber = QStringLiteral("未知");
    unknown.plateColor = QStringLiteral("-");
    unknown.plateState = CapturePlateState::Unknown;
    unknown.type = CaptureType::UnknownPlate;

    QVERIFY2(service.addRecord(valid), qPrintable(service.lastError()));
    QVERIFY2(service.addRecord(unknown), qPrintable(service.lastError()));

    QCOMPARE(service.records(CaptureRecordFilter::All).size(), 2);
    QCOMPARE(service.records(CaptureRecordFilter::ValidPlate).size(), 1);
    QCOMPARE(service.records(CaptureRecordFilter::UnknownPlate).size(), 1);
    QCOMPARE(service.latestForDevice(QStringLiteral("device-001"))->id, QStringLiteral("capture-2"));

    QVERIFY2(service.deleteRecord(QStringLiteral("capture-2")), qPrintable(service.lastError()));
    QCOMPARE(service.records(CaptureRecordFilter::All).size(), 1);
    QCOMPARE(service.records(CaptureRecordFilter::UnknownPlate).size(), 0);

    CaptureRecordService reloaded(dbPath);
    QVERIFY2(reloaded.initialize(), qPrintable(reloaded.lastError()));
    QCOMPARE(reloaded.records(CaptureRecordFilter::All).size(), 1);
    QCOMPARE(reloaded.records(CaptureRecordFilter::All).first().id, QStringLiteral("capture-1"));

    const QString csvPath = QDir(tempDir.path()).filePath(QStringLiteral("records.csv"));
    QVERIFY2(reloaded.exportCsv(CaptureRecordFilter::All, csvPath), qPrintable(reloaded.lastError()));

    QFile csv(csvPath);
    QVERIFY(csv.open(QIODevice::ReadOnly));
    const QByteArray content = csv.readAll();
    QVERIFY(content.startsWith("\xEF\xBB\xBF"));
    QVERIFY(QString::fromUtf8(content).contains(QStringLiteral("抓拍精确时间,车牌号码,车牌颜色,事件类型,设备编号,通行朝向,画面坐标参数 (w/c),备注")));
    QVERIFY(QString::fromUtf8(content).contains(QStringLiteral("粤B12345")));
}

void CaptureRecordServiceTest::storesNullRemarkAsEmptyText()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = QDir(tempDir.path()).filePath(QStringLiteral("records.sqlite"));
    CaptureRecordService service(dbPath);
    QVERIFY2(service.initialize(), qPrintable(service.lastError()));

    CaptureRecord record;
    record.id = QStringLiteral("capture-null-remark");
    record.timestamp = QDateTime::fromString(QStringLiteral("2026-07-09T10:11:12.345"), Qt::ISODateWithMs);
    record.deviceId = QStringLiteral("device-001");
    record.deviceName = QStringLiteral("一号设备");
    record.plateNumber = QStringLiteral("粤B12345");
    record.plateColor = QStringLiteral("蓝牌");
    record.eventType = QStringLiteral("抓拍");
    record.direction = QStringLiteral("由北向南");
    record.coordinateText = QStringLiteral("w=1920,c=(720,360,240,80)");
    record.plateState = CapturePlateState::Valid;
    record.remark = QString();

    QVERIFY2(service.addRecord(record), qPrintable(service.lastError()));

    const QVector<CaptureRecord> rows = service.records(CaptureRecordFilter::All);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().remark, QStringLiteral(""));
}

void CaptureRecordServiceTest::returnsMostRecentRowsWhenMaxRowsIsSet()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = QDir(tempDir.path()).filePath(QStringLiteral("records.sqlite"));
    CaptureRecordService service(dbPath);
    QVERIFY2(service.initialize(), qPrintable(service.lastError()));

    for (int i = 1; i <= 3; ++i) {
        CaptureRecord record;
        record.id = QStringLiteral("capture-%1").arg(i);
        record.timestamp = QDateTime(QDate(2026, 7, 9), QTime(10, i, 0));
        record.deviceId = QStringLiteral("device-001");
        record.deviceName = QStringLiteral("device");
        record.plateNumber = QStringLiteral("ABC%1").arg(i);
        record.plateColor = QStringLiteral("blue");
        record.eventType = QStringLiteral("capture");
        record.direction = QStringLiteral("north");
        record.coordinateText = QStringLiteral("w=1920");
        QVERIFY2(service.addRecord(record), qPrintable(service.lastError()));
    }

    const QVector<CaptureRecord> rows = service.records(CaptureRecordFilter::All, 2);
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(0).id, QStringLiteral("capture-2"));
    QCOMPARE(rows.at(1).id, QStringLiteral("capture-3"));
}

QTEST_MAIN(CaptureRecordServiceTest)

#include "CaptureRecordServiceTest.moc"
