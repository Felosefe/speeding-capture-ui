#include "../src/models/table_models/CaptureRecordTableModel.h"
#include "../src/video/VideoWidget.h"

#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

using namespace rv1126b;

class EventViewUiTest final : public QObject
{
    Q_OBJECT
private slots:
    void compositeIdentityUpsertsWithoutDuplicate();
    void terminalPlateFiltersAndTimeWarningAreVisible();
    void evidencePreviewLoadsJpegAndShowsOfflineOrRetryState();
};

static VehicleEvent uiEvent(qint64 eventId, qint64 trackId, OcrStatus status)
{
    VehicleEvent event;
    event.identity = {QStringLiteral("dev-a"), eventId, trackId};
    event.eventTime.epochMs = 1000;
    event.eventTime.sourceEpochMs = 1000;
    event.eventTime.quality.value = TimeQuality::NativeUtc;
    event.ocrStatus.value = status;
    return event;
}

void EventViewUiTest::compositeIdentityUpsertsWithoutDuplicate()
{
    CaptureRecordTableModel model;
    VehicleEvent queued = uiEvent(1, 10, OcrStatus::Queued);
    model.setVehicleEvents({queued});
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), CaptureRecordTableModel::PlateStateRole).toString(),
             QStringLiteral("pending"));

    queued.ocrStatus.value = OcrStatus::Matched;
    queued.plateText = QStringLiteral("粤B12345");
    model.upsertVehicleEvent(queued);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.vehicleEventAt(0)->plateText, QStringLiteral("粤B12345"));

    model.upsertVehicleEvent(uiEvent(1, 11, OcrStatus::NoPlate));
    QCOMPARE(model.rowCount(), 2);
}

void EventViewUiTest::terminalPlateFiltersAndTimeWarningAreVisible()
{
    CaptureRecordTableModel model;
    VehicleEvent event = uiEvent(1, 1, OcrStatus::NoPlate);
    event.eventTime.quality.value = TimeQuality::BoardEpochUnverified;
    model.setVehicleEvents({event});
    QCOMPARE(model.data(model.index(0, 0), CaptureRecordTableModel::PlateStateRole).toString(),
             QStringLiteral("unknown"));
    QVERIFY(model.data(model.index(0, CaptureRecordTableModel::TimeQualityColumn)).toString()
                .contains(QStringLiteral("未校验")));
    QVERIFY(model.data(model.index(0, 0), Qt::ToolTipRole).toString()
                .contains(QStringLiteral("不可作为可靠 UTC")));
}

void EventViewUiTest::evidencePreviewLoadsJpegAndShowsOfflineOrRetryState()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("evidence.jpg"));
    QImage image(40, 20, QImage::Format_RGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(path, "JPEG"));

    VideoWidget widget(VideoWidget::Mode::Snapshot);
    const VehicleEvent event = uiEvent(1, 1, OcrStatus::Matched);
    widget.setVehicleEvent(&event);
    EvidenceCacheEntry available;
    available.identity = event.identity;
    available.status = EvidenceCacheStatus::Available;
    available.localFilePath = path;
    widget.setEvidenceState(available, false);
    QVERIFY(widget.hasDecodedEvidence());
    QVERIFY(widget.evidenceMessage().isEmpty());

    EvidenceCacheEntry missing;
    missing.identity = event.identity;
    missing.status = EvidenceCacheStatus::Missing;
    missing.failureCode = QStringLiteral("offline_not_cached");
    widget.setEvidenceState(missing, false);
    QVERIFY(widget.evidenceMessage().contains(QStringLiteral("设备离线")));

    EvidenceCacheEntry retry;
    retry.identity = event.identity;
    retry.status = EvidenceCacheStatus::RetryWait;
    widget.setEvidenceState(retry, true);
    QVERIFY(widget.evidenceMessage().contains(QStringLiteral("等待重试")));
}

QTEST_MAIN(EventViewUiTest)
#include "EventViewUiTest.moc"

