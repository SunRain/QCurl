/**
 * @file
 * @brief 实现 Blocking Extras 的请求体读取状态。
 */

#include "private/QCBlockingRequestBody_p.h"

#include <QIODevice>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace QCurl::Internal {
namespace {

qint64 resolveBytesSeekOffset(const QCBlockingRequestBodyReadState &state,
                              curl_off_t offset,
                              int origin)
{
    switch (origin) {
        case SEEK_SET:
            return static_cast<qint64>(offset);
        case SEEK_CUR:
            return state.offset + static_cast<qint64>(offset);
        case SEEK_END:
            return state.body.sizeBytes + static_cast<qint64>(offset);
        default:
            return -1;
    }
}

qint64 resolveDeviceSeekPosition(const QCBlockingRequestBodyReadState &state,
                                 const QIODevice *device,
                                 curl_off_t offset,
                                 int origin)
{
    switch (origin) {
        case SEEK_SET:
            return state.body.basePosition + static_cast<qint64>(offset);
        case SEEK_CUR:
            return device->pos() + static_cast<qint64>(offset);
        case SEEK_END:
            return state.body.basePosition + state.body.sizeBytes + static_cast<qint64>(offset);
        default:
            return -1;
    }
}

bool isDeviceSeekPositionInRange(const QCBlockingRequestBodyReadState &state, qint64 targetPosition)
{
    const qint64 endPosition = state.body.basePosition + state.body.sizeBytes;
    return targetPosition >= state.body.basePosition && targetPosition <= endPosition;
}

} // namespace

QCBlockingRequestBody makeBlockingBytesBody(const QByteArray &body)
{
    QCBlockingRequestBody requestBody;
    requestBody.kind = QCBlockingRequestBody::Kind::Bytes;
    requestBody.bytes = &body;
    requestBody.sizeBytes = body.size();
    requestBody.explicitSize = true;
    return requestBody;
}

QCBlockingRequestBody makeBlockingDeviceBody(QIODevice *device, qint64 sizeBytes, bool explicitSize)
{
    QCBlockingRequestBody requestBody;
    requestBody.kind = QCBlockingRequestBody::Kind::Device;
    requestBody.device = device;
    requestBody.sizeBytes = sizeBytes;
    requestBody.explicitSize = explicitSize;
    requestBody.basePosition = device ? device->pos() : 0;
    requestBody.initialDeviceSize = device ? device->size() : -1;
    requestBody.seekable = device && !device->isSequential();
    return requestBody;
}

bool isStreamingBody(const QCBlockingRequestBody &body) noexcept
{
    return body.kind == QCBlockingRequestBody::Kind::Device;
}

curl_off_t curlBodySize(const QCBlockingRequestBody &body) noexcept
{
    return static_cast<curl_off_t>(body.sizeBytes);
}

size_t readBlockingRequestBodyCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *state = static_cast<QCBlockingRequestBodyReadState *>(userdata);
    if (!state || !ptr) {
        return CURL_READFUNC_ABORT;
    }

    const auto capacity = static_cast<qint64>(size * nmemb);
    const qint64 remaining = state->body.sizeBytes - state->offset;
    const qint64 amount = std::min(capacity, remaining);
    if (amount <= 0) {
        return 0;
    }

    if (state->body.kind == QCBlockingRequestBody::Kind::Bytes) {
        if (!state->body.bytes) {
            state->failureMessage = QStringLiteral("Blocking Extras request body is missing");
            return CURL_READFUNC_ABORT;
        }
        std::memcpy(ptr,
                    state->body.bytes->constData() + state->offset,
                    static_cast<size_t>(amount));
        state->offset += amount;
        return static_cast<size_t>(amount);
    }

    QIODevice *device = state->body.device;
    if (!device) {
        state->failureMessage = QStringLiteral("Blocking Extras raw body device is missing");
        return CURL_READFUNC_ABORT;
    }
    if (!state->body.explicitSize && state->body.initialDeviceSize >= 0
        && device->size() != state->body.initialDeviceSize) {
        state->failureMessage =
            QStringLiteral("Blocking Extras raw body device size changed during upload");
        return CURL_READFUNC_ABORT;
    }

    const qint64 read = device->read(ptr, amount);
    if (read < 0) {
        state->failureMessage =
            QStringLiteral("Blocking Extras raw body device read failed during upload");
        return CURL_READFUNC_ABORT;
    }
    if (read == 0 && !device->atEnd()) {
        state->failureMessage =
            QStringLiteral("Blocking Extras raw body device is not ready for synchronous upload");
        return CURL_READFUNC_ABORT;
    }

    state->offset += read;
    return static_cast<size_t>(read);
}

Q_DECL_HIDDEN int seekBlockingRequestBodyCallback(void *userdata, curl_off_t offset, int origin)
{
    auto *state = static_cast<QCBlockingRequestBodyReadState *>(userdata);
    if (!state) {
        return CURL_SEEKFUNC_FAIL;
    }

    if (state->body.kind == QCBlockingRequestBody::Kind::Bytes) {
        const qint64 targetOffset = resolveBytesSeekOffset(*state, offset, origin);
        if (targetOffset < 0 || targetOffset > state->body.sizeBytes) {
            return CURL_SEEKFUNC_FAIL;
        }
        state->offset = targetOffset;
        return CURL_SEEKFUNC_OK;
    }

    QIODevice *device = state->body.device;
    if (!device || !state->body.seekable) {
        state->failureMessage =
            QStringLiteral("Blocking Extras raw body device does not support replay seek");
        return CURL_SEEKFUNC_CANTSEEK;
    }

    const qint64 targetPosition = resolveDeviceSeekPosition(*state, device, offset, origin);
    if (!isDeviceSeekPositionInRange(*state, targetPosition)) {
        state->failureMessage =
            QStringLiteral("Blocking Extras raw body replay seek target is out of range");
        return CURL_SEEKFUNC_FAIL;
    }
    if (!device->seek(targetPosition)) {
        state->failureMessage = QStringLiteral("Blocking Extras raw body replay seek failed");
        return CURL_SEEKFUNC_FAIL;
    }

    state->offset = targetPosition - state->body.basePosition;
    return CURL_SEEKFUNC_OK;
}

} // namespace QCurl::Internal
