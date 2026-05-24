// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

/**
 * @file
 * @brief 声明网络 mock 与请求捕获接口。
 */

#ifndef QCNETWORKMOCKHANDLER_H
#define QCNETWORKMOCKHANDLER_H

#include "QCGlobal.h"
#include "QCNetworkError.h"

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QPair>
#include <QScopedPointer>
#include <QSharedDataPointer>
#include <QUrl>

#include <optional>

namespace QCurl {

enum class HttpMethod;

namespace Internal {
struct QCNetworkMockData;
class QCNetworkMockHandlerAccess;
} // namespace Internal

class QCNetworkCapturedRequestData;
class QCNetworkMockHandlerPrivate;

/**
 * @brief 捕获到的请求快照。
 *
 * 这是 Core Test Support 的 accessor-only 值类型，用于离线断言
 * middleware/header/body 形态，不暴露长期 ABI 敏感的 public fields。
 */
class QCURL_EXPORT QCNetworkCapturedRequest
{
public:
    using RawHeaderPair = QPair<QByteArray, QByteArray>;

    QCNetworkCapturedRequest();
    QCNetworkCapturedRequest(const QCNetworkCapturedRequest &other);
    QCNetworkCapturedRequest(QCNetworkCapturedRequest &&other) noexcept;
    ~QCNetworkCapturedRequest();

    QCNetworkCapturedRequest &operator=(const QCNetworkCapturedRequest &other);
    QCNetworkCapturedRequest &operator=(QCNetworkCapturedRequest &&other) noexcept;

    [[nodiscard]] QUrl url() const;
    void setUrl(const QUrl &url);

    [[nodiscard]] HttpMethod method() const;
    void setMethod(HttpMethod method);
    [[nodiscard]] QByteArray customMethod() const;
    void setCustomMethod(const QByteArray &method);

    [[nodiscard]] QList<RawHeaderPair> headers() const;
    void setHeaders(const QList<RawHeaderPair> &headers);
    void addHeader(const QByteArray &name, const QByteArray &value);

    [[nodiscard]] QByteArray bodyPreview() const;
    void setBodyPreview(const QByteArray &bodyPreview);

    [[nodiscard]] qsizetype bodySize() const;
    void setBodySize(qsizetype bodySize);

    [[nodiscard]] bool followLocation() const;
    void setFollowLocation(bool followLocation);

    /// 空值表示请求未显式配置 connect timeout。
    [[nodiscard]] std::optional<qint64> connectTimeoutMs() const;
    void setConnectTimeoutMs(qint64 timeoutMs);
    void clearConnectTimeoutMs();

    /// 空值表示请求未显式配置 total timeout。
    [[nodiscard]] std::optional<qint64> totalTimeoutMs() const;
    void setTotalTimeoutMs(qint64 timeoutMs);
    void clearTotalTimeoutMs();

private:
    QSharedDataPointer<QCNetworkCapturedRequestData> d;
};

/**
 * @brief 网络请求 Mock 处理器。
 *
 * 用于单元测试，模拟网络响应、错误和延迟。
 *
 *
 * @example
 * @code
 * auto *mock = new QCNetworkMockHandler();
 * mock->mockResponse(HttpMethod::Get, url, QByteArray("success response"));
 * mock->mockError(HttpMethod::Get, url, NetworkError::ConnectionRefused);
 * mock->setGlobalDelay(100); // 100ms delay
 *
 * TestSupport::setMockHandler(&manager, mock);
 *
 * // All requests will use mock responses
 * auto *reply = manager->get(request);
 * @endcode
 */
class QCURL_EXPORT QCNetworkMockHandler
{
public:
    using CapturedRequest = QCNetworkCapturedRequest;

    /// 构造 mock 处理器并初始化空的 mock/捕获状态。
    QCNetworkMockHandler();
    /// 销毁 mock 序列与已捕获请求。
    ~QCNetworkMockHandler();

    Q_DISABLE_COPY_MOVE(QCNetworkMockHandler)

    /**
     * @brief 模拟成功响应（指定 HTTP method）
     */
    void mockResponse(HttpMethod method,
                      const QUrl &url,
                      const QByteArray &response,
                      int statusCode                              = 200,
                      const QMap<QByteArray, QByteArray> &headers = QMap<QByteArray, QByteArray>());

    /**
     * @brief 模拟成功响应（注入原始响应头块）
     *
     * 说明：
     * - rawHeaderData 用于覆盖 d->headerData，以便离线门禁验证折叠行/unfold 等语义。
     * - headers 仍可提供（用于便捷构造），但当 rawHeaderData 存在时优先使用 rawHeaderData。
     */
    void mockResponse(HttpMethod method,
                      const QUrl &url,
                      const QByteArray &response,
                      int statusCode,
                      const QMap<QByteArray, QByteArray> &headers,
                      const QByteArray &rawHeaderData);

    /**
     * @brief 追加模拟成功响应（序列）
     * @note 每次命中会推进序列游标；当序列耗尽后复用最后一条。
     */
    void enqueueResponse(
        HttpMethod method,
        const QUrl &url,
        const QByteArray &response,
        int statusCode                              = 200,
        const QMap<QByteArray, QByteArray> &headers = QMap<QByteArray, QByteArray>());

    /**
     * @brief 追加模拟成功响应（序列，注入原始响应头块）
     *
     * 说明同 mockResponse(method, ..., rawHeaderData)。
     */
    void enqueueResponse(HttpMethod method,
                         const QUrl &url,
                         const QByteArray &response,
                         int statusCode,
                         const QMap<QByteArray, QByteArray> &headers,
                         const QByteArray &rawHeaderData);

    /**
     * @brief 模拟错误响应（指定 HTTP method）
     * @note error 用于模拟 libcurl 层网络错误；HTTP 错误推荐用 statusCode>=400 的 mockResponse。
     */
    void mockError(HttpMethod method,
                   const QUrl &url,
                   NetworkError error,
                   const QMap<QByteArray, QByteArray> &headers = QMap<QByteArray, QByteArray>());

    /**
     * @brief 追加模拟错误响应（序列）
     */
    void enqueueError(HttpMethod method,
                      const QUrl &url,
                      NetworkError error,
                      const QMap<QByteArray, QByteArray> &headers = QMap<QByteArray, QByteArray>());

    /**
     * @brief 设置全局模拟延迟
     * @param msecs 延迟时间（毫秒）
     */
    void setGlobalDelay(int msecs);

    /**
     * @brief 获取全局延迟时间
     * @return 延迟时间（毫秒）
     */
    int globalDelay() const;

    /**
     * @brief 检查指定 method+url 是否有模拟。
     */
    bool hasMock(HttpMethod method, const QUrl &url) const;

    /**
     * @brief 获取模拟响应
     * @param url 请求 URL
     * @param statusCode 输出状态码
     * @return 响应数据
     */
    QByteArray getMockResponse(HttpMethod method, const QUrl &url, int &statusCode) const;

    /**
     * @brief 获取模拟错误
     * @param url 请求 URL
     * @return 错误类型
     */
    NetworkError getMockError(HttpMethod method, const QUrl &url) const;

    /**
     * @brief 检查是否为错误模拟
     * @param url 请求 URL
     * @return 如果是错误模拟返回 true
     */
    bool isErrorMock(HttpMethod method, const QUrl &url) const;

    // ==================
    // 请求捕获（用于离线断言：middleware/header 注入、body 形态等）
    // ==================

    /// 启用或关闭请求捕获。
    void setCaptureEnabled(bool enabled);
    /// 返回请求捕获开关状态。
    bool captureEnabled() const;
    /// 设置 body 预览的最大截断字节数。
    void setCaptureBodyPreviewLimit(int bytes);
    /// 返回 body 预览的最大截断字节数。
    int captureBodyPreviewLimit() const;

    /// 记录一次已归一化的请求快照。
    void recordRequest(const CapturedRequest &request);
    /// 返回当前累计的请求快照副本。
    QList<CapturedRequest> capturedRequests() const;
    /// 取走并清空当前累计的请求快照。
    QList<CapturedRequest> takeCapturedRequests();
    /// 清空所有已捕获的请求快照。
    void clearCapturedRequests();

    /**
     * @brief 清空所有模拟
     */
    void clear();

private:
    bool consumeMock(HttpMethod method, const QUrl &url, Internal::QCNetworkMockData &out);

    static QString makeKey(HttpMethod method, const QUrl &url);

    QScopedPointer<QCNetworkMockHandlerPrivate> d_ptr;

    friend class Internal::QCNetworkMockHandlerAccess;
};

} // namespace QCurl

#endif // QCNETWORKMOCKHANDLER_H
