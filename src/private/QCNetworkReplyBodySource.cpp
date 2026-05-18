/**
 * @file
 * @brief QCNetworkReply request-body source 状态与 curl 回调实现。
 */

#include "private/QCNetworkReplyBodySource_p.h"

#include "QCCurlMultiManager.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkReply_p.h"

#include <QIODevice>
#include <QThread>

#include <cstdio>

namespace QCurl::Internal {
namespace {

void setBodySourceError(ReplyBodySourceState &state, NetworkError code, const QString &message)
{
    state.hasErrorOverride = true;
    state.errorOverrideCode = code;
    state.errorOverrideMessage = message;
}

bool sourceIsFinished(const QCNetworkReplyPrivate *reply)
{
    return !reply || reply->state == ReplyState::Cancelled || reply->state == ReplyState::Error
        || reply->state == ReplyState::Finished;
}

size_t pauseBodySourceSend(QCNetworkReplyPrivate *reply)
{
    reply->internalPauseMask |= CURLPAUSE_SEND;
    reply->appliedPauseMask |= CURLPAUSE_SEND;
    reply->setUploadSendPaused(true);
    return CURL_READFUNC_PAUSE;
}

size_t abortWithReadError(QCNetworkReplyPrivate *reply, QIODevice *device)
{
    setBodySourceError(reply->requestBodySource,
                       NetworkError::InvalidRequest,
                       QStringLiteral("request body source: 读取失败: %1")
                           .arg(device->errorString()));
    return CURL_READFUNC_ABORT;
}

size_t handleSourceNotReady(QCNetworkReplyPrivate *reply)
{
    if (reply->executionMode == ExecutionMode::Sync) {
        setBodySourceError(
            reply->requestBodySource,
            NetworkError::InvalidRequest,
            QStringLiteral("request body source: Sync raw-body 不支持 source-not-ready 恢复"));
        return CURL_READFUNC_ABORT;
    }

    return pauseBodySourceSend(reply);
}

bool failBodySourcePrepare(QString *errorMessage, const QString &message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
    return false;
}

bool validateBodySourceThreads(const QCNetworkReplyPrivate *reply,
                               const QIODevice *sourceDevice,
                               QString *errorMessage)
{
    if (reply->executionMode == ExecutionMode::Async) {
        auto *multiManager = QCCurlMultiManager::instance();
        if (reply->q_ptr && reply->q_ptr->thread() != multiManager->thread()) {
            return failBodySourcePrepare(
                errorMessage,
                QStringLiteral(
                    "request body source: Reply 线程与 MultiManager 线程不一致，无法安全流式读取"));
        }
    }

    if (reply->q_ptr && sourceDevice->thread() != reply->q_ptr->thread()) {
        return failBodySourcePrepare(
            errorMessage,
            QStringLiteral("request body source: 源 QIODevice 与 Reply 不在同一线程"));
    }

    return true;
}

bool validateUnknownBodySize(const QCNetworkReplyPrivate *reply,
                             const RequestBody &bodySpec,
                             QString *errorMessage)
{
    if (!bodySpec.allowChunkedPost) {
        return failBodySourcePrepare(
            errorMessage,
            QStringLiteral(
                "request body source: 未指定 sizeBytes，且无法从设备推导长度（unknown-size raw "
                "body 仅支持 manager-level POST device 入口）"));
    }

    if (reply->request.httpVersion() != QCNetworkHttpVersion::Http1_1) {
        return failBodySourcePrepare(
            errorMessage,
            QStringLiteral("request body source: unknown size 的 POST chunked 仅支持 HTTP/1.1（请改为 "
                           "Http1_1 或指定 sizeBytes）"));
    }

    return true;
}

void resolveBodySourceSize(ReplyBodySourceState &state,
                           const RequestBody &bodySpec,
                           const QIODevice *sourceDevice)
{
    state.sizeBytes = bodySpec.sizeBytes;
    if (state.sizeBytes >= 0 || !bodySpec.inferDeviceSize || !state.seekable) {
        return;
    }

    const qint64 totalSize = sourceDevice->size();
    if (totalSize >= 0 && totalSize >= state.basePos) {
        state.sizeBytes = totalSize - state.basePos;
    }
}

void initializeBodySourceState(ReplyBodySourceState &state,
                               const RequestBody &bodySpec,
                               QIODevice *sourceDevice)
{
    state.device = sourceDevice;
    state.basePos = sourceDevice->pos();
    state.seekable = !sourceDevice->isSequential();
    resolveBodySourceSize(state, bodySpec, sourceDevice);
}

size_t readUnknownSizeBody(char *ptr,
                           size_t totalSize,
                           QCNetworkReplyPrivate *reply,
                           QIODevice *device,
                           ReplyBodySourceState &state)
{
    const qint64 n = device->read(ptr, static_cast<qint64>(totalSize));
    if (n < 0) {
        return abortWithReadError(reply, device);
    }
    if (n == 0) {
        return device->atEnd() ? 0 : handleSourceNotReady(reply);
    }

    state.bytesRead += n;
    return static_cast<size_t>(n);
}

size_t readKnownSizeBody(char *ptr,
                         size_t totalSize,
                         QCNetworkReplyPrivate *reply,
                         QIODevice *device,
                         ReplyBodySourceState &state)
{
    const qint64 remaining = state.sizeBytes - state.bytesRead;
    if (remaining <= 0) {
        return 0;
    }

    const qint64 want = qMin(static_cast<qint64>(totalSize), remaining);
    const qint64 n = device->read(ptr, want);
    if (n < 0) {
        return abortWithReadError(reply, device);
    }
    if (n > 0) {
        state.bytesRead += n;
        return static_cast<size_t>(n);
    }
    if (!device->atEnd()) {
        return handleSourceNotReady(reply);
    }

    setBodySourceError(
        state,
        NetworkError::InvalidRequest,
        QStringLiteral("request body source: 数据提前结束（期望剩余 %1 bytes）").arg(remaining));
    return CURL_READFUNC_ABORT;
}

bool resolveBodySourceSeekTarget(ReplyBodySourceState &state,
                                 QIODevice *device,
                                 curl_off_t offset,
                                 int origin,
                                 qint64 *targetPos)
{
    const qint64 off = static_cast<qint64>(offset);
    switch (origin) {
        case SEEK_SET:
            *targetPos = state.basePos + off;
            return true;
        case SEEK_CUR:
            *targetPos = device->pos() + off;
            return true;
        case SEEK_END:
            if (state.sizeBytes < 0) {
                setBodySourceError(
                    state,
                    NetworkError::InvalidRequest,
                    QStringLiteral("request body source: unknown size 不支持 SEEK_END（无法重发 body）"));
                return false;
            }
            *targetPos = state.basePos + state.sizeBytes + off;
            return true;
        default:
            return false;
    }
}

bool bodySourceSeekTargetInRange(const ReplyBodySourceState &state, qint64 targetPos)
{
    if (targetPos < state.basePos) {
        return false;
    }
    return state.sizeBytes < 0 || targetPos <= (state.basePos + state.sizeBytes);
}

} // namespace

void resetReplyBodySource(ReplyBodySourceState &state)
{
    state.device = nullptr;
    state.basePos = 0;
    state.sizeBytes = -1;
    state.bytesRead = 0;
    state.seekable = false;
    clearReplyBodySourceError(state);
}

void clearReplyBodySourceError(ReplyBodySourceState &state)
{
    state.hasErrorOverride = false;
    state.errorOverrideCode = NetworkError::NoError;
    state.errorOverrideMessage.clear();
}

bool hasReplyBodySourceError(const ReplyBodySourceState &state) noexcept
{
    return state.hasErrorOverride;
}

NetworkError replyBodySourceErrorCode(const ReplyBodySourceState &state) noexcept
{
    return state.errorOverrideCode;
}

QString replyBodySourceErrorMessage(const ReplyBodySourceState &state)
{
    return state.errorOverrideMessage;
}

bool prepareReplyBodySource(QCNetworkReplyPrivate *reply,
                            const RequestBody &bodySpec,
                            QString *errorMessage)
{
    if (!reply) {
        return failBodySourcePrepare(errorMessage,
                                     QStringLiteral("request body source: Reply 状态不可用"));
    }

    auto &state = reply->requestBodySource;
    resetReplyBodySource(state);

    QIODevice *sourceDevice = bodySpec.device.data();
    if (!sourceDevice) {
        return true;
    }

    if (!validateBodySourceThreads(reply, sourceDevice, errorMessage)) {
        return false;
    }

    if (!sourceDevice->isReadable()) {
        return failBodySourcePrepare(errorMessage,
                                     QStringLiteral("request body source: 源 QIODevice 不可读"));
    }

    initializeBodySourceState(state, bodySpec, sourceDevice);
    return state.sizeBytes >= 0 || validateUnknownBodySize(reply, bodySpec, errorMessage);
}

bool rewindReplyBodySourceForRetry(QCNetworkReplyPrivate *reply, QString *errorMessage)
{
    if (!reply || !reply->requestBodySource.device || reply->attemptCount <= 0) {
        return true;
    }

    auto &state = reply->requestBodySource;
    if (!state.seekable) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "request body source: non-seekable body 不支持自动重试（需要重发 body）");
        }
        return false;
    }

    if (!state.device->seek(state.basePos)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("request body source: 重试需要重发 body：seek(%1) 失败")
                                .arg(state.basePos);
        }
        return false;
    }

    state.bytesRead = 0;
    return true;
}

size_t readReplyBodySourceCallback(char *ptr, size_t size, size_t nmemb, QCNetworkReplyPrivate *reply)
{
    if (!reply || !reply->q_ptr) {
        return CURL_READFUNC_ABORT;
    }

    if (reply->state == ReplyState::Cancelled || reply->state == ReplyState::Error) {
        return CURL_READFUNC_ABORT;
    }

    auto &state = reply->requestBodySource;
    QIODevice *device = state.device.data();
    if (!device) {
        setBodySourceError(state,
                           NetworkError::InvalidRequest,
                           QStringLiteral("request body source: 源 QIODevice 在传输中被销毁"));
        return CURL_READFUNC_ABORT;
    }

    if (!device->isReadable()) {
        setBodySourceError(state,
                           NetworkError::InvalidRequest,
                           QStringLiteral("request body source: 源 QIODevice 已不可读"));
        return CURL_READFUNC_ABORT;
    }

    const size_t totalSize = size * nmemb;
    if (totalSize == 0) {
        return 0;
    }

    if (state.sizeBytes < 0) {
        return readUnknownSizeBody(ptr, totalSize, reply, device, state);
    }
    return readKnownSizeBody(ptr, totalSize, reply, device, state);
}

int seekReplyBodySourceCallback(QCNetworkReplyPrivate *reply, curl_off_t offset, int origin)
{
    if (!reply || !reply->q_ptr || sourceIsFinished(reply)) {
        return CURL_SEEKFUNC_FAIL;
    }

    auto &state = reply->requestBodySource;
    QIODevice *device = state.device.data();
    if (!device) {
        return CURL_SEEKFUNC_CANTSEEK;
    }

    if (!state.seekable) {
        setBodySourceError(
            state,
            NetworkError::InvalidRequest,
            QStringLiteral(
                "request body source: 无法重发 body：源 QIODevice 不支持 seek（重定向/重试/认证协商）"));
        return CURL_SEEKFUNC_CANTSEEK;
    }

    qint64 targetPos = -1;
    if (!resolveBodySourceSeekTarget(state, device, offset, origin, &targetPos)) {
        return CURL_SEEKFUNC_FAIL;
    }

    if (!bodySourceSeekTargetInRange(state, targetPos)) {
        return CURL_SEEKFUNC_FAIL;
    }
    if (!device->seek(targetPos)) {
        setBodySourceError(
            state,
            NetworkError::InvalidRequest,
            QStringLiteral("request body source: 无法重发 body：seek(%1) 失败").arg(targetPos));
        return CURL_SEEKFUNC_FAIL;
    }

    state.bytesRead = targetPos - state.basePos;
    return CURL_SEEKFUNC_OK;
}

} // namespace QCurl::Internal
