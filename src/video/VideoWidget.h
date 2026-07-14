#pragma once

#include "../models/CaptureRecord.h"
#include "../models/Device.h"

#include <QWidget>

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
    void drawEmptyState(QPainter& painter, const QRect& rect, const QString& text) const;

    QString titleText() const;
    QString antiFakeCode() const;

    Mode mode_;
    QTimer* timer_ = nullptr;
    Device device_;
    CaptureRecord latestRecord_;
    bool hasDevice_ = false;
    bool hasLatestRecord_ = false;
    int frameIndex_ = 0;
    VideoDisplayOptions displayOptions_;
};
