/**
 * @file
 * @brief QCNetworkReply cancellation and pause/resume controls.
 */

#include "QCCurlMultiManager.h"
#include "QCNetworkError.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "private/QCNetworkReplyFlowControl_p.h"

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

    // 如果已经取消或已完成，不需要再操作
    if (d->state == ReplyState::Cancelled || d->state == ReplyState::Finished
        || d->state == ReplyState::Error) {
        return;
    }

    // 从多句柄管理器移除（Running/Paused 状态）。
    if (d->state == ReplyState::Running || d->state == ReplyState::Paused) {
        // ⚠️ cancel 可能在 libcurl 回调栈内触发（例如在 downloadProgress 槽函数中）。
        // 直接调用 curl_multi_remove_handle 会引入重入风险，并可能触发 CURLM_BAD_EASY_HANDLE。
        // manager 会先保活 transfer record，并在 callback 栈退出后执行移除。
        auto *multiManager = QCCurlMultiManager::instance();
        if (d->multiTransferRecord) {
            auto *record                = d->multiTransferRecord;
            d->transferRemovalRequested = true;
            multiManager->removeTransferRecord(record);
        } else {
            QPointer<QCNetworkReply> safeThis(this);
            QMetaObject::invokeMethod(
                multiManager,
                [multiManager, safeThis]() {
                    if (safeThis) {
                        multiManager->removeReply(safeThis.data());
                    }
                },
                Qt::QueuedConnection);
        }
    }
    // 取消属于可观测错误语义：外部应能稳定区分“用户取消”与“空 body / 尚无数据”等情况
    d->setError(NetworkError::OperationCancelled,
                QCurl::errorString(NetworkError::OperationCancelled));

    if (property("_qcurl_reply_destroying").toBool()) {
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

    if (d->state == ReplyState::Cancelled || d->state == ReplyState::Finished
        || d->state == ReplyState::Error) {
        return;
    }

    if (d->state == ReplyState::Running || d->state == ReplyState::Paused) {
        auto *multiManager = QCCurlMultiManager::instance();
        if (d->multiTransferRecord) {
            auto *record                = d->multiTransferRecord;
            d->transferRemovalRequested = true;
            multiManager->removeTransferRecord(record);
        } else {
            QPointer<QCNetworkReply> safeThis(this);
            QMetaObject::invokeMethod(
                multiManager,
                [multiManager, safeThis]() {
                    if (safeThis) {
                        multiManager->removeReply(safeThis.data());
                    }
                },
                Qt::QueuedConnection);
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
