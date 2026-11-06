#include "QCCurlMultiManager.h"
#include "QCNetworkReply.h"
#include "private/QCCurlMultiDomain_p.h"
#include "private/QCCurlMultiTransferRecord_p.h"
#include "private/QCCurlOptionAdapter_p.h"
#include "private/QCNetworkLogRedaction_p.h"

#include <QDebug>
#include <QMetaObject>
#include <QMutexLocker>

#include <algorithm>
#include <utility>

namespace QCurl {

namespace {

void logFinishedReply(CURL *easy, QCNetworkReply *reply, CURLcode result, long responseCode)
{
    char *redirectUrl = nullptr;
    curl_easy_getinfo(easy, CURLINFO_REDIRECT_URL, &redirectUrl);
    qDebug() << "QCCurlMultiManager::checkMultiInfo: Request finished"
             << "Reply:" << reply << "CURLcode:" << result << "HTTP code:" << responseCode
             << "Redirect:"
             << (redirectUrl
                     ? QCNetworkLogRedaction::redactUrl(QUrl(QString::fromUtf8(redirectUrl)))
                     : QStringLiteral("none"));
}

} // namespace

std::optional<QCCurlMultiManager::FinishedTransfer> QCCurlMultiManager::takeFinishedTransferLocked(
    CURLMsg *message)
{
    if (!message || message->msg != CURLMSG_DONE) {
        return std::nullopt;
    }

    CURL *easy = message->easy_handle;
    if (!easy) {
        QMutexLocker locker(&m_mutex);
        poisonLocked("QCCurlMultiManager::checkMultiInfo/invalid-done", CURLM_BAD_EASY_HANDLE);
        return std::nullopt;
    }
    QMutexLocker locker(&m_mutex);
    auto transferIt = m_activeTransfers.find(easy);
    if (transferIt == m_activeTransfers.end()) {
        poisonLocked("QCCurlMultiManager::checkMultiInfo/unknown-done", CURLM_BAD_EASY_HANDLE);
        return std::nullopt;
    }

    QCCurlMultiTransferRecord *record = transferIt.value().data();
    if (record->hasPersistentCompletionHandler()) {
        long responseCode = 0;
        curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &responseCode);
        FinishedTransfer transfer;
        transfer.persistentRecord     = transferIt.value();
        transfer.persistentCompletion = record->takePersistentCompletionHandler();
        transfer.curlCode             = message->data.result;
        transfer.httpStatusCode       = responseCode;
        return transfer;
    }
    QPointer<QCNetworkReply> safeReply = record->observer();
    long responseCode                  = 0;
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &responseCode);

    logFinishedReply(easy, safeReply.data(), message->data.result, responseCode);

    auto transfer = detachTransferRecordLocked(record,
                                               message->data.result,
                                               "QCCurlMultiManager::checkMultiInfo");
    if (transfer) {
        transfer->reply          = safeReply;
        transfer->httpStatusCode = responseCode;
    }
    return transfer;
}

std::optional<QCCurlMultiManager::FinishedTransfer> QCCurlMultiManager::detachTransferRecordLocked(
    QCCurlMultiTransferRecord *record, CURLcode result, const char *context)
{
    if (!record) {
        return std::nullopt;
    }
    auto transferIt = std::find_if(m_activeTransfers.begin(),
                                   m_activeTransfers.end(),
                                   [record](const auto &entry) { return entry.data() == record; });
    if (transferIt == m_activeTransfers.end()) {
        return std::nullopt;
    }

    CURL *easy            = transferIt.key();
    const bool persistent = record->hasPersistentCompletionHandler();
    long responseCode     = 0;
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &responseCode);
    if (!removeTransferHandleLocked(transferIt.value(), result, context)) {
        return std::nullopt;
    }
    record->clearActiveHandle();

    if (!detachShareBindingLocked(easy, context)) {
        return std::nullopt;
    }

    QString callbackError;
    if (record->hasCompletionHandler() && !record->unbindCallbacks(&callbackError)) {
        poisonLocked(context, CURLM_INTERNAL_ERROR);
        return std::nullopt;
    }
    FinishedTransfer transfer;
    transfer.curlCode       = result;
    transfer.httpStatusCode = responseCode;
    if (persistent) {
        transfer.persistentRecord     = transferIt.value();
        transfer.persistentCompletion = transferIt.value()->takePersistentCompletionHandler();
    } else {
        transfer.completion     = transferIt.value()->takeCompletionHandler();
        transfer.reply          = transferIt.value()->observer();
        transfer.detachedHandle = transferIt.value()->takeHandle();
    }
    transferIt.value()->clearObserver();
    m_activeTransfers.erase(transferIt);
    m_runningRequests.fetch_sub(1, std::memory_order_relaxed);
    return transfer;
}

bool QCCurlMultiManager::removeTransferHandleLocked(
    const QSharedPointer<QCCurlMultiTransferRecord> &record, CURLcode result, const char *context)
{
    const auto domain = record->domain();
    if (!domain || !domain->multi) {
        poisonLocked(context, CURLM_BAD_HANDLE);
        return false;
    }
    const CURLMcode removeResult = Internal::CurlOptions::removeMultiHandle(domain->multi,
                                                                            record->handle());
    if (removeResult == CURLM_RECURSIVE_API_CALL) {
        if (m_isShuttingDown.load(std::memory_order_relaxed)) {
            poisonLocked(context, removeResult);
            return false;
        }
        // 回调内不能解绑；排队时持有 record，成功前不归还 easy 或 share 引用。
        const QString retryContext = QString::fromUtf8(context ? context : "unknown");
        QMetaObject::invokeMethod(
            this,
            [this, record, result, retryContext]() {
                retryDetachTransferRecord(record, result, retryContext);
            },
            Qt::QueuedConnection);
        return false;
    }
    if (removeResult != CURLM_OK) {
        poisonLocked(context, removeResult);
        return false;
    }
    return true;
}

void QCCurlMultiManager::retryDetachTransferRecord(
    const QSharedPointer<QCCurlMultiTransferRecord> &record, CURLcode result, const QString &context)
{
    if (!record || m_isPoisoned.load(std::memory_order_relaxed)
        || m_isShuttingDown.load(std::memory_order_relaxed)) {
        return;
    }

    std::optional<FinishedTransfer> detached;
    {
        QMutexLocker locker(&m_mutex);
        detached = detachTransferRecordLocked(record.data(), result, context.toUtf8().constData());
    }
    if (detached) {
        dispatchFinishedTransfer(std::move(detached.value()));
    }
}

void QCCurlMultiManager::dispatchFinishedTransfer(FinishedTransfer &&transfer)
{
    if (transfer.persistentCompletion) {
        transfer.persistentCompletion(transfer.persistentRecord->token(),
                                      transfer.curlCode,
                                      transfer.httpStatusCode);
        return;
    }
    if (transfer.completion) {
        transfer.completion(std::move(transfer.detachedHandle.value()),
                            transfer.curlCode,
                            transfer.httpStatusCode);
        if (transfer.reply) {
            Q_EMIT requestFinished(transfer.reply.data(), static_cast<int>(transfer.curlCode));
        }
        return;
    }
}

} // namespace QCurl
