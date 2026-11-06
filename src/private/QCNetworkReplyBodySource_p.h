/**
 * @file
 * @brief 封装调用方设备的读取、定位与请求重放规则。
 */

#ifndef QCNETWORKREPLYBODYSOURCE_P_H
#define QCNETWORKREPLYBODYSOURCE_P_H

#include "QCNetworkHttpVersion.h"
#include "private/QCReplyLifecycle_p.h"
#include "private/QCRequestPipeline_p.h"

#include <QPointer>

class QIODevice;

namespace QCurl::Internal {

/** @brief 一次设备读取的结果；Wait 表示暂不可读，不等同于 EOF。 */
enum class ReplyBodyReadKind {
    Data,
    Wait,
    End,
    Error,
};

/** @brief 设备读取结果，只有 Data 的 bytes 表示有效载荷长度。 */
struct ReplyBodyReadResult
{
    ReplyBodyReadKind kind = ReplyBodyReadKind::End;
    qint64 bytes           = 0;
};

/**
 * @brief 借用外部设备的请求体来源，不持有 Reply 或传输驱动。
 *
 * prepare 前由 adapter 完成设备线程检查；本对象不关闭、不接管设备，所有 I/O 在 owner
 * thread 执行。原生 callback 和 readyRead 将结果连接到 flow，不在此处发布信号。
 */
class Q_DECL_HIDDEN QCReplyBodySource
{
public:
    [[nodiscard]] bool prepare(const RequestBody &body,
                               HttpMethod method,
                               QCNetworkHttpVersion version,
                               bool retryEnabled,
                               QString *error);
    [[nodiscard]] ReplyBodyReadResult readInto(char *buffer, qint64 capacity);
    [[nodiscard]] int seek(curl_off_t offset, int origin);
    [[nodiscard]] bool rewindForRetry(QString *error);
    void clearFailure();

    [[nodiscard]] QIODevice *device() const noexcept;
    [[nodiscard]] bool replayable() const noexcept;
    [[nodiscard]] qint64 sizeBytes() const noexcept { return m_sizeBytes; }
    [[nodiscard]] bool readiness() const;
    [[nodiscard]] std::optional<ReplyAttemptErrorInfo> failure() const { return m_failure; }

private:
    [[nodiscard]] bool validateConfiguration(const RequestBody &body,
                                             HttpMethod method,
                                             QCNetworkHttpVersion version,
                                             bool retryEnabled,
                                             QString *error) const;
    void recordFailure(const QString &message);
    [[nodiscard]] std::optional<qint64> seekTarget(curl_off_t offset, int origin);

    QPointer<QIODevice> m_device;
    qint64 m_basePos   = 0;
    qint64 m_sizeBytes = -1;
    qint64 m_bytesRead = 0;
    bool m_seekable    = false;
    std::optional<ReplyAttemptErrorInfo> m_failure;
};

} // namespace QCurl::Internal

#endif // QCNETWORKREPLYBODYSOURCE_P_H
