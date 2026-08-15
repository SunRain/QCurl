/**
 * @file
 * @brief QCNetworkReply request-body source 状态与 curl 回调实现。
 */

#include "QCCurlMultiManager.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkReply_p.h"
#include "private/QCNetworkReplyBodySource_p.h"
#include "private/QCNetworkReplySignal_p.h"
#include "private/QCNetworkReplyTransferState_p.h"

#include <QIODevice>
#include <QThread>

#include <cstdio>

namespace QCurl::Internal {
namespace {

void setBodySourceError(ReplyBodySourceState &state, NetworkError code, const QString &message)
{
    state.hasErrorOverride     = true;
    state.errorOverrideCode    = code;
    state.errorOverrideMessage = message;
}

bool sourceIsFinished(const QCNetworkReplyTransferState *state)
{
    return !state || state->state == ReplyState::Cancelled || state->state == ReplyState::Error
           || state->state == ReplyState::Finished;
}

size_t pauseBodySourceSend(QCNetworkReplyTransferState *state,
                           const QPointer<QCNetworkReply> &observer)
{
    state->internalPauseMask |= CURLPAUSE_SEND;
    state->appliedPauseMask |= CURLPAUSE_SEND;
    if (!state->uploadSendPaused) {
        state->uploadSendPaused = true;
        if (emitReplySignal(observer,
                            [](QCNetworkReply *reply) {
                                Q_EMIT reply->uploadSendPausedChanged(true);
                            })
            == SignalEmissionResult::Destroyed) {
            return CURL_READFUNC_ABORT;
        }
    }
    return CURL_READFUNC_PAUSE;
}

size_t abortWithReadError(QCNetworkReplyTransferState *state, QIODevice *device)
{
    setBodySourceError(state->requestBodySource,
                       NetworkError::InvalidRequest,
                       QStringLiteral("request body source: 读取失败: %1")
                           .arg(device->errorString()));
    return CURL_READFUNC_ABORT;
}

size_t handleSourceNotReady(QCNetworkReplyTransferState *state,
                            const QPointer<QCNetworkReply> &observer)
{
    return pauseBodySourceSend(state, observer);
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
    auto *multiManager = QCCurlMultiManager::instance();
    if (reply->qObject() && reply->qObject()->thread() != multiManager->thread()) {
        return failBodySourcePrepare(
            errorMessage,
            QStringLiteral(
                "request body source: Reply 线程与 MultiManager 线程不一致，无法安全流式读取"));
    }

    if (reply->qObject() && sourceDevice->thread() != reply->qObject()->thread()) {
        return failBodySourcePrepare(
            errorMessage, QStringLiteral("request body source: 源 QIODevice 与 Reply 不在同一线程"));
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
            QStringLiteral(
                "request body source: unknown size 的 POST chunked 仅支持 HTTP/1.1（请改为 "
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
    state.device   = sourceDevice;
    state.basePos  = sourceDevice->pos();
    state.seekable = !sourceDevice->isSequential();
    resolveBodySourceSize(state, bodySpec, sourceDevice);
}

size_t readUnknownSizeBody(char *ptr,
                           size_t totalSize,
                           QCNetworkReplyTransferState *transferState,
                           const QPointer<QCNetworkReply> &observer,
                           QIODevice *device,
                           ReplyBodySourceState &state)
{
    const qint64 n = device->read(ptr, static_cast<qint64>(totalSize));
    if (n < 0) {
        return abortWithReadError(transferState, device);
    }
    if (n == 0) {
        return device->atEnd() ? 0 : handleSourceNotReady(transferState, observer);
    }

    state.bytesRead += n;
    return static_cast<size_t>(n);
}

size_t readKnownSizeBody(char *ptr,
                         size_t totalSize,
                         QCNetworkReplyTransferState *transferState,
                         const QPointer<QCNetworkReply> &observer,
                         QIODevice *device,
                         ReplyBodySourceState &state)
{
    const qint64 remaining = state.sizeBytes - state.bytesRead;
    if (remaining <= 0) {
        return 0;
    }

    const qint64 want = qMin(static_cast<qint64>(totalSize), remaining);
    const qint64 n    = device->read(ptr, want);
    if (n < 0) {
        return abortWithReadError(transferState, device);
    }
    if (n > 0) {
        state.bytesRead += n;
        return static_cast<size_t>(n);
    }
    if (!device->atEnd()) {
        return handleSourceNotReady(transferState, observer);
    }

    setBodySourceError(state,
                       NetworkError::InvalidRequest,
                       QStringLiteral("request body source: 数据提前结束（期望剩余 %1 bytes）")
                           .arg(remaining));
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
                    QStringLiteral(
                        "request body source: unknown size 不支持 SEEK_END（无法重发 body）"));
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
    state.device    = nullptr;
    state.basePos   = 0;
    state.sizeBytes = -1;
    state.bytesRead = 0;
    state.seekable  = false;
    clearReplyBodySourceError(state);
}

void clearReplyBodySourceError(ReplyBodySourceState &state)
{
    state.hasErrorOverride  = false;
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

size_t readReplyBodySourceCallback(char *ptr,
                                   size_t size,
                                   size_t nmemb,
                                   QCNetworkReplyTransferState *state,
                                   const QPointer<QCNetworkReply> &observer)
{
    if (!state || !observer) {
        return CURL_READFUNC_ABORT;
    }

    if (state->state == ReplyState::Cancelled || state->state == ReplyState::Error) {
        return CURL_READFUNC_ABORT;
    }

    auto &bodyState   = state->requestBodySource;
    QIODevice *device = bodyState.device.data();
    if (!device) {
        setBodySourceError(bodyState,
                           NetworkError::InvalidRequest,
                           QStringLiteral("request body source: 源 QIODevice 在传输中被销毁"));
        return CURL_READFUNC_ABORT;
    }

    if (!device->isReadable()) {
        setBodySourceError(bodyState,
                           NetworkError::InvalidRequest,
                           QStringLiteral("request body source: 源 QIODevice 已不可读"));
        return CURL_READFUNC_ABORT;
    }

    const size_t totalSize = size * nmemb;
    if (totalSize == 0) {
        return 0;
    }

    if (bodyState.sizeBytes < 0) {
        return readUnknownSizeBody(ptr, totalSize, state, observer, device, bodyState);
    }
    return readKnownSizeBody(ptr, totalSize, state, observer, device, bodyState);
}

int seekReplyBodySourceCallback(QCNetworkReplyTransferState *state, curl_off_t offset, int origin)
{
    if (sourceIsFinished(state)) {
        return CURL_SEEKFUNC_FAIL;
    }

    auto &bodyState   = state->requestBodySource;
    QIODevice *device = bodyState.device.data();
    if (!device) {
        return CURL_SEEKFUNC_CANTSEEK;
    }

    if (!bodyState.seekable) {
        setBodySourceError(bodyState,
                           NetworkError::InvalidRequest,
                           QStringLiteral("request body source: 无法重发 body：源 QIODevice 不支持 "
                                          "seek（重定向/重试/认证协商）"));
        return CURL_SEEKFUNC_CANTSEEK;
    }

    qint64 targetPos = -1;
    if (!resolveBodySourceSeekTarget(bodyState, device, offset, origin, &targetPos)) {
        return CURL_SEEKFUNC_FAIL;
    }

    if (!bodySourceSeekTargetInRange(bodyState, targetPos)) {
        return CURL_SEEKFUNC_FAIL;
    }
    if (!device->seek(targetPos)) {
        setBodySourceError(bodyState,
                           NetworkError::InvalidRequest,
                           QStringLiteral("request body source: 无法重发 body：seek(%1) 失败")
                               .arg(targetPos));
        return CURL_SEEKFUNC_FAIL;
    }

    bodyState.bytesRead = targetPos - bodyState.basePos;
    return CURL_SEEKFUNC_OK;
}

} // namespace QCurl::Internal
