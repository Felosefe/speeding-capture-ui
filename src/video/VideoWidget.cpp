#include "VideoWidget.h"

#include <QDateTime>
#include <QFont>
#include <QLinearGradient>
#include <QPainter>
#include <QPen>
#include <QTimer>

VideoWidget::VideoWidget(Mode mode, QWidget* parent)
    : QWidget(parent)
    , mode_(mode)
    , timer_(new QTimer(this))
{
    setMinimumSize(320, 220);
    setAutoFillBackground(false);

    timer_->setInterval(mode_ == Mode::Live ? 1000 / displayOptions_.previewFrameRate : 500);
    connect(timer_, &QTimer::timeout, this, &VideoWidget::advanceFrame);
    timer_->start();
}

void VideoWidget::setDevice(const Device* device)
{
    hasDevice_ = device != nullptr;
    if (device) {
        device_ = *device;
    }
    update();
}

void VideoWidget::setLatestRecord(const CaptureRecord* record)
{
    hasLatestRecord_ = record != nullptr;
    if (record) {
        latestRecord_ = *record;
    }
    update();
}

void VideoWidget::setDisplayOptions(const VideoDisplayOptions& options)
{
    displayOptions_ = options;
    const int frameRate = qBound(1, displayOptions_.previewFrameRate, 60);
    timer_->setInterval(mode_ == Mode::Live ? 1000 / frameRate : qMax(250, 1000 / frameRate));
    update();
}

void VideoWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRect rect = this->rect().adjusted(1, 1, -1, -1);
    drawBackground(painter, rect);

    if (!hasDevice_) {
        drawEmptyState(painter, rect, QStringLiteral("请选择设备"));
        return;
    }

    if (mode_ == Mode::Snapshot && !hasLatestRecord_) {
        drawRoad(painter, rect);
        drawEmptyState(painter, rect, QStringLiteral("暂无抓拍图片"));
        return;
    }

    drawRoad(painter, rect);
    if (displayOptions_.showOnlyVehicleFrames && !hasLatestRecord_) {
        drawEmptyState(painter, rect, QStringLiteral("暂无车辆画面"));
        return;
    }
    drawVehicleOverlay(painter, rect);
    drawPlateCloseup(painter, rect);
    drawTextOverlay(painter, rect);
}

void VideoWidget::advanceFrame()
{
    ++frameIndex_;
    if (mode_ == Mode::Live || hasLatestRecord_) {
        update();
    }
}

void VideoWidget::drawBackground(QPainter& painter, const QRect& rect) const
{
    QLinearGradient gradient(rect.topLeft(), rect.bottomLeft());
    gradient.setColorAt(0.0, QColor(18, 24, 32));
    gradient.setColorAt(0.55, QColor(30, 34, 38));
    gradient.setColorAt(1.0, QColor(12, 16, 20));
    painter.fillRect(rect, gradient);

    painter.setPen(QPen(QColor(75, 86, 98), 1));
    painter.drawRect(rect);
}

void VideoWidget::drawRoad(QPainter& painter, const QRect& rect) const
{
    const int horizon = rect.top() + rect.height() / 4;
    const QPoint leftNear(rect.left() + rect.width() / 10, rect.bottom());
    const QPoint rightNear(rect.right() - rect.width() / 10, rect.bottom());
    const QPoint leftFar(rect.center().x() - rect.width() / 8, horizon);
    const QPoint rightFar(rect.center().x() + rect.width() / 8, horizon);

    QPolygon road;
    road << leftNear << leftFar << rightFar << rightNear;
    painter.setBrush(QColor(45, 48, 50));
    painter.setPen(Qt::NoPen);
    painter.drawPolygon(road);

    if (displayOptions_.showCalibrationLines) {
        painter.setPen(QPen(QColor(210, 210, 190), 2, Qt::DashLine));
        for (int i = -1; i <= 1; ++i) {
            const int nearX = rect.center().x() + i * rect.width() / 7;
            const int farX = rect.center().x() + i * rect.width() / 24;
            painter.drawLine(QPoint(nearX, rect.bottom()), QPoint(farX, horizon));
        }
    }

    painter.setPen(QPen(QColor(110, 118, 125), 2));
    painter.drawLine(leftNear, leftFar);
    painter.drawLine(rightNear, rightFar);

    painter.setPen(QPen(QColor(55, 75, 72), 1));
    for (int y = horizon; y < rect.bottom(); y += 28) {
        painter.drawLine(rect.left() + 16, y, rect.right() - 16, y + 12);
    }
}

void VideoWidget::drawVehicleOverlay(QPainter& painter, const QRect& rect) const
{
    const bool snapshot = mode_ == Mode::Snapshot && hasLatestRecord_;
    const int progress = snapshot ? 55 : (frameIndex_ * 3) % 100;
    const int vehicleWidth = rect.width() / 5;
    const int vehicleHeight = rect.height() / 5;
    const int x = rect.center().x() - vehicleWidth / 2 + (snapshot ? 0 : (progress - 50) * rect.width() / 420);
    const int y = rect.top() + rect.height() / 2 + (snapshot ? rect.height() / 12 : progress * rect.height() / 520);

    QRect vehicleRect(x, y, vehicleWidth, vehicleHeight);
    vehicleRect = vehicleRect.intersected(rect.adjusted(24, 36, -24, -28));

    painter.setPen(QPen(QColor(255, 60, 45), 3));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(vehicleRect);

    QRect plateRect(
        vehicleRect.center().x() - vehicleRect.width() / 4,
        vehicleRect.bottom() - vehicleRect.height() / 4,
        vehicleRect.width() / 2,
        qMax(18, vehicleRect.height() / 5));

    painter.setPen(QPen(QColor(60, 150, 255), 2));
    painter.drawRect(plateRect);

    painter.fillRect(plateRect.adjusted(1, 1, -1, -1), QColor(35, 85, 155, 190));
    painter.setPen(Qt::white);
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei"), 9, QFont::Bold));
    painter.drawText(plateRect, Qt::AlignCenter, hasLatestRecord_ ? latestRecord_.plateNumber : QStringLiteral("粤B12345"));
}

void VideoWidget::drawPlateCloseup(QPainter& painter, const QRect& rect) const
{
    if (mode_ != Mode::Snapshot || !hasLatestRecord_ || displayOptions_.plateImagePosition == QStringLiteral("hidden")) {
        return;
    }

    QRect closeupRect;
    if (displayOptions_.plateImagePosition == QStringLiteral("bottom")) {
        closeupRect = QRect(rect.center().x() - rect.width() / 5, rect.bottom() - 112, rect.width() * 2 / 5, 56);
    } else {
        closeupRect = QRect(rect.right() - rect.width() / 3 - 18, rect.center().y() - 28, rect.width() / 3, 56);
    }

    painter.setPen(QPen(QColor(70, 150, 255), 2));
    painter.setBrush(QColor(15, 42, 78, 210));
    painter.drawRoundedRect(closeupRect, 4, 4);
    painter.setPen(Qt::white);
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei"), 14, QFont::Bold));
    painter.drawText(closeupRect, Qt::AlignCenter, latestRecord_.plateNumber);
}

void VideoWidget::drawTextOverlay(QPainter& painter, const QRect& rect) const
{
    const CaptureRecord* record = hasLatestRecord_ ? &latestRecord_ : nullptr;
    const QString plate = record ? record->plateNumber : QStringLiteral("实时检测中");
    const QString plateColor = record ? record->plateColor : QStringLiteral("-");
    const int speed = record ? record->speedKmh : 0;
    const int speedLimit = record ? record->speedLimitKmh : device_.config.speedLimitKmh;
    const QString direction = record ? record->direction : device_.config.direction;
    const QString captureTime = record
        ? record->timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
        : QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));

    painter.setFont(QFont(QStringLiteral("Microsoft YaHei"), 10));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 150));
    painter.drawRoundedRect(rect.adjusted(10, 10, -10, -rect.height() + 100), 4, 4);

    painter.setPen(QColor(235, 242, 250));
    const int left = rect.left() + 18;
    int top = rect.top() + 22;
    const int lineHeight = 20;

    painter.drawText(left, top, titleText());
    top += lineHeight;
    painter.drawText(left, top, QStringLiteral("地点：%1   方向：%2").arg(device_.config.location, direction));
    top += lineHeight;
    painter.drawText(left, top, QStringLiteral("时间：%1   防伪码：%2").arg(captureTime, antiFakeCode()));

    painter.setBrush(QColor(0, 0, 0, 150));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(rect.adjusted(10, rect.height() - 76, -10, -10), 4, 4);

    painter.setPen(QColor(235, 242, 250));
    const QString vehicleLine = displayOptions_.overlaySpeed
        ? QStringLiteral("车牌：%1   颜色：%2   速度：%3 km/h   限速：%4 km/h")
              .arg(plate, plateColor)
              .arg(speed)
              .arg(speedLimit)
        : QStringLiteral("车牌：%1   颜色：%2").arg(plate, plateColor);
    painter.drawText(rect.left() + 18, rect.bottom() - 48, vehicleLine);
    painter.drawText(rect.left() + 18, rect.bottom() - 24,
                     QStringLiteral("设备：%1   状态：%2")
                         .arg(device_.name, connectionStateText(device_.status.connectionState)));
}

void VideoWidget::drawEmptyState(QPainter& painter, const QRect& rect, const QString& text) const
{
    painter.setPen(QColor(150, 165, 180));
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei"), 16, QFont::Bold));
    painter.drawText(rect, Qt::AlignCenter, text);
}

QString VideoWidget::titleText() const
{
    return mode_ == Mode::Live ? QStringLiteral("实时预览") : QStringLiteral("最近抓拍");
}

QString VideoWidget::antiFakeCode() const
{
    const QString seed = hasLatestRecord_ ? latestRecord_.id : device_.id;
    return QString::number(qHash(seed) & 0xFFFFFF, 16).rightJustified(8, QLatin1Char('0')).toUpper();
}
