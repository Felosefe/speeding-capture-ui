#include "DeviceCardDelegate.h"

#include "../models/table_models/DeviceTableModel.h"

#include <QApplication>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

namespace {

// 与设备搜索对话框、整体界面保持同一套配色。
const QColor CardBackground(0xff, 0xff, 0xff);
const QColor CardBackgroundSelected(0xea, 0xf2, 0xfe);
const QColor CardBorder(0xe2, 0xe6, 0xea);
const QColor CardBorderSelected(0x1a, 0x73, 0xe8);
const QColor TitleColor(0x20, 0x24, 0x29);
const QColor MutedColor(0x6b, 0x72, 0x80);

QColor stateColor(DeviceConnectionState state)
{
    switch (state) {
    case DeviceConnectionState::Online:
        return QColor(0x0f, 0x7b, 0x33);
    case DeviceConnectionState::Connecting:
    case DeviceConnectionState::Disconnecting:
        return QColor(0x1a, 0x73, 0xe8);
    case DeviceConnectionState::Degraded:
        return QColor(0xb5, 0x6a, 0x00);
    case DeviceConnectionState::AuthenticationFailed:
    case DeviceConnectionState::Fault:
        return QColor(0xb4, 0x23, 0x18);
    case DeviceConnectionState::Offline:
    default:
        return QColor(0x9a, 0xa0, 0xa6);
    }
}

} // namespace

DeviceCardDelegate::DeviceCardDelegate(const DeviceTableModel* model, Mode mode, QObject* parent)
    : QStyledItemDelegate(parent)
    , model_(model)
    , mode_(mode)
{
}

QSize DeviceCardDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const
{
    const QFontMetrics metrics(option.font);
    if (mode_ == Mode::Property) {
        return QSize(140, metrics.height() + 12);
    }
    // 用真实字体算高度，不要写死数字：字体或缩放一变，写死的行高就会让卡片里的
    // 两行文字溢出到相邻行上（第一版 54px 就是这个毛病）。
    return QSize(180, metrics.height() * 2 + 26);
}

void DeviceCardDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                               const QModelIndex& index) const
{
    if (mode_ == Mode::Property) {
        paintPropertyRow(painter, option, index);
        return;
    }
    paintDeviceCard(painter, option, index);
}

void DeviceCardDelegate::paintPropertyRow(QPainter* painter, const QStyleOptionViewItem& option,
                                          const QModelIndex& index) const
{
    // 属性表是两列（名称 / 值），但这里要把整行当一行画。视图会为每个单元格各调一次
    // paint，所以只在第一列真正绘制，否则整行会被画两遍（第一版就是这个毛病）。
    if (index.column() != 0) {
        return;
    }
    const QString name = index.sibling(index.row(), 0).data(Qt::DisplayRole).toString();
    const QString value = index.sibling(index.row(), 1).data(Qt::DisplayRole).toString();
    if (name.isEmpty() && value.isEmpty()) {
        return;
    }

    // 行宽取整张表的宽度（不是单个单元格），这样"值"能画到右边、能长一点。
    const int rowWidth = option.widget ? option.widget->width() : option.rect.width() * 2;

    painter->save();
    const QRect row(0, option.rect.top(), rowWidth, option.rect.height());
    const QFontMetrics metrics(option.font);
    const int labelWidth = qBound(70, row.width() / 3, 130);

    painter->setPen(QColor(0x6b, 0x72, 0x80));
    painter->drawText(QRect(row.left() + 12, row.top(), labelWidth, row.height()),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      metrics.elidedText(name, Qt::ElideRight, labelWidth - 6));

    painter->setPen(QColor(0x20, 0x24, 0x29));
    const int valueLeft = row.left() + 12 + labelWidth + 8;
    const int valueWidth = qMax(20, row.right() - 10 - valueLeft);
    painter->drawText(QRect(valueLeft, row.top(), valueWidth, row.height()),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      metrics.elidedText(value, Qt::ElideMiddle, valueWidth));

    painter->setPen(QColor(0xf0, 0xf2, 0xf5));
    painter->drawLine(row.left() + 10, row.bottom() - 1, row.right() - 10, row.bottom() - 1);
    painter->restore();
}

void DeviceCardDelegate::paintDeviceCard(QPainter* painter, const QStyleOptionViewItem& option,
                                         const QModelIndex& index) const
{
    if (!model_) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }
    const Device* device = model_->deviceAt(index.row());
    if (!device) {
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRect card = option.rect.adjusted(6, 5, -6, -5);
    const bool selected = option.state & QStyle::State_Selected;
    const QColor accent = stateColor(device->status.connectionState);

    // 卡片本体
    QPainterPath path;
    path.addRoundedRect(QRectF(card), 7.0, 7.0);
    painter->fillPath(path, selected ? CardBackgroundSelected : CardBackground);
    painter->setPen(QPen(selected ? CardBorderSelected : CardBorder, selected ? 1.4 : 1.0));
    painter->drawPath(path);

    // 左侧状态竖条（选中态用统一的蓝色描边，避免两种颜色打架）
    painter->setPen(Qt::NoPen);
    painter->setBrush(selected ? CardBorderSelected : accent);
    painter->drawRoundedRect(QRectF(card.left() + 3.5, card.top() + 9, 3.0, card.height() - 18), 1.5, 1.5);

    // 两行按卡片实际高度均分，上下各留 8px；不写死基线，行高变了也不会溢出。
    const int lineHeight = qMax(12, (card.height() - 16) / 2);
    const int titleBaseline = card.top() + 8 + lineHeight - 4;
    const int detailBaseline = titleBaseline + lineHeight;

    const int left = card.left() + 13;
    const int right = card.right() - 9;
    const int available = qMax(20, right - left);

    // 第一行：设备名 + 右侧状态
    QFont titleFont = option.font;
    titleFont.setBold(true);
    const QFontMetrics titleMetrics(titleFont);
    const QString stateText = connectionStateText(device->status.connectionState);
    const QFontMetrics stateMetrics(option.font);
    const int stateWidth = stateMetrics.horizontalAdvance(stateText) + 2;
    const QString title = titleMetrics.elidedText(
        device->name.isEmpty() ? device->id : device->name, Qt::ElideRight,
        qMax(20, available - stateWidth - 8));

    painter->setFont(titleFont);
    painter->setPen(TitleColor);
    painter->drawText(QPoint(left, titleBaseline), title);

    painter->setFont(option.font);
    painter->setPen(accent);
    painter->drawText(QPoint(right - stateMetrics.horizontalAdvance(stateText), titleBaseline), stateText);

    // 第二行：IP[:端口] · 最后心跳时分秒
    QStringList details;
    if (!device->ipAddress.isEmpty()) {
        details << (device->port > 0
                        ? QStringLiteral("%1:%2").arg(device->ipAddress).arg(device->port)
                        : device->ipAddress);
    } else {
        details << QStringLiteral("IP 未获取");
    }
    if (device->status.lastHeartbeat.isValid()) {
        details << device->status.lastHeartbeat.toString(QStringLiteral("HH:mm:ss"));
    }
    painter->setPen(MutedColor);
    painter->drawText(QPoint(left, detailBaseline),
                      stateMetrics.elidedText(details.join(QStringLiteral(" · ")), Qt::ElideRight, available));

    painter->restore();
}
