#pragma once

#include "../models/CaptureRecord.h"
#include "../models/Device.h"
#include "../rv1126b/domain/Models.h"

#include <QImage>
#include <QWidget>

#include <optional>

class QTimer;

struct VideoDisplayOptions {
    int previewFrameRate = 12;
    bool showOnlyVehicleFrames = false;
    bool overlaySpeed = true;
    bool showCalibrationLines = true;
    QString plateImagePosition = QStringLiteral("right");
};

class VideoWidget final : public QWidget
{
    Q_OBJECT

public:
    enum class Mode {
        Live,
        Snapshot
    };

    explicit VideoWidget(Mode mode, QWidget* parent = nullptr);

    void setDevice(const Device* device);
    void setLatestRecord(const CaptureRecord* record);
    void setVehicleEvent(const rv1126b::VehicleEvent* event);
    void setEvidenceState(const std::optional<rv1126b::EvidenceCacheEntry>& entry,
                          bool deviceOnline);
    void clearVehicleEvent();
    QString evidenceMessage() const;
    bool hasDecodedEvidence() const;
    void setDisplayOptions(const VideoDisplayOptions& options);

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void advanceFrame();

private:
    void drawBackground(QPainter& painter, const QRect& rect) const;
    void drawRoad(QPainter& painter, const QRect& rect) const;
    void drawVehicleOverlay(QPainter& painter, const QRect& rect) const;
    void drawPlateCloseup(QPainter& painter, const QRect& rect) const;
    void drawTextOverlay(QPainter& painter, const QRect& rect) const;
    void drawEvidence(QPainter& painter, const QRect& rect) const;
    void drawVehicleEventOverlay(QPainter& painter, const QRect& rect) const;
    void drawEmptyState(QPainter& painter, const QRect& rect, const QString& text) const;

    QString titleText() const;
    QString antiFakeCode() const;

    Mode mode_;
    QTimer* timer_ = nullptr;
    Device device_;
    CaptureRecord latestRecord_;
    bool hasDevice_ = false;
    bool hasLatestRecord_ = false;
    bool hasVehicleEvent_ = false;
    bool evidenceDeviceOnline_ = false;
    int frameIndex_ = 0;
    VideoDisplayOptions displayOptions_;
    rv1126b::VehicleEvent vehicleEvent_;
    std::optional<rv1126b::EvidenceCacheEntry> evidenceEntry_;
    QImage evidenceImage_;
    QString evidenceMessage_;
};
