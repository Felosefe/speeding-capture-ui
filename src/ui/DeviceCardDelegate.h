#pragma once

#include <QStyledItemDelegate>

class DeviceTableModel;

// 左栏「设备」列表的卡片式绘制。
//
// 为什么不用 QTableView 默认的行绘制：真机模式下这一栏只有 200 多像素宽，
// 默认绘制会把「RV1126B · rv1126b_001 / 192.168.137.73:18080 · 在线 / 最后心跳 …」
// 硬折成四五行、还被选中蓝底压住，很难看。这里画成一张一张的卡片：
//
//     ┌──────────────────────────────┐
//     │ ▌ RV1126B · rv1126b_001  在线 │   ← 左侧竖条按连接状态着色
//     │   192.168.137.73 · 19:25:36   │
//     └──────────────────────────────┘
//
// 选中态用浅蓝底 + 深蓝描边，而不是整块高饱和蓝，保证文字始终清楚。
class DeviceCardDelegate final : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit DeviceCardDelegate(const DeviceTableModel* model, QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    const DeviceTableModel* model_ = nullptr;
};
