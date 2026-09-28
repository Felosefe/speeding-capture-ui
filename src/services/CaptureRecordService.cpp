#include "CaptureRecordService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>
#include <QVariant>

namespace {
QString defaultCaptureDatabasePath()
{
#ifdef CAMERA_MANAGER_SOURCE_DIR
    QDir root(QString::fromUtf8(CAMERA_MANAGER_SOURCE_DIR));
#else
    QDir root(QCoreApplication::applicationDirPath());
#endif
    return root.filePath(QStringLiteral("data/capture_records.sqlite"));
}

QString nonNullText(const QString& value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

QString captureTypeToStorage(CaptureType type)
{
    return QString::number(static_cast<int>(type));
}

CaptureType captureTypeFromStorage(const QVariant& value)
{
    const int type = value.toInt();
    switch (type) {
    case static_cast<int>(CaptureType::Overspeed):
        return CaptureType::Overspeed;
    case static_cast<int>(CaptureType::UnknownPlate):
        return CaptureType::UnknownPlate;
    case static_cast<int>(CaptureType::Blacklist):
        return CaptureType::Blacklist;
    case static_cast<int>(CaptureType::Normal):
    default:
        return CaptureType::Normal;
    }
}

QString plateStateToStorage(CapturePlateState state)
{
    return state == CapturePlateState::Unknown ? QStringLiteral("unknown") : QStringLiteral("valid");
}

CapturePlateState plateStateFromStorage(const QVariant& value)
{
    return value.toString() == QStringLiteral("unknown")
        ? CapturePlateState::Unknown
        : CapturePlateState::Valid;
}
}

CaptureRecordService::CaptureRecordService(QObject* parent)
    : CaptureRecordService(defaultCaptureDatabasePath(), parent)
{
}

CaptureRecordService::CaptureRecordService(const QString& databasePath, QObject* parent)
    : QObject(parent)
    , databasePath_(databasePath)
    , connectionName_(QStringLiteral("capture-records-%1").arg(reinterpret_cast<quintptr>(this)))
{
}

CaptureRecordService::~CaptureRecordService()
{
    if (database_.isValid()) {
        database_.close();
    }
    database_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName_);
}

bool CaptureRecordService::initialize()
{
    QDir().mkpath(QFileInfo(databasePath_).absolutePath());

    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        setLastError(QStringLiteral("SQLite driver is not available"));
        return false;
    }

    database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    database_.setDatabaseName(databasePath_);
    if (!database_.open()) {
        setLastError(database_.lastError().text());
        return false;
    }

    return ensureSchema();
}

bool CaptureRecordService::addRecord(const CaptureRecord& record)
{
    if (!database_.isOpen()) {
        setLastError(QStringLiteral("Capture record database is not open"));
        return false;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO capture_records ("
        "id, captured_at, plate_number, plate_color, event_type, device_id, device_name, "
        "direction, coordinate_text, remark, plate_state, capture_type, speed_kmh, "
        "speed_limit_kmh, file_path"
        ") VALUES ("
        ":id, :captured_at, :plate_number, :plate_color, :event_type, :device_id, :device_name, "
        ":direction, :coordinate_text, :remark, :plate_state, :capture_type, :speed_kmh, "
        ":speed_limit_kmh, :file_path"
        ")"));

    if (!bindRecord(query, record) || !query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }

    emit recordAdded(record);
    return true;
}

bool CaptureRecordService::deleteRecord(const QString& id)
{
    if (!database_.isOpen()) {
        setLastError(QStringLiteral("Capture record database is not open"));
        return false;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("DELETE FROM capture_records WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }

    emit recordDeleted(id);
    return true;
}

QVector<CaptureRecord> CaptureRecordService::records(CaptureRecordFilter filter, int maxRows) const
{
    QVector<CaptureRecord> result;
    if (!database_.isOpen()) {
        return result;
    }

    QSqlQuery query(database_);
    const QString columns = QStringLiteral(
        "id, captured_at, plate_number, plate_color, event_type, device_id, device_name, "
        "direction, coordinate_text, remark, plate_state, capture_type, speed_kmh, speed_limit_kmh, file_path");
    const QString sql = maxRows > 0
        ? QStringLiteral(
              "SELECT %1 FROM (SELECT %1 FROM capture_records %2 ORDER BY captured_at DESC, id DESC LIMIT %3) "
              "ORDER BY captured_at ASC, id ASC")
              .arg(columns, filterClause(filter))
              .arg(maxRows)
        : QStringLiteral("SELECT %1 FROM capture_records %2 ORDER BY captured_at ASC, id ASC")
              .arg(columns, filterClause(filter));

    if (!query.exec(sql)) {
        setLastError(query.lastError().text());
        return result;
    }

    while (query.next()) {
        result.append(recordFromQuery(query));
    }

    return result;
}

std::optional<CaptureRecord> CaptureRecordService::latestForDevice(const QString& deviceId) const
{
    if (!database_.isOpen()) {
        return std::nullopt;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT id, captured_at, plate_number, plate_color, event_type, device_id, device_name, "
        "direction, coordinate_text, remark, plate_state, capture_type, speed_kmh, speed_limit_kmh, file_path "
        "FROM capture_records WHERE device_id = :device_id ORDER BY captured_at DESC, id DESC LIMIT 1"));
    query.bindValue(QStringLiteral(":device_id"), deviceId);

    if (!query.exec()) {
        setLastError(query.lastError().text());
        return std::nullopt;
    }

    if (!query.next()) {
        return std::nullopt;
    }

    return recordFromQuery(query);
}

bool CaptureRecordService::exportCsv(CaptureRecordFilter filter, const QString& filePath) const
{
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setLastError(file.errorString());
        return false;
    }

    file.write("\xEF\xBB\xBF", 3);
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);

    stream << csvLine({
        QStringLiteral("抓拍精确时间"),
        QStringLiteral("车牌号码"),
        QStringLiteral("车牌颜色"),
        QStringLiteral("事件类型"),
        QStringLiteral("设备编号"),
        QStringLiteral("通行朝向"),
        QStringLiteral("画面坐标参数 (w/c)"),
        QStringLiteral("备注"),
    });

    const QVector<CaptureRecord> rows = records(filter);
    for (const CaptureRecord& record : rows) {
        stream << csvLine({
            record.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
            record.plateNumber,
            record.plateColor,
            record.eventType,
            record.deviceId,
            record.direction,
            record.coordinateText,
            record.remark,
        });
    }

    stream.flush();
    return true;
}

QString CaptureRecordService::lastError() const
{
    return lastError_;
}

bool CaptureRecordService::ensureSchema()
{
    QSqlQuery query(database_);
    const bool ok = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS capture_records ("
        "id TEXT PRIMARY KEY,"
        "captured_at TEXT NOT NULL,"
        "plate_number TEXT NOT NULL,"
        "plate_color TEXT NOT NULL,"
        "event_type TEXT NOT NULL,"
        "device_id TEXT NOT NULL,"
        "device_name TEXT NOT NULL,"
        "direction TEXT NOT NULL,"
        "coordinate_text TEXT NOT NULL,"
        "remark TEXT NOT NULL,"
        "plate_state TEXT NOT NULL,"
        "capture_type INTEGER NOT NULL,"
        "speed_kmh INTEGER NOT NULL,"
        "speed_limit_kmh INTEGER NOT NULL,"
        "file_path TEXT NOT NULL"
        ")"));

    if (!ok) {
        setLastError(query.lastError().text());
    }
    return ok;
}

bool CaptureRecordService::bindRecord(QSqlQuery& query, const CaptureRecord& record) const
{
    query.bindValue(QStringLiteral(":id"), nonNullText(record.id));
    query.bindValue(QStringLiteral(":captured_at"), record.timestamp.toString(Qt::ISODateWithMs));
    query.bindValue(QStringLiteral(":plate_number"), nonNullText(record.plateNumber));
    query.bindValue(QStringLiteral(":plate_color"), nonNullText(record.plateColor));
    query.bindValue(QStringLiteral(":event_type"), nonNullText(record.eventType));
    query.bindValue(QStringLiteral(":device_id"), nonNullText(record.deviceId));
    query.bindValue(QStringLiteral(":device_name"), nonNullText(record.deviceName));
    query.bindValue(QStringLiteral(":direction"), nonNullText(record.direction));
    query.bindValue(QStringLiteral(":coordinate_text"), nonNullText(record.coordinateText));
    query.bindValue(QStringLiteral(":remark"), nonNullText(record.remark));
    query.bindValue(QStringLiteral(":plate_state"), plateStateToStorage(record.plateState));
    query.bindValue(QStringLiteral(":capture_type"), captureTypeToStorage(record.type));
    query.bindValue(QStringLiteral(":speed_kmh"), record.speedKmh);
    query.bindValue(QStringLiteral(":speed_limit_kmh"), record.speedLimitKmh);
    query.bindValue(QStringLiteral(":file_path"), nonNullText(record.filePath));
    return true;
}

CaptureRecord CaptureRecordService::recordFromQuery(const QSqlQuery& query) const
{
    CaptureRecord record;
    record.id = query.value(0).toString();
    record.timestamp = QDateTime::fromString(query.value(1).toString(), Qt::ISODateWithMs);
    record.plateNumber = query.value(2).toString();
    record.plateColor = query.value(3).toString();
    record.eventType = query.value(4).toString();
    record.deviceId = query.value(5).toString();
    record.deviceName = query.value(6).toString();
    record.direction = query.value(7).toString();
    record.coordinateText = query.value(8).toString();
    record.remark = query.value(9).toString();
    record.plateState = plateStateFromStorage(query.value(10));
    record.type = captureTypeFromStorage(query.value(11));
    record.speedKmh = query.value(12).toInt();
    record.speedLimitKmh = query.value(13).toInt();
    record.filePath = query.value(14).toString();
    return record;
}

QString CaptureRecordService::filterClause(CaptureRecordFilter filter) const
{
    switch (filter) {
    case CaptureRecordFilter::ValidPlate:
        return QStringLiteral("WHERE plate_state = 'valid'");
    case CaptureRecordFilter::UnknownPlate:
        return QStringLiteral("WHERE plate_state = 'unknown'");
    case CaptureRecordFilter::All:
    default:
        return {};
    }
}

QString CaptureRecordService::csvLine(const QStringList& values) const
{
    QStringList escaped;
    escaped.reserve(values.size());
    for (const QString& value : values) {
        escaped.append(csvEscape(value));
    }
    return escaped.join(QLatin1Char(',')) + QLatin1Char('\n');
}

QString CaptureRecordService::csvEscape(const QString& value) const
{
    QString escaped = value;
    escaped.replace(QStringLiteral("\""), QStringLiteral("\"\""));

    if (escaped.contains(QLatin1Char(','))
        || escaped.contains(QLatin1Char('"'))
        || escaped.contains(QLatin1Char('\n'))
        || escaped.contains(QLatin1Char('\r'))) {
        return QStringLiteral("\"%1\"").arg(escaped);
    }

    return escaped;
}

void CaptureRecordService::setLastError(const QString& message) const
{
    lastError_ = message;
}
