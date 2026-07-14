#include "../src/models/CaptureRecord.h"
#include "../src/models/SystemSettings.h"
#include "../src/services/CaptureStorageService.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class CaptureStorageServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void expandsFileTemplateAllocatesPaddedIndexAndWritesTextInfo();
    void deletesOldestFilesUntilFreeSpacePolicyIsSatisfied();
    void deletesFilesOlderThanConfiguredAge();
};

static CaptureRecord sampleRecord()
{
    CaptureRecord record;
    record.id = QStringLiteral("capture-1");
    record.timestamp = QDateTime::fromString(QStringLiteral("2026-07-09T10:11:12.345"), Qt::ISODateWithMs);
    record.deviceId = QStringLiteral("device-001");
    record.deviceName = QStringLiteral("Camera 1");
    record.plateNumber = QStringLiteral("ABC123");
    record.plateColor = QStringLiteral("blue");
    record.eventType = QStringLiteral("capture");
    record.type = CaptureType::Overspeed;
    record.speedKmh = 72;
    record.speedLimitKmh = 60;
    return record;
}

void CaptureStorageServiceTest::expandsFileTemplateAllocatesPaddedIndexAndWritesTextInfo()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    SystemSettings settings = SystemSettings::defaults();
    settings.storage.rootPath = tempDir.path();
    settings.storage.normalImageTemplate = QStringLiteral("{yyyyMMdd}/{deviceId}_{plate}_{type}_{index}.jpg");
    settings.storage.indexDigits = 4;
    settings.storage.syncTextInfoFile = true;

    CaptureStorageService service(settings.storage);
    const CaptureStorageResult first = service.saveCaptureAssets(sampleRecord(), CaptureAssetKind::NormalImage);

    QVERIFY2(first.ok, qPrintable(first.errorMessage));
    QVERIFY(first.primaryPath.endsWith(QStringLiteral("20260709/device-001_ABC123_overspeed_0001.jpg")));
    QVERIFY(QFile::exists(first.primaryPath));
    QVERIFY(first.textInfoPath.endsWith(QStringLiteral(".txt")));
    QVERIFY(QFile::exists(first.textInfoPath));

    const CaptureStorageResult second = service.saveCaptureAssets(sampleRecord(), CaptureAssetKind::NormalImage);
    QVERIFY2(second.ok, qPrintable(second.errorMessage));
    QVERIFY(second.primaryPath.endsWith(QStringLiteral("20260709/device-001_ABC123_overspeed_0002.jpg")));
}

void CaptureStorageServiceTest::deletesOldestFilesUntilFreeSpacePolicyIsSatisfied()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString oldPath = QDir(tempDir.path()).filePath(QStringLiteral("old.jpg"));
    const QString newPath = QDir(tempDir.path()).filePath(QStringLiteral("new.jpg"));
    {
        QFile oldFile(oldPath);
        QVERIFY(oldFile.open(QIODevice::WriteOnly));
        oldFile.write(QByteArray(128, 'o'));
        oldFile.close();

        QFile newFile(newPath);
        QVERIFY(newFile.open(QIODevice::WriteOnly));
        newFile.write(QByteArray(128, 'n'));
        newFile.close();
    }

    QFile oldTimestamp(oldPath);
    QVERIFY(oldTimestamp.open(QIODevice::ReadWrite));
    QVERIFY(oldTimestamp.setFileTime(QDateTime::currentDateTime().addDays(-3), QFileDevice::FileModificationTime));
    oldTimestamp.close();

    QFile newTimestamp(newPath);
    QVERIFY(newTimestamp.open(QIODevice::ReadWrite));
    QVERIFY(newTimestamp.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime));
    newTimestamp.close();

    SystemSettings settings = SystemSettings::defaults();
    settings.storage.rootPath = tempDir.path();
    CaptureStorageService service(settings.storage);

    const int removed = service.deleteOldestFiles(1, 1);
    QCOMPARE(removed, 1);
    QVERIFY(!QFile::exists(oldPath));
    QVERIFY(QFile::exists(newPath));
}

void CaptureStorageServiceTest::deletesFilesOlderThanConfiguredAge()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString oldPath = QDir(tempDir.path()).filePath(QStringLiteral("expired.jpg"));
    const QString freshPath = QDir(tempDir.path()).filePath(QStringLiteral("fresh.jpg"));
    {
        QFile oldFile(oldPath);
        QVERIFY(oldFile.open(QIODevice::WriteOnly));
        oldFile.write("old");
        oldFile.close();

        QFile freshFile(freshPath);
        QVERIFY(freshFile.open(QIODevice::WriteOnly));
        freshFile.write("fresh");
        freshFile.close();
    }

    QFile oldTimestamp(oldPath);
    QVERIFY(oldTimestamp.open(QIODevice::ReadWrite));
    QVERIFY(oldTimestamp.setFileTime(QDateTime(QDate(2026, 7, 1), QTime(0, 0)), QFileDevice::FileModificationTime));
    oldTimestamp.close();

    QFile freshTimestamp(freshPath);
    QVERIFY(freshTimestamp.open(QIODevice::ReadWrite));
    QVERIFY(freshTimestamp.setFileTime(QDateTime(QDate(2026, 7, 8), QTime(0, 0)), QFileDevice::FileModificationTime));
    freshTimestamp.close();

    SystemSettings settings = SystemSettings::defaults();
    settings.storage.rootPath = tempDir.path();
    CaptureStorageService service(settings.storage);

    QCOMPARE(service.deleteFilesOlderThan(3, QDateTime(QDate(2026, 7, 9), QTime(12, 0))), 1);
    QVERIFY(!QFile::exists(oldPath));
    QVERIFY(QFile::exists(freshPath));
}

QTEST_MAIN(CaptureStorageServiceTest)

#include "CaptureStorageServiceTest.moc"
