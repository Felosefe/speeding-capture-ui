#pragma once

#include "../models/CaptureRecord.h"

#include <QObject>
#include <QSqlDatabase>
#include <QVector>

#include <optional>

enum class CaptureRecordFilter {
    All,
    ValidPlate,
    UnknownPlate
};

class CaptureRecordService final : public QObject
{
    Q_OBJECT

public:
    explicit CaptureRecordService(QObject* parent = nullptr);
    explicit CaptureRecordService(const QString& databasePath, QObject* parent = nullptr);
    ~CaptureRecordService() override;

    bool initialize();
    bool addRecord(const CaptureRecord& record);
    bool deleteRecord(const QString& id);
    QVector<CaptureRecord> records(CaptureRecordFilter filter, int maxRows = -1) const;
    std::optional<CaptureRecord> latestForDevice(const QString& deviceId) const;
    bool exportCsv(CaptureRecordFilter filter, const QString& filePath) const;

    QString lastError() const;

signals:
    void recordAdded(const CaptureRecord& record);
    void recordDeleted(const QString& id);

private:
    bool ensureSchema();
    bool bindRecord(QSqlQuery& query, const CaptureRecord& record) const;
    CaptureRecord recordFromQuery(const QSqlQuery& query) const;
    QString filterClause(CaptureRecordFilter filter) const;
    QString csvLine(const QStringList& values) const;
    QString csvEscape(const QString& value) const;
    void setLastError(const QString& message) const;

    QString databasePath_;
    QString connectionName_;
    QSqlDatabase database_;
    mutable QString lastError_;
};
