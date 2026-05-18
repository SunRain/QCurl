/**
 * @file
 * @brief 声明 Blocking Extras 的请求体读取状态。
 */

#ifndef QCBLOCKINGREQUESTBODY_P_H
#define QCBLOCKINGREQUESTBODY_P_H

#include <QByteArray>
#include <QString>

#include <cstddef>
#include <curl/curl.h>

class QIODevice;

namespace QCurl::Internal {

struct QCBlockingRequestBody
{
    enum class Kind {
        Bytes,
        Device,
    };

    Kind kind = Kind::Bytes;
    const QByteArray *bytes = nullptr;
    QIODevice *device = nullptr;
    qint64 sizeBytes = 0;
    bool explicitSize = false;
    qint64 initialDeviceSize = -1;
};

struct QCBlockingRequestBodyReadState
{
    QCBlockingRequestBody body;
    qint64 offset = 0;
    QString failureMessage;
};

[[nodiscard]] QCBlockingRequestBody makeBlockingBytesBody(const QByteArray &body);
[[nodiscard]] QCBlockingRequestBody makeBlockingDeviceBody(QIODevice *device,
                                                           qint64 sizeBytes,
                                                           bool explicitSize);

[[nodiscard]] bool isStreamingBody(const QCBlockingRequestBody &body) noexcept;
[[nodiscard]] curl_off_t curlBodySize(const QCBlockingRequestBody &body) noexcept;

size_t readBlockingRequestBodyCallback(char *ptr, size_t size, size_t nmemb, void *userdata);

} // namespace QCurl::Internal

#endif // QCBLOCKINGREQUESTBODY_P_H
