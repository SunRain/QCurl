/**
 * @file
 * @brief Orchestrates QCNetworkReply execution on its owner thread.
 */

#include "QCCurlMultiManager.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "private/QCNetworkReplyExecution_p.h"

#include <QDebug>
#include <QMetaObject>
#include <QThread>

namespace QCurl {

void QCNetworkReply::execute()
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this]() { execute(); }, Qt::QueuedConnection);
        return;
    }

    Internal::QCNetworkReplyExecution::run(this);
}

void Internal::QCNetworkReplyExecution::run(QCNetworkReply *reply)
{
    auto *d = reply->d_func();
    if (d->state == ReplyState::Running || d->state == ReplyState::Paused) {
        qWarning() << "QCNetworkReply::execute() called while already running";
        return;
    }
    if (d->state == ReplyState::Cancelled || d->state == ReplyState::Finished
        || d->state == ReplyState::Error) {
        return;
    }

    if (d->errorCode != NetworkError::NoError) {
        Q_UNUSED(d->setState(ReplyState::Error));
        return;
    }

    auto *manager = qobject_cast<QCNetworkAccessManager *>(reply->parent());
    if (completeFromCache(reply, manager) || dispatchMock(reply, manager)
        || !prepareNetwork(reply, manager)) {
        return;
    }

    if (d->setState(ReplyState::Running) == SignalEmissionResult::Destroyed) {
        return;
    }
    qDebug() << "QCNetworkReply::execute: Started async request for"
             << d->request.url();
    QCCurlMultiManager::instance()->addReply(reply);
}

} // namespace QCurl
