/**
 * @file
 * @brief 声明 QCNetworkAccessManager 的内部状态。
 */

#ifndef QCNETWORKACCESSMANAGERPRIVATE_H
#define QCNETWORKACCESSMANAGERPRIVATE_H

#include "QCNetworkAccessManager.h"
#include "QCNetworkRequestScheduler.h"
#include "private/QCRequestPipeline_p.h"

#include <QFuture>
#include <QPointer>

#include <functional>

namespace QCurl {

class QCNetworkCache;

/**
 * @brief QCNetworkAccessManager 私有实现（PIMPL）
 *
 * 保存 access manager 的请求、调度和扩展状态。
 */
class QCNetworkAccessManagerPrivate
{
    Q_DECLARE_PUBLIC(QCNetworkAccessManager)

public:
    using ReplyFactory = std::function<QCNetworkReply *()>;

    /// 构造 access manager 的私有状态对象。
    explicit QCNetworkAccessManagerPrivate(QCNetworkAccessManager *self)
        : cookieModeFlag(QCNetworkAccessManager::NotOpen)
        , cookieFilePath()
        , schedulerEnabled(false)
        , scheduler(nullptr)
        , schedulerPolicy(QCNetworkSchedulerPolicy::defaultPolicy())
        , cache(nullptr)
        , shareHandleConfig()
        , hstsAltSvcCacheConfig()
        , logger()
        , q_ptr(self)
    {}

    /// 释放私有状态对象本身，底层资源由各成员生命周期负责。
    ~QCNetworkAccessManagerPrivate() = default;

    [[nodiscard]] QCNetworkRequest prepareManagedRequest(
        const QCNetworkRequest &request, const QList<QCNetworkMiddleware *> &middlewares) const;
    [[nodiscard]] QCCookieOperationResult importCookiesOnOwnerThread(const QList<QCCookie> &cookies,
                                                                     const QUrl &originUrl);
    [[nodiscard]] QCCookieExportResult exportCookiesOnOwnerThread(const QUrl &filterUrl) const;
    [[nodiscard]] QCCookieOperationResult clearAllCookiesOnOwnerThread();
    [[nodiscard]] QFuture<QCCookieOperationResult> runCookieOperationAsync(
        QCNetworkAccessManager *manager, std::function<QCCookieOperationResult()> command);
    [[nodiscard]] QFuture<QCCookieExportResult> runCookieExportAsync(
        QCNetworkAccessManager *manager, std::function<QCCookieExportResult()> command);
    [[nodiscard]] QCNetworkReply *createPreparedManagedReply(
        const QCNetworkRequest &request,
        HttpMethod method,
        const Internal::RequestBody &requestBodySource,
        const QByteArray &body,
        const QList<QCNetworkMiddleware *> &middlewares);
    void startPreparedReply(QCNetworkReply *reply, const QCNetworkRequest &request) const;

    QCNetworkAccessManager::CookieFileModeFlag cookieModeFlag; ///< cookie 文件打开模式
    QString cookieFilePath;                                    ///< 共享 cookie 文件路径
    bool schedulerEnabled;                                     ///< 请求调度开关
    QCNetworkRequestScheduler *scheduler;                      ///< manager 持有的调度器子对象
    QCNetworkSchedulerPolicy schedulerPolicy;                  ///< 当前调度 admission policy
    QPointer<QCNetworkCache> cache; ///< 外部注入的缓存实例（manager 不持有所有权）
    QCNetworkAccessManager::ShareHandleConfig shareHandleConfig;         ///< share handle 配置
    QCNetworkAccessManager::HstsAltSvcCacheConfig hstsAltSvcCacheConfig; ///< HSTS/Alt-Svc 持久化配置

    // 高级功能成员
    QCNetworkLoggerHandle logger;   ///< 当前注入的 opaque 日志句柄
    bool debugTraceEnabled = false; ///< 是否启用 debug trace
    struct MiddlewareEntry
    {
        QCNetworkMiddleware *middleware = nullptr; ///< 调用方持有，中间件析构时会注销
    };

    QList<MiddlewareEntry> middlewares; ///< 按注册顺序保存的中间件链

    [[nodiscard]] QCNetworkReply *dispatchSendRequest(const QCNetworkRequest &request,
                                                      HttpMethod method,
                                                      const Internal::RequestBody &requestBodySource,
                                                      const QByteArray &body,
                                                      const char *apiName,
                                                      const ReplyFactory &impl);
    [[nodiscard]] QCNetworkReply *dispatchManagedSendRequest(
        const QCNetworkRequest &request,
        HttpMethod method,
        const Internal::RequestBody &requestBodySource,
        const QByteArray &body,
        const char *apiName);
    [[nodiscard]] QCNetworkReply *createReply(const QCNetworkRequest &request,
                                              HttpMethod method,
                                              const Internal::RequestBody &requestBodySource,
                                              const QByteArray &body,
                                              QObject *parent);
    [[nodiscard]] QCNetworkReply *createManagedReply(const QCNetworkRequest &request,
                                                     HttpMethod method,
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
                                                            const QString &message,
                                                            QObject *parent);
    void applyReplyDefaults(QCNetworkReply *reply) const;
    void prepareManagedReply(QCNetworkReply *reply,
                             const QList<QCNetworkMiddleware *> &middlewares) const;
    [[nodiscard]] bool rejectOffOwnerThread(QString *error, const char *apiName) const;

private:
    QCNetworkAccessManager *q_ptr;
};

} // namespace QCurl

#endif // QCNETWORKACCESSMANAGERPRIVATE_H
