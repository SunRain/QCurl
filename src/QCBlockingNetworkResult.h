/**
 * @file
 * @brief 声明 Blocking Extras 的同步请求值结果。
 */

#ifndef QCBLOCKINGNETWORKRESULT_H
#define QCBLOCKINGNETWORKRESULT_H

#include "QCGlobal.h"
#include "QCNetworkError.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QPair>
#include <QSharedDataPointer>
#include <QString>

namespace QCurl {

class QCCookieDelta;
class QCBlockingNetworkResultData;

/**
 * @brief Blocking Extras 使用的非 QObject 同步请求结果。
 *
 * 该类型只表达一次阻塞请求的最终快照：状态码、响应体、响应头和错误。
 * 它不持有 live manager、reply 或事件驱动对象。
 */
class QCURL_EXPORT QCBlockingNetworkResult
{
public:
    using HeaderList = QList<QPair<QByteArray, QByteArray>>;

    QCBlockingNetworkResult();
    QCBlockingNetworkResult(const QCBlockingNetworkResult &other);
    QCBlockingNetworkResult(QCBlockingNetworkResult &&other) noexcept;
    ~QCBlockingNetworkResult();

    QCBlockingNetworkResult &operator=(const QCBlockingNetworkResult &other);
    QCBlockingNetworkResult &operator=(QCBlockingNetworkResult &&other) noexcept;

    [[nodiscard]] static QCBlockingNetworkResult success(int statusCode,
                                                         QByteArray body,
                                                         HeaderList headers = {});
    [[nodiscard]] static QCBlockingNetworkResult success(int statusCode,
                                                         QByteArray body,
                                                         HeaderList headers,
                                                         QCCookieDelta cookieDelta);
    [[nodiscard]] static QCBlockingNetworkResult success(int statusCode,
                                                         QByteArray body,
                                                         HeaderList headers,
                                                         QCCookieDelta cookieDelta,
                                                         qint64 bytesReceived);
    [[nodiscard]] static QCBlockingNetworkResult failure(NetworkError error,
                                                         QString errorMessage,
                                                         int statusCode = 0);

    [[nodiscard]] bool isSuccess() const noexcept;
    [[nodiscard]] NetworkError error() const noexcept;
    [[nodiscard]] QString errorMessage() const;
    [[nodiscard]] int diagnosticCurlCode() const noexcept;
    void setDiagnosticCurlCode(int code) noexcept;
    [[nodiscard]] int statusCode() const noexcept;
    [[nodiscard]] QByteArray body() const;
    /// 返回 canonical 响应头列表，保留重复 header 和接收顺序。
    [[nodiscard]] HeaderList headers() const;
    /// 返回 `headers()` 的兼容别名；新代码优先使用 `headers()`。
    [[nodiscard]] HeaderList rawHeaderList() const;
    /// 返回便捷查找用 header map；重复 header 会按 QHash 语义折叠。
    [[nodiscard]] QHash<QByteArray, QByteArray> rawHeaders() const;
    [[nodiscard]] QCCookieDelta cookieDelta() const;
    [[nodiscard]] qint64 bytesReceived() const noexcept;

private:
    QSharedDataPointer<QCBlockingNetworkResultData> d;
};

} // namespace QCurl

#endif // QCBLOCKINGNETWORKRESULT_H
