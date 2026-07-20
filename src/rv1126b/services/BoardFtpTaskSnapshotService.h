#pragma once

#include "../ports/IEventRepository.h"
#include "../protocol/ApiDtos.h"

#include <QObject>
#include <QString>

namespace rv1126b {

class BoardFtpTaskSnapshotService final : public QObject
{
    Q_OBJECT

public:
    BoardFtpTaskSnapshotService(
        QString deviceId,
        IEventRepository* repository,
        QObject* parent = nullptr);

    QString deviceId() const;

    StoredFtpTask snapshotFromDetail(const FtpTaskDetailDto& detail, qint64 refreshedEpochMs = 0) const;

    RequestId saveTaskDetail(
        const FtpTaskDetailDto& detail,
        QObject* context,
        ApiCompletion<StoredFtpTask> completion);

    RequestId loadTaskSnapshots(
        const FtpTaskQuery& query,
        QObject* context,
        ApiCompletion<QVector<StoredFtpTask>> completion);

private:
    template<typename T>
    RequestId finish(QObject* context, ApiCompletion<T> completion, ApiResult<T> result)
    {
        Q_UNUSED(context)
        const RequestId requestId = RequestId::createUuid();
        if (completion) {
            completion(std::move(result));
        }
        return requestId;
    }

    ApiError serviceError(const QString& code, const QString& message) const;

    QString deviceId_;
    IEventRepository* repository_ = nullptr;
};

} // namespace rv1126b
