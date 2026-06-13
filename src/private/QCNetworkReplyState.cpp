/**
 * @file
 * @brief QCNetworkReply private state transitions and curl callbacks.
 */

#include "QCCurlMultiManager.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkCache.h"
#include "QCNetworkCachePolicy.h"
#include "QCNetworkConnectionPoolManager_p.h"
#include "QCNetworkLogger.h"
#include "QCNetworkReply_p.h"
#include "private/QCNetworkLogRedaction_p.h"
#include "private/QCNetworkReplyBodySource_p.h"
#include "private/QCNetworkReplyCallbacks_p.h"
#include "private/QCNetworkReplyFlowControl_p.h"
#include "private/QCNetworkReplyResponse_p.h"
#include "private/QCNetworkReplyRuntime_p.h"

#include <QPointer>

namespace QCurl {

namespace {

constexpr int kCookieWriteMode = static_cast<int>(QCNetworkAccessManager::WriteOnly);

} // namespace

void QCNetworkReplyPrivate::setState(ReplyState newState)
{
    Q_Q(QCNetworkReply);

    if (state == newState) {
        return;
    }

    if (newState == ReplyState::Finished && beforeFinishTransition) {
        if (const auto finishError = beforeFinishTransition(); finishError.has_value()) {
            setError(NetworkError::InvalidRequest, finishError.value());
            newState = ReplyState::Error;
        }
        beforeFinishTransition = nullptr;
    }

    state = newState;

    if (newState == ReplyState::Running) {
        if (!elapsedTimerStarted) {
            elapsedTimer.start();
            elapsedTimerStarted = true;
            durationMs          = -1;
        }
    } else if (newState == ReplyState::Finished || newState == ReplyState::Error
               || newState == ReplyState::Cancelled) {
        if (elapsedTimerStarted && durationMs < 0) {
            durationMs = elapsedTimer.elapsed();
        }
    }

    // 发射状态变更信号
    emit q->stateChanged(newState);

    // 终态收敛：内部流控状态不应残留（避免影响后续诊断/合同采集）
    if (newState == ReplyState::Finished || newState == ReplyState::Error
        || newState == ReplyState::Cancelled) {
        Internal::clearReplyFlowControlOnTerminalState(this);
    }

    // 根据新状态发射对应信号
    if (newState == ReplyState::Finished || newState == ReplyState::Error) {
        // 终态统一解析当前已收到的响应头；Error 场景下也需要保留最终 header block 可读性。
        parseHeaders();
    }

    if (newState == ReplyState::Finished) {
        // ==================
        // Cookie jar flush：当启用 COOKIEJAR 时，确保请求完成后立即落盘
        // ==================
        if (curlManager.handle() && (cookieMode & kCookieWriteMode) && !cookieFilePath.isEmpty()) {
            const CURLcode rc = curl_easy_setopt(curlManager.handle(), CURLOPT_COOKIELIST, "FLUSH");
            if (rc != CURLE_OK) {
                if (Internal::isReplyCapabilityRelatedCurlError(rc)) {
                    Internal::appendReplyCapabilityWarning(
                        this,
                        QStringLiteral(
                            "libcurl 不支持 Cookie "
                            "flush（CURLOPT_COOKIELIST，%1），CookieJAR 可能不会立即落盘")
                            .arg(QString::fromUtf8(curl_easy_strerror(rc))));
                } else {
                    Internal::appendReplyCapabilityWarning(this,
                                                           QStringLiteral("Cookie flush 失败（%1）")
                                                               .arg(QString::fromUtf8(
                                                                   curl_easy_strerror(rc))));
                }
            }
        }

        // ==================
        // 连接池统计 - 记录连接复用情况
        // ==================
        if (curlManager.handle()) {
            Internal::QCNetworkConnectionPoolManagerInternal::recordRequestCompleted(curlManager
                                                                                         .handle(),
                                                                                     false);
        }

        // ==================
        // 缓存集成 - 请求完成后自动写入缓存
        // ==================
        QCNetworkAccessManager *manager = qobject_cast<QCNetworkAccessManager *>(q->parent());
        if (manager) {
            QCNetworkCache *cache       = manager->cache();
            QCNetworkCachePolicy policy = request.cachePolicy();

            // OnlyNetwork 策略：不缓存
            if (cache && policy != QCNetworkCachePolicy::OnlyNetwork) {
                // 只缓存成功响应
                if (errorCode == NetworkError::NoError) {
                    // 解析响应头（检查可缓存性）
                    QMap<QByteArray, QByteArray> headers = q->parseResponseHeaders();

                    if (QCNetworkCache::isCacheable(headers)) {
                        // 准备元数据
                        QCNetworkCacheMetadata meta;
                        meta.setUrl(request.url());
                        meta.setHeaders(headers);
                        meta.setExpirationDate(QCNetworkCache::parseExpirationDate(headers));

                        // 获取响应数据（不移除缓冲区数据）
                        QByteArray responseData = bodyBuffer.read(bodyBuffer.byteAmount());

                        // 写入缓存
                        cache->insert(request.url(), responseData, meta);
                        qDebug() << "QCNetworkReply: Cached response for"
                                 << request.url().toString();

                        // 重新放回缓冲区（确保 readAll() 仍可用）
                        bodyBuffer.append(responseData);
                    } else {
                        qDebug() << "QCNetworkReply: Response not cacheable for"
                                 << request.url().toString();
                    }
                }
            }
        }

        emit q->finished();
    } else if (newState == ReplyState::Error) {
        // 错误时同时发射 error 和 finished 信号
        emit q->error(errorCode);
        emit q->finished();
    } else if (newState == ReplyState::Cancelled) {
        // 取消属于终态：同时发射 cancelled + finished（对齐 QNetworkReply 行为）
        emit q->cancelled();
        emit q->finished();
    }
}

void QCNetworkReplyPrivate::setError(NetworkError error, const QString &message)
{
    errorCode    = error;
    errorMessage = message;
}

void QCNetworkReplyPrivate::parseHeaders()
{
    Internal::parseReplyHeaders(this);
}

bool QCNetworkReplyPrivate::applyPauseMask(int desiredMask)
{
    return Internal::applyReplyPauseMask(this, desiredMask);
}

int QCNetworkReplyPrivate::desiredPauseMask() const
{
    return Internal::desiredReplyPauseMask(this);
}

void QCNetworkReplyPrivate::setBackpressureActive(bool active)
{
    Internal::setReplyBackpressureActive(this, active);
}

void QCNetworkReplyPrivate::setUploadSendPaused(bool paused)
{
    Internal::setReplyUploadSendPaused(this, paused);
}

void QCNetworkReplyPrivate::maybeResumeRecvFromBackpressure()
{
    Internal::maybeResumeReplyRecvFromBackpressure(this);
}

void QCNetworkReplyPrivate::resumeSendFromRequestBodySourceIfNeeded()
{
    Internal::resumeReplySendFromRequestBodySourceIfNeeded(this);
}

// ==================
// Curl 静态回调函数实现
// ==================

size_t QCNetworkReplyPrivate::curlWriteCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    return Internal::writeReplyCurlCallback(ptr,
                                            size,
                                            nmemb,
                                            static_cast<QCNetworkReplyPrivate *>(userdata));
}

size_t QCNetworkReplyPrivate::curlHeaderCallback(char *ptr,
                                                 size_t size,
                                                 size_t nmemb,
                                                 void *userdata)
{
    return Internal::headerReplyCurlCallback(ptr,
                                             size,
                                             nmemb,
                                             static_cast<QCNetworkReplyPrivate *>(userdata));
}

size_t QCNetworkReplyPrivate::curlReadCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    Internal::ReplyCurlCallbackScope callbackScope;

    return Internal::readReplyBodySourceCallback(ptr,
                                                 size,
                                                 nmemb,
                                                 static_cast<QCNetworkReplyPrivate *>(userdata));
}

int QCNetworkReplyPrivate::curlSeekCallback(void *userdata, curl_off_t offset, int origin)
{
    auto *d = static_cast<QCNetworkReplyPrivate *>(userdata);
    return Internal::seekReplyBodySourceCallback(d, offset, origin);
}

int QCNetworkReplyPrivate::curlProgressCallback(
    void *userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    return Internal::progressReplyCurlCallback(static_cast<QCNetworkReplyPrivate *>(userdata),
                                               dltotal,
                                               dlnow,
                                               ultotal,
                                               ulnow);
}

int QCNetworkReplyPrivate::curlDebugCallback(
    CURL *handle, curl_infotype type, char *data, size_t size, void *userptr)
{
    Q_UNUSED(handle);

    auto *d = static_cast<QCNetworkReplyPrivate *>(userptr);
    if (!d || !d->q_ptr) {
        return 0;
    }

    if (d->state == ReplyState::Cancelled || d->state == ReplyState::Error) {
        return 0;
    }

    auto *manager = qobject_cast<QCNetworkAccessManager *>(d->q_ptr->parent());
    if (!manager || !manager->debugTraceEnabled()) {
        return 0;
    }

    QCNetworkLogger *logger = manager->logger();
    if (!logger) {
        return 0;
    }

    const QByteArray raw  = QByteArray(data, static_cast<int>(size));
    const QString message = formatDebugTraceMessage(type, raw);
    if (message.isEmpty()) {
        return 0;
    }

    logger->log(NetworkLogLevel::Debug, QStringLiteral("Trace"), message);
    return 0;
}

QString QCNetworkReplyPrivate::formatDebugTraceMessage(curl_infotype type, const QByteArray &raw)
{
    auto redactBlock = [](const QByteArray &block) -> QString {
        const QList<QByteArray> lines = block.split('\n');
        QStringList out;
        out.reserve(lines.size());
        for (const QByteArray &line : lines) {
            if (line.isEmpty()) {
                continue;
            }
            out.append(QCNetworkLogRedaction::redactSensitiveTraceLine(line));
        }
        return out.join(QStringLiteral("\n"));
    };

    QString message;

    switch (type) {
        case CURLINFO_TEXT:
            message = QStringLiteral("TEXT: %1").arg(redactBlock(raw).trimmed());
            break;
        case CURLINFO_HEADER_IN:
            message = QStringLiteral("HEADER_IN: %1").arg(redactBlock(raw).trimmed());
            break;
        case CURLINFO_HEADER_OUT:
            message = QStringLiteral("HEADER_OUT: %1").arg(redactBlock(raw).trimmed());
            break;
        case CURLINFO_DATA_IN:
            message = QStringLiteral("DATA_IN: len=%1").arg(static_cast<qulonglong>(raw.size()));
            break;
        case CURLINFO_DATA_OUT:
            message = QStringLiteral("DATA_OUT: len=%1").arg(static_cast<qulonglong>(raw.size()));
            break;
        case CURLINFO_SSL_DATA_IN:
            message = QStringLiteral("SSL_DATA_IN: len=%1").arg(static_cast<qulonglong>(raw.size()));
            break;
        case CURLINFO_SSL_DATA_OUT:
            message = QStringLiteral("SSL_DATA_OUT: len=%1").arg(static_cast<qulonglong>(raw.size()));
            break;
        default:
            message = QStringLiteral("TRACE_%1: len=%2")
                          .arg(static_cast<int>(type))
                          .arg(static_cast<qulonglong>(raw.size()));
            break;
    }

    if (message.size() > 4096) {
        message = message.left(4096) + QStringLiteral("…(truncated)");
    }

    return message;
}

void QCNetworkReplyPrivate::onCurlMultiFinished(CURLcode curlCode)
{
    Q_Q(QCNetworkReply);

    // 已取消/已错误：保持既有可观测语义，不允许完成回调覆盖状态
    if (state == ReplyState::Cancelled || state == ReplyState::Error
        || state == ReplyState::Finished) {
        return;
    }

    long httpCode = 0;
    if (CURL *handle = curlManager.handle()) {
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &httpCode);
    }

    const auto info = Internal::attemptErrorFromCurlAndHttp(this, curlCode, httpCode);

    if (info.error == NetworkError::NoError) {
        setState(ReplyState::Finished);
        return;
    }

    const auto delay = Internal::advanceReplyRetryIfNeeded(this, info.error);
    if (delay.has_value()) {
        Internal::scheduleAsyncReplyRetry(QPointer<QCNetworkReply>(q), this, delay.value());
        return;
    }

    setError(info.error, info.message);
    setState(ReplyState::Error);
}

} // namespace QCurl
