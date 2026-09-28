#pragma once

#include "../domain/Models.h"
#include "../ports/IEventRepository.h"

#include <QObject>
#include <QSet>
#include <QString>

#include <optional>

namespace rv1126b {

// 把检测结果写成"人能直接看的文件夹"。
//
// 背景（2026-09-28 现场反馈）：原来只有证据图会自动落到
// <存储根目录>/rv1126b/events/<设备>/<事件>_<轨迹>_evidence.jpg，
// 车牌、速度、OCR 状态这些结果只进 SQLite，用户在资源管理器里翻不到。
//
// 这个服务在证据图落盘成功后，按事件建一个子目录，写出：
//     evidence.jpg / snapshot.jpg / event.json / ocr.json / detail.json / summary.txt
// JSON 全部来自本地库（同步时已存好的详情），不会为了写文件再压板端。
//
// 目录结构（与「资料包导出」保持一致，方便用户混着看）：
//     <目标文件夹>/<设备ID>/<yyyy-MM-dd>/<yyyyMMdd_HHmmss>_<车牌>_event<ID>_track<ID>/
//     <目标文件夹>/records.csv        ← 一行一条，追加写，Excel 可直接打开
class DetectionResultWriter final : public QObject
{
    Q_OBJECT

public:
    explicit DetectionResultWriter(IEventRepository* repository, QObject* parent = nullptr);

    void setTargetRoot(const QString& targetRoot);
    QString targetRoot() const { return targetRoot_; }
    void setAutoEnabled(bool enabled);
    bool autoEnabled() const { return autoEnabled_; }

    // 目标文件夹不存在就新建（用户要求"没有就新建"）。
    bool ensureTargetRoot(QString* errorMessage = nullptr) const;

    // 证据图已就绪时调用。自动导出关闭、事件已写过都会直接返回。
    // evidencePath 为空表示本地还没有图，此时仍然写 JSON/摘要（结果本身不该丢）。
    void writeAutoBundle(const VehicleEvent& event, const QString& evidencePath);

    // 「一键拉回本机」用：不套用自动开关，按需覆盖已有目录（force）。
    // 详情从本地库取不到时也会把图 + 摘要写出来。
    // wrote 允许为空：由调用方决定要不要关心这一条到底写成了没有。
    void writeBundleNow(const VehicleEvent& event, const QString& evidencePath, bool force,
                        bool* wrote = nullptr);

    // 事件是否已经写出过（供拉取流程去重）。
    bool alreadyWritten(const EventIdentity& identity) const;

signals:
    void bundleWritten(const rv1126b::EventIdentity& identity, const QString& folderPath);
    void bundleFailed(const rv1126b::EventIdentity& identity, const QString& reason);

private:
    QString identityKey(const EventIdentity& identity) const;
    bool writeBundle(const VehicleEvent& event,
                     const std::optional<EventDetailSnapshot>& detail,
                     const QString& evidencePath);

    IEventRepository* repository_ = nullptr;
    QString targetRoot_;
    QSet<QString> writtenIdentities_;
    QSet<QString> pendingIdentities_;
    bool autoEnabled_ = false;
};

} // namespace rv1126b
