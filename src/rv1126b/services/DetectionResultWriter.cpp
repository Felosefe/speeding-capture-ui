#include "DetectionResultWriter.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSharedPointer>
#include <QTextStream>

namespace rv1126b {
namespace {

QString sanitizeSegment(const QString& value, const QString& fallback)
{
    QString result;
    result.reserve(value.size());
    for (const QChar ch : value) {
        if (ch.isLetterOrNumber() || ch == QLatin1Char('-') || ch == QLatin1Char('_')) {
            result.append(ch);
        } else if (ch.isSpace()) {
            result.append(QLatin1Char('_'));
        }
    }
    result = result.left(48);
    return result.isEmpty() ? fallback : result;
}

// 事件时间用板端归一化后的 UTC 毫秒，展示成本地时间（与界面一致）。
QDateTime eventLocalTime(const VehicleEvent& event)
{
    return QDateTime::fromMSecsSinceEpoch(event.eventTime.epochMs).toLocalTime();
}

bool writeTextFile(const QString& path, const QString& text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << text;
    return true;
}

bool writeJsonFile(const QString& path, const QJsonObject& object)
{
    if (object.isEmpty()) {
        return false;
    }
    return writeTextFile(path, QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented)));
}

QString csvEscape(const QString& value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    if (escaped.contains(QLatin1Char(',')) || escaped.contains(QLatin1Char('"'))
        || escaped.contains(QLatin1Char('\n'))) {
        escaped = QLatin1Char('"') + escaped + QLatin1Char('"');
    }
    return escaped;
}

} // namespace

DetectionResultWriter::DetectionResultWriter(IEventRepository* repository, QObject* parent)
    : QObject(parent)
    , repository_(repository)
{
}

void DetectionResultWriter::setTargetRoot(const QString& targetRoot)
{
    targetRoot_ = QDir::cleanPath(targetRoot.trimmed());
}

void DetectionResultWriter::setAutoEnabled(bool enabled)
{
    autoEnabled_ = enabled;
}

bool DetectionResultWriter::ensureTargetRoot(QString* errorMessage) const
{
    if (targetRoot_.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("尚未设置检测结果文件夹");
        return false;
    }
    if (QDir().mkpath(targetRoot_)) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = QStringLiteral("无法创建文件夹：%1").arg(targetRoot_);
    }
    return false;
}

QString DetectionResultWriter::identityKey(const EventIdentity& identity) const
{
    return QStringLiteral("%1/%2/%3")
        .arg(identity.deviceId)
        .arg(identity.eventId)
        .arg(identity.trackId);
}

bool DetectionResultWriter::alreadyWritten(const EventIdentity& identity) const
{
    return writtenIdentities_.contains(identityKey(identity));
}

void DetectionResultWriter::writeBundleNow(const VehicleEvent& event, const QString& evidencePath,
                                           bool force, bool* wrote)
{
    if (wrote) *wrote = false;
    if (!repository_) {
        return;
    }
    const QString key = identityKey(event.identity);
    if (!force && writtenIdentities_.contains(key)) {
        return;
    }
    QString errorMessage;
    if (!ensureTargetRoot(&errorMessage)) {
        emit bundleFailed(event.identity, errorMessage);
        return;
    }

    const QString evidence = evidencePath;
    const VehicleEvent captured = event;
    auto completed = QSharedPointer<bool>::create(false);
    auto written = QSharedPointer<bool>::create(false);
    pendingIdentities_.insert(key);
    repository_->loadDetail(
        event.identity, this,
        [this, completed, written, key, captured, evidence](ApiResult<std::optional<EventDetailSnapshot>> result) {
            *completed = true;
            pendingIdentities_.remove(key);
            /*
             * 2026-10-07：同步兜底（下面那个 if）已经写过这一条时不要再写第二遍。
             * 真实仓储的 loadDetail 是异步的，所以两个分支都会命中：先同步写一次拿到
             * 立刻可用的结果，异步回调回来又写一次 —— 同一个事件的包被写两遍，两次都
             * 会打开 records.csv 和同名包文件，交错写盘。拉取 951 条时表现为"写了 610 个
             * 残缺包（每个只有 summary.txt）然后闪退"。
             */
            if (*written) {
                return;
            }
            *written = writeBundle(captured, result.isSuccess() ? result.value() : std::nullopt, evidence);
        });
    if (!*completed) {
        // 仓储没有同步回调（例如数据库打不开）：退化成只写图和摘要，别让这条彻底丢。
        pendingIdentities_.remove(key);
        *written = writeBundle(captured, std::nullopt, evidence);
    }
    if (wrote) *wrote = *written;
}

void DetectionResultWriter::writeAutoBundle(const VehicleEvent& event, const QString& evidencePath)
{
    if (!autoEnabled_ || !repository_) {
        return;
    }
    const QString key = identityKey(event.identity);
    if (writtenIdentities_.contains(key) || pendingIdentities_.contains(key)) {
        return;
    }

    QString errorMessage;
    if (!ensureTargetRoot(&errorMessage)) {
        emit bundleFailed(event.identity, errorMessage);
        return;
    }

    // 详情从本地库取；取不到也照样写（图 + 摘要仍然有用），不阻塞同步链路。
    // 注意：只有真正写出文件后才记进 writtenIdentities_，否则一次取库失败会让这条
    // 事件永远不再尝试。
    const QString evidence = evidencePath;
    const VehicleEvent captured = event;
    auto completed = QSharedPointer<bool>::create(false);
    pendingIdentities_.insert(key);
    repository_->loadDetail(
        event.identity, this,
        [this, completed, key, captured, evidence](ApiResult<std::optional<EventDetailSnapshot>> result) {
            *completed = true;
            pendingIdentities_.remove(key);
            writeBundle(captured, result.isSuccess() ? result.value() : std::nullopt, evidence);
        });
    if (!*completed) {
        pendingIdentities_.remove(key);
    }
}

bool DetectionResultWriter::writeBundle(const VehicleEvent& event,
                                        const std::optional<EventDetailSnapshot>& detail,
                                        const QString& evidencePath)
{
    const QDateTime when = eventLocalTime(event);
    const QString deviceId = sanitizeSegment(event.identity.deviceId, QStringLiteral("device"));
    const QString dayFolder = when.toString(QStringLiteral("yyyy-MM-dd"));
    const QString plate = event.plateText.trimmed().isEmpty()
                              ? QStringLiteral("无牌")
                              : sanitizeSegment(event.plateText, QStringLiteral("无牌"));
    /*
     * 2026-10-07: the plate is back in the folder name at the user's request
     * (readable: 20260921_160731_闽A163KM_event5605_track396).  Note the event-level
     * skip is keyed on eventId:trackId from index.csv, NOT on the folder path, so a
     * changed plate cannot make an already-pulled event be pulled again.
     */
    const QString eventFolder = QStringLiteral("%1_%2_event%3_track%4")
                                    .arg(when.toString(QStringLiteral("yyyyMMdd_HHmmss")), plate)
                                    .arg(event.identity.eventId)
                                    .arg(event.identity.trackId);

    const QString folder = QDir(targetRoot_).filePath(
        QStringLiteral("%1/%2/%3").arg(deviceId, dayFolder, eventFolder));
    if (!QDir().mkpath(folder)) {
        emit bundleFailed(event.identity, QStringLiteral("无法创建事件文件夹：%1").arg(folder));
        return false;
    }

    bool wroteAnything = false;

    // 1) 证据图（本地缓存直接复制，不再下载）
    bool evidenceWritten = false;
    if (!evidencePath.isEmpty() && QFileInfo::exists(evidencePath)) {
        const QString target = QDir(folder).filePath(QStringLiteral("evidence.jpg"));
        QFile::remove(target);
        evidenceWritten = QFile::copy(evidencePath, target);
        wroteAnything = wroteAnything || evidenceWritten;
    }

    // 2) 板端原始 JSON：事件 / OCR / 详情
    if (detail.has_value()) {
        wroteAnything |= writeJsonFile(QDir(folder).filePath(QStringLiteral("detail.json")),
                                       detail->rawJson);
        wroteAnything |= writeJsonFile(QDir(folder).filePath(QStringLiteral("ocr.json")),
                                       detail->ocr);
        const QJsonObject formal = detail->rawJson.value(QStringLiteral("formal")).toObject();
        wroteAnything |= writeJsonFile(QDir(folder).filePath(QStringLiteral("event.json")), formal);
    }

    // 3) 中文摘要：不打开数据库也能看懂这条是什么
    QString summary;
    {
        QTextStream stream(&summary);
        stream.setEncoding(QStringConverter::Utf8);
        stream << "设备：" << event.identity.deviceId << "\n";
        stream << "时间：" << when.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")) << "\n";
        stream << "事件ID：" << event.identity.eventId << "\n";
        stream << "Track ID：" << event.identity.trackId << "\n";
        stream << "车牌：" << (event.plateText.isEmpty() ? QStringLiteral("未识别") : event.plateText) << "\n";
        stream << "车牌颜色：" << (event.plateColor.isEmpty() ? QStringLiteral("未识别") : event.plateColor) << "\n";
        stream << "OCR 状态：" << event.ocrStatus.rawValue << "\n";
        stream << "通行朝向：" << event.motionDirection << "\n";
        stream << "速度：" << (event.speedValid ? QString::number(event.speedKmh) : QStringLiteral("无效"))
               << " km/h\n";
        stream << "测速状态：" << event.speedStatus << "\n";
        stream << "证据图：" << (evidenceWritten ? QStringLiteral("evidence.jpg") : QStringLiteral("缺失"))
               << "\n";
    }
    wroteAnything |= writeTextFile(QDir(folder).filePath(QStringLiteral("summary.txt")), summary);

    // 4) 总表：一行一条，追加写
    const QString csvPath = QDir(targetRoot_).filePath(QStringLiteral("records.csv"));
    const bool needHeader = !QFileInfo::exists(csvPath) || QFileInfo(csvPath).size() == 0;
    QFile csv(csvPath);
    if (csv.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream stream(&csv);
        stream.setEncoding(QStringConverter::Utf8);
        if (needHeader) {
            stream << "设备,时间,事件ID,TrackID,车牌,车牌颜色,OCR状态,通行朝向,速度km/h,测速状态,证据图,目录\n";
        }
        stream << csvEscape(event.identity.deviceId) << ','
               << csvEscape(when.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))) << ','
               << event.identity.eventId << ','
               << event.identity.trackId << ','
               << csvEscape(event.plateText) << ','
               << csvEscape(event.plateColor) << ','
               << csvEscape(event.ocrStatus.rawValue) << ','
               << csvEscape(event.motionDirection) << ','
               << (event.speedValid ? QString::number(event.speedKmh) : QString()) << ','
               << csvEscape(event.speedStatus) << ','
               << (evidenceWritten ? QStringLiteral("有") : QStringLiteral("无")) << ','
               << csvEscape(QDir(targetRoot_).relativeFilePath(folder)) << '\n';
    }

    if (wroteAnything) {
        writtenIdentities_.insert(identityKey(event.identity));
        emit bundleWritten(event.identity, folder);
        return true;
    }
    emit bundleFailed(event.identity, QStringLiteral("没有可写出的内容（事件详情与证据图都缺失）"));
    return false;
}

} // namespace rv1126b
