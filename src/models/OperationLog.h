#pragma once

#include <QDateTime>
#include <QString>

struct OperationLog {
    QDateTime timestamp;
    QString level = QStringLiteral("INFO");
    QString target;
    QString action;
    QString message;
};
