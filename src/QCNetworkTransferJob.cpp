#include "QCNetworkTransferJob.h"

#include "QCNetworkReply.h"

#include <QPointer>
#include <QVariant>

namespace QCurl {

class QCNetworkTransferJobPrivate
{
public:
    QPointer<QCNetworkReply> reply;
    bool finished         = false;
    NetworkError error    = NetworkError::NoError;
    QString errorString;
};

QCNetworkTransferJob::QCNetworkTransferJob(QObject *parent)
    : QObject(parent)
    , d_ptr(new QCNetworkTransferJobPrivate)
{}

QCNetworkTransferJob::~QCNetworkTransferJob() = default;

QCNetworkReply *QCNetworkTransferJob::reply() const noexcept
{
    Q_D(const QCNetworkTransferJob);
    return d->reply.data();
}

bool QCNetworkTransferJob::isFinished() const noexcept
{
    Q_D(const QCNetworkTransferJob);
    return d->finished;
}

NetworkError QCNetworkTransferJob::error() const noexcept
{
    Q_D(const QCNetworkTransferJob);
    return d->error;
}

QString QCNetworkTransferJob::errorString() const
{
    Q_D(const QCNetworkTransferJob);
    return d->errorString;
}

void QCNetworkTransferJob::setReply(QCNetworkReply *reply)
{
    Q_D(QCNetworkTransferJob);
    d->reply = reply;
    if (!reply) {
        return;
    }

    QPointer<QCNetworkTransferJob> safeThis(this);
    QObject::connect(reply, &QObject::destroyed, this, [safeThis]() {
        if (!safeThis || safeThis->isFinished()) {
            return;
        }
        safeThis->failBecauseReplyDestroyed();
    });
}

void QCNetworkTransferJob::fail(NetworkError errorCode, const QString &message)
{
    Q_D(QCNetworkTransferJob);
    if (d->finished) {
        return;
    }

    d->finished    = true;
    d->error       = errorCode;
    d->errorString = message;
    emit failed(errorCode, message);
    emit finished();
}

void QCNetworkTransferJob::finish()
{
    Q_D(QCNetworkTransferJob);
    if (d->finished) {
        return;
    }

    d->finished = true;
    d->error    = NetworkError::NoError;
    d->errorString.clear();
    emit finished();
}

void QCNetworkTransferJob::finishFromReply(QCNetworkReply *reply)
{
    if (!reply) {
        fail(NetworkError::OperationCancelled,
             QStringLiteral("QCNetworkTransferJob: 底层 reply 在任务完成前不可用"));
        return;
    }

    if (reply->error() == NetworkError::OperationCancelled
        && reply->property("_qcurl_reply_destroying").toBool()) {
        failBecauseReplyDestroyed();
    } else if (reply->state() == ReplyState::Cancelled) {
        fail(reply->error(), reply->errorString());
    } else if (reply->error() == NetworkError::NoError) {
        finish();
    } else {
        fail(reply->error(), reply->errorString());
    }
}

void QCNetworkTransferJob::failBecauseReplyDestroyed()
{
    fail(NetworkError::OperationCancelled,
         QStringLiteral("QCNetworkTransferJob: 底层 reply 在任务完成前被销毁"));
}

} // namespace QCurl
