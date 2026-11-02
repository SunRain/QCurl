/**
 * @file
 * @brief 实现 QCNetworkReply 的取消、中止与传输暂停控制。
 */

#include "QCCurlMultiManager.h"
#include "QCNetworkError.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "private/QCNetworkReplyFlowControl_p.h"
#include "private/QCNetworkReplyProperties_p.h"

#include <QMetaObject>
#include <QPointer>
#include <QThread>

namespace QCurl {

void QCNetworkReply::cancel()
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this]() { cancel(); }, Qt::QueuedConnection);
        return;
    }

    Q_D(QCNetworkReply);
    const QPointer<QCNetworkReply> observer(this);

    // 如果已经取消或已完成，不需要再操作
    if (d->state == ReplyState::Cancelled || d->state == ReplyState::Finished
        || d->state == ReplyState::Error) {
        return;
    }

    d->diagnosticCurlCode = 0;
    if (d->transferToken != 0) {
        // 回调内取消由 manager 延迟 detach；token 保留到 completion 完成所有权回收。
        d->transferRemovalRequested = true;
        QCCurlMultiManager::instance()->removeTransfer(d->transferToken);
        if (!observer) {
            return;
        }
    }
    // 取消属于可观测错误语义：外部应能稳定区分“用户取消”与“空 body / 尚无数据”等情况
    d->setError(NetworkError::OperationCancelled,
                QCurl::errorString(NetworkError::OperationCancelled));

    if (property(Internal::replyproperties::kDestroying).toBool()) {
        d->state = ReplyState::Cancelled;
        return;
    }

    // 设置取消状态（这会发射 cancelled 信号）
    // 注意：即使在 Idle 状态（重试延迟期间）也允许取消
    Q_UNUSED(d->setState(ReplyState::Cancelled));
}

void QCNetworkReply::abortWithError(NetworkError error, const QString &message)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(
            this,
            [this, error, message]() { abortWithError(error, message); },
            Qt::QueuedConnection);
        return;
    }

    Q_D(QCNetworkReply);
    const QPointer<QCNetworkReply> observer(this);

    if (d->state == ReplyState::Cancelled || d->state == ReplyState::Finished
        || d->state == ReplyState::Error) {
        return;
    }

    d->diagnosticCurlCode = 0;
    if (d->transferToken != 0) {
        d->transferRemovalRequested = true;
        QCCurlMultiManager::instance()->removeTransfer(d->transferToken);
        if (!observer) {
            return;
        }
    }

    const QString resolvedMessage = message.isEmpty() ? QCurl::errorString(error) : message;
    d->setError(error, resolvedMessage);
    Q_UNUSED(d->setState(ReplyState::Error));
}

void QCNetworkReply::pauseTransport(PauseMode mode)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(
            this, [this, mode]() { pauseTransport(mode); }, Qt::QueuedConnection);
        return;
    }

    Q_D(QCNetworkReply);
    Internal::pauseReplyTransport(this, d, mode);
}

void QCNetworkReply::resumeTransport()
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this]() { resumeTransport(); }, Qt::QueuedConnection);
        return;
    }

    Q_D(QCNetworkReply);
    Internal::resumeReplyTransport(this, d);
}

} // namespace QCurl
