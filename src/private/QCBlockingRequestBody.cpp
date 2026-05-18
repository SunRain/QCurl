/**
 * @file
 * @brief 实现 Blocking Extras 的请求体读取状态。
 */

#include "private/QCBlockingRequestBody_p.h"

#include <QIODevice>

#include <algorithm>
#include <cstring>

namespace QCurl::Internal {

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
    requestBody.initialDeviceSize = device ? device->size() : -1;
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

} // namespace QCurl::Internal
