/**
 * @file
 * @brief 声明 QCNetworkAccessManager 的内部状态。
 */

#ifndef QCNETWORKACCESSMANAGERPRIVATE_H
#define QCNETWORKACCESSMANAGERPRIVATE_H

#include "QCNetworkAccessManager.h"

#include <QSet>
#include <QSocketNotifier>
#include <QTimer>

#include <curl/curl.h>

namespace QCurl {

class QCNetworkAsyncReply;

/**
 * @brief QCNetworkAccessManager 私有实现（PIMPL）
 *
 * 保存 libcurl multi 句柄与事件驱动相关资源。
 */
class QCNetworkAccessManagerPrivate
{
public:
    /// 构造 access manager 的私有状态对象。
    QCNetworkAccessManagerPrivate(QCNetworkAccessManager *self)
        : cookieFilePath()
        , replyList()
        , curlMultiHandle(nullptr)
        , timer(nullptr)
        , socketDescriptor(CURL_SOCKET_BAD)
        , readNotifier(nullptr)
        , writeNotifier(nullptr)
        , errorNotifier(nullptr)
        , logger(nullptr)
        , middlewares()
        , mockHandler(nullptr)
        , q_ptr(self)
    {}

    /// 释放私有状态对象本身，底层资源由各成员生命周期负责。
    ~QCNetworkAccessManagerPrivate() = default;

    QString cookieFilePath; ///< 共享 cookie 文件路径

    QSet<QCNetworkAsyncReply *> replyList; ///< 当前活跃的异步 reply 集合

    CURLM *curlMultiHandle; ///< 当前 manager 绑定的 multi handle

    QTimer *timer; ///< 驱动 multi timeout 的 Qt 定时器

    curl_socket_t socketDescriptor; ///< 当前监听中的 socket 描述符

    QSocketNotifier *readNotifier; ///< 读事件 notifier

    QSocketNotifier *writeNotifier; ///< 写事件 notifier

    QSocketNotifier *errorNotifier; ///< 错误事件 notifier

    // 高级功能成员
    QCNetworkLogger *logger; ///< 当前注入的日志记录器
    bool debugTraceEnabled = false; ///< 是否启用 debug trace
    QList<QCNetworkMiddleware *> middlewares; ///< 请求/响应中间件链
    QCNetworkMockHandler *mockHandler; ///< 可选的 mock 处理器

    Q_DECLARE_PUBLIC(QCNetworkAccessManager)

private:
    QCNetworkAccessManager *q_ptr;
};

} // namespace QCurl

#endif // QCNETWORKACCESSMANAGERPRIVATE_H
