/**
 * @file
 * @brief 实现调用方设备的读取、定位与请求重放规则。
 */

#include "private/QCNetworkReplyBodySource_p.h"

#include <QIODevice>

#include <cstdio>
#include <limits>

namespace QCurl::Internal {
namespace {

bool fail(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
    return false;
}

std::optional<qint64> checkedPosition(qint64 base, qint64 offset)
{
    if ((offset > 0 && base > std::numeric_limits<qint64>::max() - offset)
        || (offset < 0 && base < std::numeric_limits<qint64>::min() - offset)) {
        return std::nullopt;
    }
    return base + offset;
}

} // namespace

bool QCReplyBodySource::validateConfiguration(const RequestBody &body,
                                              HttpMethod method,
                                              QCNetworkHttpVersion version,
                                              bool retryEnabled,
                                              QString *error) const
{
    if (!body.device) {
        return true;
    }
    if (m_sizeBytes < 0 && !body.allowChunkedPost) {
        return fail(error,
                    QStringLiteral(
                        "request body source: 未指定 sizeBytes，且无法从设备推导长度"
                        "（unknown-size raw body 仅支持 manager-level POST device 入口）"));
    }
    if (m_sizeBytes < 0 && version != QCNetworkHttpVersion::Http1_1) {
        return fail(error,
                    QStringLiteral("request body source: unknown size 的 POST chunked 仅支持 "
                                   "HTTP/1.1（请改为 Http1_1 或指定 sizeBytes）"));
    }
    if (method != HttpMethod::Post && method != HttpMethod::Put) {
        return fail(error, QStringLiteral("request body source 仅支持 PUT/POST（当前方法未支持）"));
    }
    if (retryEnabled && !m_seekable) {
        return fail(error,
                    QStringLiteral("request body source: non-seekable body 不支持自动重试"
                                   "（需要重发 body；请关闭 retryPolicy 或使用 seekable 来源）"));
    }
    return true;
}

bool QCReplyBodySource::prepare(const RequestBody &body,
                                HttpMethod method,
                                QCNetworkHttpVersion version,
                                bool retryEnabled,
                                QString *error)
{
    m_device    = nullptr;
    m_basePos   = 0;
    m_sizeBytes = -1;
    m_bytesRead = 0;
    m_seekable  = false;
    m_failure.reset();

    QIODevice *device = body.device.data();
    if (!device) {
        return true;
    }
    if (!device->isReadable()) {
        return fail(error, QStringLiteral("request body source: 源 QIODevice 不可读"));
    }

    m_device    = device;
    m_basePos   = device->pos();
    m_seekable  = !device->isSequential();
    m_sizeBytes = body.sizeBytes;
    if (m_sizeBytes < 0 && body.inferDeviceSize && m_seekable) {
        const qint64 totalSize = device->size();
        if (totalSize >= m_basePos) {
            m_sizeBytes = totalSize - m_basePos;
        }
    }
    return validateConfiguration(body, method, version, retryEnabled, error);
}

ReplyBodyReadResult QCReplyBodySource::readInto(char *buffer, qint64 capacity)
{
    if (!m_device) {
        recordFailure(QStringLiteral("request body source: 源 QIODevice 在传输中被销毁"));
        return {ReplyBodyReadKind::Error, 0};
    }
    if (!buffer || capacity <= 0) {
        return {ReplyBodyReadKind::End, 0};
    }
    if (!m_device->isReadable()) {
        recordFailure(QStringLiteral("request body source: 源 QIODevice 已不可读"));
        return {ReplyBodyReadKind::Error, 0};
    }

    const qint64 remaining = m_sizeBytes < 0 ? capacity : m_sizeBytes - m_bytesRead;
    if (remaining <= 0) {
        return {ReplyBodyReadKind::End, 0};
    }
    const qint64 amount = qMin(capacity, remaining);
    const qint64 bytes  = m_device->read(buffer, amount);
    if (bytes < 0) {
        recordFailure(
            QStringLiteral("request body source: 读取失败: %1").arg(m_device->errorString()));
        return {ReplyBodyReadKind::Error, 0};
    }
    if (bytes == 0) {
        if (m_device->atEnd()) {
            if (m_sizeBytes >= 0 && m_bytesRead < m_sizeBytes) {
                recordFailure(
                    QStringLiteral("request body source: 数据提前结束（期望剩余 %1 bytes）")
                        .arg(m_sizeBytes - m_bytesRead));
                return {ReplyBodyReadKind::Error, 0};
            }
            return {ReplyBodyReadKind::End, 0};
        }
        return {ReplyBodyReadKind::Wait, 0};
    }
    m_bytesRead += bytes;
    return {ReplyBodyReadKind::Data, bytes};
}

std::optional<qint64> QCReplyBodySource::seekTarget(curl_off_t offset, int origin)
{
    if (!m_device || !m_seekable) {
        return std::nullopt;
    }
    const qint64 off = static_cast<qint64>(offset);
    switch (origin) {
        case SEEK_SET:
            return checkedPosition(m_basePos, off);
        case SEEK_CUR:
            return checkedPosition(m_device->pos(), off);
        case SEEK_END: {
            if (m_sizeBytes < 0) {
                recordFailure(QStringLiteral("request body source: unknown size 不支持 SEEK_END"
                                             "（无法重发 body）"));
                return std::nullopt;
            }
            const auto end = checkedPosition(m_basePos, m_sizeBytes);
            return end ? checkedPosition(*end, off) : std::nullopt;
        }
        default:
            return std::nullopt;
    }
}

int QCReplyBodySource::seek(curl_off_t offset, int origin)
{
    if (!m_device) {
        return CURL_SEEKFUNC_CANTSEEK;
    }
    if (!m_seekable) {
        recordFailure(QStringLiteral("request body source: 无法重发 body：源 QIODevice 不支持 "
                                     "seek（重定向/重试/认证协商）"));
        return CURL_SEEKFUNC_CANTSEEK;
    }
    const auto target = seekTarget(offset, origin);
    if (!target || *target < m_basePos || (m_sizeBytes >= 0 && *target - m_basePos > m_sizeBytes)) {
        return CURL_SEEKFUNC_FAIL;
    }
    if (!m_device->seek(*target)) {
        recordFailure(
            QStringLiteral("request body source: 无法重发 body：seek(%1) 失败").arg(*target));
        return CURL_SEEKFUNC_FAIL;
    }
    m_bytesRead = *target - m_basePos;
    return CURL_SEEKFUNC_OK;
}

bool QCReplyBodySource::rewindForRetry(QString *error)
{
    if (!m_device || !m_seekable) {
        return !m_device
               || fail(error,
                       QStringLiteral("request body source: non-seekable body 不支持自动重试"
                                      "（需要重发 body）"));
    }
    if (!m_device->seek(m_basePos)) {
        return fail(error,
                    QStringLiteral("request body source: 重试需要重发 body：seek(%1) 失败")
                        .arg(m_basePos));
    }
    m_bytesRead = 0;
    return true;
}

QIODevice *QCReplyBodySource::device() const noexcept
{
    return m_device.data();
}

bool QCReplyBodySource::replayable() const noexcept
{
    return !m_device || m_seekable;
}

bool QCReplyBodySource::readiness() const
{
    return m_device && m_device->isReadable()
           && (m_device->bytesAvailable() > 0 || m_device->atEnd());
}

void QCReplyBodySource::recordFailure(const QString &message)
{
    m_failure = ReplyAttemptErrorInfo{NetworkError::InvalidRequest, message};
}

void QCReplyBodySource::clearFailure()
{
    m_failure.reset();
}

} // namespace QCurl::Internal
