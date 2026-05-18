/**
 * @file
 * @brief 声明 QCNetworkAccessManager 的内部状态。
 */

#ifndef QCNETWORKACCESSMANAGERPRIVATE_H
#define QCNETWORKACCESSMANAGERPRIVATE_H

#include "QCNetworkAccessManager.h"
#include "private/QCRequestPipeline_p.h"

#include <QSet>
#include <QSocketNotifier>
#include <QTimer>
#include <QFuture>

#include <curl/curl.h>
#include <functional>

namespace QCurl {

class QCNetworkAsyncReply;
class QCNetworkCache;

/**
 * @brief QCNetworkAccessManager 私有实现（PIMPL）
 *
 * 保存 libcurl multi 句柄与事件驱动相关资源。
 */
class QCNetworkAccessManagerPrivate
{
public:
    using ReplyFactory = std::function<QCNetworkReply *()>;

    /// 构造 access manager 的私有状态对象。
    QCNetworkAccessManagerPrivate(QCNetworkAccessManager *self)
        : cookieModeFlag(QCNetworkAccessManager::NotOpen)
        , cookieFilePath()
        , schedulerEnabled(false)
        , cache(nullptr)
        , shareHandleConfig()
        , hstsAltSvcCacheConfig()
        , replyList()
        , curlMultiHandle(nullptr)
        , timer(nullptr)
        , socketDescriptor(CURL_SOCKET_BAD)
        , readNotifier(nullptr)
        , writeNotifier(nullptr)
        , errorNotifier(nullptr)
        , logger(nullptr)
        , q_ptr(self)
    {}

    /// 释放私有状态对象本身，底层资源由各成员生命周期负责。
    ~QCNetworkAccessManagerPrivate() = default;

    [[nodiscard]] QCNetworkRequest prepareManagedRequest(
        const QCNetworkRequest &request, const QList<QCNetworkMiddleware *> &middlewares) const;
    [[nodiscard]] QCCookieOperationResult importCookiesOnOwnerThread(
        const QList<QNetworkCookie> &cookies,
        const QUrl &originUrl);
    [[nodiscard]] QCCookieExportResult exportCookiesOnOwnerThread(
        const QUrl &filterUrl) const;
    [[nodiscard]] QCCookieOperationResult clearAllCookiesOnOwnerThread();
    [[nodiscard]] QFuture<QCCookieOperationResult> runCookieOperationAsync(
        QCNetworkAccessManager *manager,
        std::function<QCCookieOperationResult()> command,
        void (QCNetworkAccessManager::*signal)(
            const QCCookieOperationResult &));
    [[nodiscard]] QFuture<QCCookieExportResult> runCookieExportAsync(
        QCNetworkAccessManager *manager,
        std::function<QCCookieExportResult()> command);
    [[nodiscard]] QCNetworkReply *createPreparedManagedReply(
        const QCNetworkRequest &request,
        HttpMethod method,
        bool async,
        const Internal::RequestBody &requestBodySource,
        const QByteArray &body,
        const QList<QCNetworkMiddleware *> &middlewares);
    void startPreparedReply(QCNetworkReply *reply,
                            const QCNetworkRequest &request,
                            bool async) const;

    QCNetworkAccessManager::CookieFileModeFlag cookieModeFlag; ///< cookie 文件打开模式
    QString cookieFilePath; ///< 共享 cookie 文件路径
    bool schedulerEnabled; ///< 请求调度开关
    QCNetworkCache *cache; ///< 外部注入的缓存实例（manager 不持有所有权）
    QCNetworkAccessManager::ShareHandleConfig shareHandleConfig; ///< share handle 配置
    QCNetworkAccessManager::HstsAltSvcCacheConfig hstsAltSvcCacheConfig; ///< HSTS/Alt-Svc 持久化配置

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
    struct MiddlewareEntry
    {
        QCNetworkMiddleware *middleware = nullptr; ///< 调用方持有，中间件析构时会注销
    };

    QList<MiddlewareEntry> middlewares; ///< 按注册顺序保存的中间件链
    Q_DECLARE_PUBLIC(QCNetworkAccessManager)

    [[nodiscard]] QCNetworkReply *dispatchSendRequest(const QCNetworkRequest &request,
                                                      HttpMethod method,
                                                      bool async,
                                                      const Internal::RequestBody &requestBodySource,
                                                      const QByteArray &body,
                                                      const char *apiName,
                                                      const ReplyFactory &impl);
    [[nodiscard]] QCNetworkReply *dispatchManagedSendRequest(
        const QCNetworkRequest &request,
        HttpMethod method,
        bool async,
        const Internal::RequestBody &requestBodySource,
        const QByteArray &body,
        const char *apiName);
    [[nodiscard]] QCNetworkReply *createReply(const QCNetworkRequest &request,
                                              HttpMethod method,
                                              bool async,
                                              const Internal::RequestBody &requestBodySource,
                                              const QByteArray &body,
                                              QObject *parent);
    [[nodiscard]] QCNetworkReply *createManagedReply(const QCNetworkRequest &request,
                                                     HttpMethod method,
                                                     bool async,
                                                     const Internal::RequestBody &requestBodySource,
                                                     const QByteArray &body,
                                                     const QList<QCNetworkMiddleware *> &middlewares);
    [[nodiscard]] QCNetworkReply *createNoEventLoopErrorReply(
        const QCNetworkRequest &request,
        HttpMethod method,
        const Internal::RequestBody &requestBodySource,
        const QByteArray &body,
        QObject *parent,
        const char *apiName);
    [[nodiscard]] QCNetworkReply *createInvalidRequestReply(const QCNetworkRequest &request,
                                                            HttpMethod method,
                                                            bool async,
                                                            const QString &message,
                                                            QObject *parent);
    void applyReplyDefaults(QCNetworkReply *reply) const;
    void prepareManagedReply(QCNetworkReply *reply,
                             const QList<QCNetworkMiddleware *> &middlewares) const;

private:
    QCNetworkAccessManager *q_ptr;
};

} // namespace QCurl

#endif // QCNETWORKACCESSMANAGERPRIVATE_H
