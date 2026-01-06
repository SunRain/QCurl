// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#ifndef QCNETWORKMOCKHANDLER_H
#define QCNETWORKMOCKHANDLER_H

#include <QUrl>
#include <QByteArray>
#include <QMap>
#include <QList>
#include <QPair>
#include <QMutex>
#include <optional>
#include "QCGlobal.h"
#include "QCNetworkError.h"

namespace QCurl {

enum class HttpMethod;

/**
 * @brief 网络请求 Mock 处理器
 * 
 * 用于单元测试，模拟网络响应、错误和延迟。
 * 
 * 
 * @example
 * @code
 * auto *mock = new QCNetworkMockHandler();
 * mock->mockResponse(url, QByteArray("success response"));
 * mock->mockError(url, NetworkError::ConnectionRefused);
 * mock->setGlobalDelay(100); // 100ms delay
 * 
 * manager->setMockHandler(mock);
 * 
 * // All requests will use mock responses
 * auto *reply = manager->sendGet(request);
 * @endcode
 */
class QCURL_EXPORT QCNetworkMockHandler
{
public:
    QCNetworkMockHandler();
    ~QCNetworkMockHandler();

    struct MockData {
        QByteArray response;
        int statusCode = 200;
        QMap<QByteArray, QByteArray> headers;
        std::optional<QByteArray> rawHeaderData;  ///< 原始响应头数据（可包含折叠行/多 header blocks）
        NetworkError error = NetworkError::NoError;
        bool isError = false;
    };

    struct CapturedRequest {
        QUrl url;
        HttpMethod method;
        QList<QPair<QByteArray, QByteArray>> headers;
        QByteArray bodyPreview;
        qsizetype bodySize = 0;
    };
    
    /**
     * @brief 模拟成功响应
     * @param url 请求 URL
     * @param response 响应数据
     * @param statusCode HTTP 状态码（默认 200）
     */
    void mockResponse(const QUrl &url, 
                     const QByteArray &response, 
                     int statusCode = 200);

    /**
     * @brief 模拟成功响应（指定 HTTP method）
     * @note 优先匹配 method+url；若未配置 method+url，则回退到 url-only（兼容旧 API）。
     */
    void mockResponse(HttpMethod method,
                      const QUrl &url,
                      const QByteArray &response,
                      int statusCode = 200,
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
     * @note 每次命中会“弹出队首”；当序列耗尽后复用最后一条。
     */
    void enqueueResponse(HttpMethod method,
                         const QUrl &url,
                         const QByteArray &response,
                         int statusCode = 200,
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
     * @brief 模拟错误响应
     * @param url 请求 URL
     * @param error 错误类型
     */
    void mockError(const QUrl &url, NetworkError error);

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
     * @brief 检查是否有模拟响应
     * @param url 请求 URL
     * @return 如果有模拟则返回 true
     */
    bool hasMock(const QUrl &url) const;

    /**
     * @brief 检查指定 method+url 是否有模拟（含回退到 url-only）
     */
    bool hasMock(HttpMethod method, const QUrl &url) const;
    
    /**
     * @brief 获取模拟响应
     * @param url 请求 URL
     * @param statusCode 输出状态码
     * @return 响应数据
     */
    QByteArray getMockResponse(const QUrl &url, int &statusCode) const;

    /**
     * @brief 消费一次模拟数据（用于执行链路回放）
     *
     * 命中规则：
     * 1) method+url 精确匹配；
     * 2) 若不存在，则回退到 url-only（旧 API）。
     */
    bool consumeMock(HttpMethod method, const QUrl &url, MockData &out);
    
    /**
     * @brief 获取模拟错误
     * @param url 请求 URL
     * @return 错误类型
     */
    NetworkError getMockError(const QUrl &url) const;
    
    /**
     * @brief 检查是否为错误模拟
     * @param url 请求 URL
     * @return 如果是错误模拟返回 true
     */
    bool isErrorMock(const QUrl &url) const;

    // ========================================================================
    // 请求捕获（用于离线断言：middleware/header 注入、body 形态等）
    // ========================================================================

    void setCaptureEnabled(bool enabled);
    bool captureEnabled() const;
    void setCaptureBodyPreviewLimit(int bytes);
    int captureBodyPreviewLimit() const;

    void recordRequest(const CapturedRequest &request);
    QList<CapturedRequest> capturedRequests() const;
    QList<CapturedRequest> takeCapturedRequests();
    void clearCapturedRequests();
    
    /**
     * @brief 清空所有模拟
     */
    void clear();
    
private:
    struct MockSequence {
        QList<MockData> items;
        int cursor = 0;
    };

    static QString makeKey(const QUrl &url);
    static QString makeKey(HttpMethod method, const QUrl &url);

    static void replaceSequence(MockSequence &seq, const MockData &item);
    static void appendSequence(MockSequence &seq, const MockData &item);

    mutable QMutex m_mutex;
    QMap<QString, MockSequence> m_sequences;
    int m_globalDelay = 0;

    bool m_captureEnabled = false;
    int m_captureBodyPreviewLimitBytes = 4096;
    QList<CapturedRequest> m_capturedRequests;
};

} // namespace QCurl

#endif // QCNETWORKMOCKHANDLER_H
