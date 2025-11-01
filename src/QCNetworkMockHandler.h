// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#ifndef QCNETWORKMOCKHANDLER_H
#define QCNETWORKMOCKHANDLER_H

#include <QUrl>
#include <QByteArray>
#include <QMap>
#include "QCGlobal.h"
#include "QCNetworkError.h"

namespace QCurl {

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
     * @brief 模拟错误响应
     * @param url 请求 URL
     * @param error 错误类型
     */
    void mockError(const QUrl &url, NetworkError error);
    
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
     * @brief 获取模拟响应
     * @param url 请求 URL
     * @param statusCode 输出状态码
     * @return 响应数据
     */
    QByteArray getMockResponse(const QUrl &url, int &statusCode) const;
    
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
    
    /**
     * @brief 清空所有模拟
     */
    void clear();
    
private:
    struct MockData {
        QByteArray response;
        int statusCode = 200;
        NetworkError error = NetworkError::NoError;
        bool isError = false;
    };
    
    QMap<QString, MockData> m_mocks;
    int m_globalDelay = 0;
};

} // namespace QCurl

#endif // QCNETWORKMOCKHANDLER_H
