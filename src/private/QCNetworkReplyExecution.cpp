/**
 * @file
 * @brief 在所属线程编排 QCNetworkReply 执行、注册与完成回收。
 */

#include "QCCurlMultiManager.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkConnectionPoolManager_p.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "private/QCNetworkLogRedaction_p.h"
#include "private/QCNetworkReplyExecution_p.h"
#include "private/QCurlRuntimeState_p.h"

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

    d->transferState->trace.beginNetworkAttempt();

    if (d->setState(ReplyState::Running) == SignalEmissionResult::Destroyed) {
        return;
    }
    qDebug() << "QCNetworkReply::execute: Started async request for"
             << QCNetworkLogRedaction::redactUrl(d->request.url(),
                                                 d->request.diagnosticQueryPolicy());
    // stateChanged 的同步槽可取消请求；admission 前必须重新确认状态。
    if (d->state != ReplyState::Running && d->state != ReplyState::Paused) {
        return;
    }
    auto result = QCCurlMultiManager::instance()->admitHttpReply(std::move(d->curlManager),
                                                                 {reply, manager, d->transferState},
                                                                 completionHandler(reply));
    consumeAdmission(reply, std::move(result));
}

QCCurlMultiManager::TransferCompletionHandler Internal::QCNetworkReplyExecution::completionHandler(
    QCNetworkReply *reply)
{
    Q_ASSERT(reply && QThread::currentThread() == reply->thread());
    const QPointer<QCNetworkReply> observer(reply);
    QCCurlMultiManager *const dispatcher = QCCurlMultiManager::instance();
    return [observer, dispatcher](QCCurlHandleManager &&handle, CURLcode code, long httpStatus) {
        // manager 的所属线程队列覆盖 reply 生命周期；错误线程不得判空或解引用 observer。
        const bool delivered = QMetaObject::invokeMethod(
            dispatcher,
            [observer, handle = std::move(handle), code, httpStatus]() mutable {
                if (!observer) {
                    return;
                }
                auto *d = observer->d_func();
                if (!handle.isValid()) {
                    // 隔离通知没有所有权转移；保留 token，终态抑制由原控制路径处理。
                    observer->abortWithError(NetworkError::Unknown, Internal::runtimeDiagnostic());
                    return;
                }
                d->curlManager   = std::move(handle);
                d->transferToken = 0;
                d->onCurlMultiFinished(code, httpStatus);
            },
            Qt::AutoConnection);
        if (!delivered) {
            qCritical() << "QCNetworkReply: 完成事件投递失败，detached handle 已安全释放";
        }
    };
}

void Internal::QCNetworkReplyExecution::consumeAdmission(
    QCNetworkReply *reply, QCCurlMultiManager::HttpAdmissionResult result)
{
    Q_ASSERT(reply && QThread::currentThread() == reply->thread());
    auto *d             = reply->d_func();
    const bool accepted = result.state == QCCurlMultiManager::HttpAdmissionState::Accepted;
    if (accepted) {
        d->transferToken = result.token;
        if (!d->poolRequestActive) {
            d->poolRequestActive = true;
            Internal::QCNetworkConnectionPoolManagerInternal::recordRequestStarted();
        }
    } else {
        if (result.returnedHandle) {
            d->curlManager = std::move(result.returnedHandle.value());
        }
        d->transferToken = 0;
    }
    d->capabilityWarnings.append(result.warnings);
    if (accepted) {
        return;
    }

    const QPointer<QCNetworkReply> observer(reply);
    const QString message = result.errorMessage.isEmpty() ? QStringLiteral("curl multi 注册失败")
                                                          : result.errorMessage;
    // 工厂与 admission 调用栈不发终态信号，调用方可在返回后建立信号连接。
    const bool delivered = QMetaObject::invokeMethod(
        reply,
        [observer, error = result.error, message]() {
            if (observer) {
                observer->abortWithError(error, message);
            }
        },
        Qt::QueuedConnection);
    if (!delivered) {
        qCritical() << "QCNetworkReply: admission 失败事件投递失败";
    }
}

} // namespace QCurl
