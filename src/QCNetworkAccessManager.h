/**
 * @file
 * @brief 声明网络访问管理器与发送接口。
 */

#ifndef QCNETWORKACCESSMANAGER_H
#define QCNETWORKACCESSMANAGER_H

#include "QCGlobal.h"
#include "QCNetworkHttpMethod.h"

#include <QByteArray>
#include <QNetworkCookie>
#include <QObject>
#include <QScopedPointer>
#include <QSharedDataPointer>
#include <QString>
#include <QUrl>

#include <optional>

class QIODevice;

namespace QCurl {

class QCNetworkBody;
class QCNetworkRequest;
class QCNetworkReply;
class QCNetworkAccessManagerPrivate;
class QCNetworkDownloadToDeviceJob;
class QCNetworkRequestScheduler;
class QCNetworkCache;
class QCNetworkLogger;
class QCNetworkMiddleware;
class QCNetworkMockHandler;
class ShareHandleConfigData;
class HstsAltSvcCacheConfigData;

/**
 * @brief 创建 QCNetworkReply，并持有 manager 级传输策略。
 *
 * manager 保存 cookies、cache、中间件、日志和调度器等共享配置。返回的 reply 遵循
 * QObject 生命周期规则，调用方通常连接信号后通过 deleteLater() 释放。
 */
class QCURL_EXPORT QCNetworkAccessManager : public QObject
{
    Q_OBJECT
public:
    /// 构造网络访问管理器，并确保 libcurl 全局状态已初始化。
    explicit QCNetworkAccessManager(QObject *parent = nullptr);

    /// 析构网络访问管理器；已创建的 reply 仍遵循 Qt 对象生命周期。
    virtual ~QCNetworkAccessManager();

    enum CookieFileModeFlag {
        NotOpen   = 0x0,
        ReadOnly  = 0x1,
        WriteOnly = 0x2,
        ReadWrite = ReadOnly | WriteOnly
    };

    /// 返回当前共享 cookie 文件路径；为空表示未配置。
    QString cookieFilePath() const;

    /// 返回当前 cookie 文件的打开模式。
    CookieFileModeFlag cookieFileMode() const;

    /// 配置共享 cookie 文件路径与打开模式。
    void setCookieFilePath(const QString &cookieFilePath,
                           CookieFileModeFlag flag = CookieFileModeFlag::ReadWrite);

    /// 导入 cookies 到当前 manager 的 cookie store（仅在 shareCookies 开启时可用）。
    bool importCookies(const QList<QNetworkCookie> &cookies,
                       const QUrl &originUrl = QUrl(),
                       QString *error        = nullptr);

    /**
     * @brief 导出当前 manager 的 cookies（仅在 shareCookies 开启时可用）。
     * @param filterUrl 可选过滤 URL；为空时返回当前 share context 可见的全部 cookies。
     * @param error 可选错误输出；返回空值时写入失败原因。
     * @return 空值表示导出失败；空列表表示导出成功但没有匹配 cookie。
     */
    [[nodiscard]] std::optional<QList<QNetworkCookie>> exportCookies(
        const QUrl &filterUrl = QUrl(), QString *error = nullptr) const;

    /// 清空当前 manager 的 cookie store（仅在 shareCookies 开启时可用）。
    bool clearAllCookies(QString *error = nullptr);

    /**
     * @brief 选择当前 manager 共享哪些 libcurl share-handle 域。
     *
     * 共享能力默认关闭，仅在显式启用后影响异步 multi 路径。
     */
    class QCURL_EXPORT ShareHandleConfig
    {
    public:
        ShareHandleConfig();
        ShareHandleConfig(const ShareHandleConfig &other);
        ShareHandleConfig(ShareHandleConfig &&other) noexcept;
        ~ShareHandleConfig();

        ShareHandleConfig &operator=(const ShareHandleConfig &other);
        ShareHandleConfig &operator=(ShareHandleConfig &&other) noexcept;

        [[nodiscard]] bool shareDnsCache() const noexcept;
        void setShareDnsCache(bool enabled);

        [[nodiscard]] bool shareCookies() const noexcept;
        void setShareCookies(bool enabled);

        [[nodiscard]] bool shareSslSession() const noexcept;
        void setShareSslSession(bool enabled);

        [[nodiscard]] bool enabled() const noexcept;

    private:
        QSharedDataPointer<ShareHandleConfigData> d;
    };

    /**
     * @brief 配置持久化 HSTS 与 Alt-Svc cache 文件。
     *
     * 空路径表示禁用对应的 libcurl cache。调用方负责目录生命周期，并决定这些文件是否跨进程复用。
     */
    class QCURL_EXPORT HstsAltSvcCacheConfig
    {
    public:
        HstsAltSvcCacheConfig();
        HstsAltSvcCacheConfig(const HstsAltSvcCacheConfig &other);
        HstsAltSvcCacheConfig(HstsAltSvcCacheConfig &&other) noexcept;
        ~HstsAltSvcCacheConfig();

        HstsAltSvcCacheConfig &operator=(const HstsAltSvcCacheConfig &other);
        HstsAltSvcCacheConfig &operator=(HstsAltSvcCacheConfig &&other) noexcept;

        [[nodiscard]] QString hstsFilePath() const;
        void setHstsFilePath(const QString &path);

        [[nodiscard]] QString altSvcFilePath() const;
        void setAltSvcFilePath(const QString &path);

        [[nodiscard]] bool enabled() const noexcept;

    private:
        QSharedDataPointer<HstsAltSvcCacheConfigData> d;
    };

    /// 配置 multi share handle（默认关闭，仅对异步 multi 路径生效）。
    void setShareHandleConfig(const ShareHandleConfig &config);

    /// 获取当前 share handle 配置。
    [[nodiscard]] ShareHandleConfig shareHandleConfig() const noexcept;

    /// 配置 HSTS/Alt-Svc cache 持久化（默认关闭，显式 opt-in）。
    void setHstsAltSvcCacheConfig(const HstsAltSvcCacheConfig &config);

    /// 获取当前 HSTS/Alt-Svc cache 配置。
    [[nodiscard]] HstsAltSvcCacheConfig hstsAltSvcCacheConfig() const noexcept;

    /// 发送 HEAD 请求（异步），返回需由调用方按 Qt 生命周期释放的 reply。
    QCNetworkReply *sendHead(const QCNetworkRequest &request);

    /// 发送 GET 请求（异步）。
    QCNetworkReply *sendGet(const QCNetworkRequest &request);

    /// 发送 POST 请求（异步，内存请求体）。
    QCNetworkReply *sendPost(const QCNetworkRequest &request, const QByteArray &data);

    /**
     * @brief 发送 POST 请求（异步，QCNetworkBody）。
     *
     * 若 request 未显式设置 `Content-Type`，会使用 body.contentType()；已显式设置时保持
     * request 原值。
     */
    QCNetworkReply *sendPost(const QCNetworkRequest &request, const QCNetworkBody &body);

    /**
     * @brief 发送 POST 请求（异步，借用 QIODevice 作为 raw body）
     *
     * `device` 由调用方持有，manager/reply 不接管所有权；调用方必须保证它在 reply
     * 结束前保持存活且打开为可读。该重载必须从 manager 所在线程调用，且 `device->thread()`
     * 必须与 manager/reply 线程一致；异步请求还要求该线程有 Qt event loop。
     *
     * 读取从调用时设备的当前 `pos()` 开始。未传 `sizeBytes` 时，seekable device 会按
     * `size() - pos()` 推导剩余长度；sequential 或未知长度 POST 仅在 HTTP/1.1 下按
     * chunked raw-body 合同发送。若重定向、重试或认证协商需要重发 body，源设备必须支持
     * seek 回起点，否则请求会失败。
     *
     * 异步 raw-body 遇到 `read() == 0 && !atEnd()` 可等待 `readyRead()` 恢复；同步
     * raw-body 遇到同样状态会 fail-fast。
     */
    QCNetworkReply *sendPost(const QCNetworkRequest &request,
                             QIODevice *device,
                             std::optional<qint64> sizeBytes = std::nullopt);

    /// 发送 PUT 请求（异步，内存请求体）。
    QCNetworkReply *sendPut(const QCNetworkRequest &request, const QByteArray &data);

    /**
     * @brief 发送 PUT 请求（异步，QCNetworkBody）。
     *
     * 若 request 未显式设置 `Content-Type`，会使用 body.contentType()；已显式设置时保持
     * request 原值。
     */
    QCNetworkReply *sendPut(const QCNetworkRequest &request, const QCNetworkBody &body);

    /**
     * @brief 发送 PUT 请求（异步，借用 QIODevice 作为 raw body）
     *
     * 所有权、线程、event loop、当前 `pos()`、seek/replay 与 async/sync
     * source-not-ready 语义同 `sendPost(..., QIODevice *, sizeBytes)`。PUT 必须具有已知
     * 长度：传入非负 `sizeBytes`，或使用可 seek 设备让实现从 `size() - pos()` 推导；未知长度
     * PUT 会 fail-fast。
     */
    QCNetworkReply *sendPut(const QCNetworkRequest &request,
                            QIODevice *device,
                            std::optional<qint64> sizeBytes = std::nullopt);

    /// 发送 DELETE 请求（异步）。
    QCNetworkReply *sendDelete(const QCNetworkRequest &request);

    /// 发送 DELETE 请求（异步，可携带请求体）。
    QCNetworkReply *sendDelete(const QCNetworkRequest &request, const QByteArray &data);

    /// 发送 PATCH 请求（异步）。
    QCNetworkReply *sendPatch(const QCNetworkRequest &request, const QByteArray &data);

    /**
     * @brief 发送 PATCH 请求（异步，QCNetworkBody）。
     *
     * 若 request 未显式设置 `Content-Type`，会使用 body.contentType()；已显式设置时保持
     * request 原值。
     */
    QCNetworkReply *sendPatch(const QCNetworkRequest &request, const QCNetworkBody &body);

    /// 发送 GET 请求（同步，阻塞；不要在 UI 线程调用）。
    QCNetworkReply *sendGetSync(const QCNetworkRequest &request);

    /// 发送 POST 请求（同步，内存请求体）。
    QCNetworkReply *sendPostSync(const QCNetworkRequest &request, const QByteArray &data);

    /**
     * @brief 发送 POST 请求（同步，借用 QIODevice 作为 raw body）
     *
     * 合同同异步 raw-body POST，但调用会阻塞直到完成。Sync raw-body 不支持
     * source-not-ready 恢复：`read() == 0 && !atEnd()` 会作为无效请求失败。
     */
    QCNetworkReply *sendPostSync(const QCNetworkRequest &request,
                                 QIODevice *device,
                                 std::optional<qint64> sizeBytes = std::nullopt);

    /// 发送 PUT 请求（同步，内存请求体）。
    QCNetworkReply *sendPutSync(const QCNetworkRequest &request, const QByteArray &data);

    /**
     * @brief 发送 PUT 请求（同步，借用 QIODevice 作为 raw body）
     *
     * 合同同异步 raw-body PUT，但调用会阻塞直到完成；未知长度 PUT 与 source-not-ready
     * 状态都会 fail-fast。
     */
    QCNetworkReply *sendPutSync(const QCNetworkRequest &request,
                                QIODevice *device,
                                std::optional<qint64> sizeBytes = std::nullopt);

    /// 设置日志记录器（manager 不持有所有权）。
    void setLogger(QCNetworkLogger *logger);

    /// 获取当前日志记录器。
    QCNetworkLogger *logger() const;

    /**
     * @brief 启用/关闭 libcurl verbose/debug trace（强制脱敏）
     *
     * 默认关闭。开启后会通过 CURLOPT_DEBUGFUNCTION 收集 trace，
     * 并对 Authorization/Cookie 等敏感信息做强制脱敏。
     *
     * @note 输出走 QCNetworkLogger；若未设置 logger，则开启不产生输出。
     */
    void setDebugTraceEnabled(bool enabled);

    /// 获取 debug trace 开关状态。
    [[nodiscard]] bool debugTraceEnabled() const noexcept;

    /// 添加请求/响应中间件（manager 不持有所有权）。
    void addMiddleware(QCNetworkMiddleware *middleware);

    /// 移除中间件。
    void removeMiddleware(QCNetworkMiddleware *middleware);

    /// 清空所有中间件。
    void clearMiddlewares();

    /// 获取当前中间件列表。
    QList<QCNetworkMiddleware *> middlewares() const;

    /// 设置 Mock 处理器（manager 不持有所有权）。
    void setMockHandler(QCNetworkMockHandler *handler);

    /// 获取当前 Mock 处理器。
    QCNetworkMockHandler *mockHandler() const;

    /**
     * @brief 启用/禁用请求调度器
     *
     * 启用后，异步 `send*()` 会经过调度器管理；关闭后直接创建并执行 reply。
     *
     * `QCNetworkRequestScheduler` 是“当前线程共享”的 thread-local 实例，而不是
     * `QCNetworkAccessManager` 私有对象。要配置当前 manager 实际使用到的 scheduler，
     * 必须在 manager owner thread 上调用 `scheduler()` / `setConfig()` /
     * `setLaneConfig()` 等接口。
     */
    void enableRequestScheduler(bool enabled);

    /// 检查调度器是否已启用。
    bool isSchedulerEnabled() const;

    /**
     * @brief 获取调度器实例
     *
     * 仅允许在 manager owner thread 上返回当前线程共享的 thread-local scheduler；
     * 本函数不会帮调用方跨线程取回 owner-thread scheduler。
     * 通过本接口拿到的实例，才是当前 manager 的异步 `send*()` 真正会使用的 scheduler。
     *
     * 若从非 owner thread 调用，本函数会给出 warning，并在 debug 构建触发断言，
     * 随后 fail-closed 返回 `nullptr`，避免误用线程懒创建新的 scheduler 实例。
     */
    QCNetworkRequestScheduler *scheduler() const;

    /**
     * @brief 获取 manager owner thread 实际使用的 scheduler 实例
     *
     * 该接口始终返回 manager owner thread 实际使用的 scheduler。
     * 若 owner thread 已具备 Qt event dispatcher，则：
     * - owner thread 调用时直接返回本线程实例；
     * - 非 owner-thread 调用时通过 `Qt::BlockingQueuedConnection`
     *   向 owner thread 取回 scheduler。
     *
     * @warning 该接口在跨线程调用时可能阻塞；避免在持锁状态、析构函数或 UI 热路径中调用。
     * @note 若 owner thread 缺少 Qt event dispatcher，函数会 fail-closed 返回 `nullptr`
     * 并给出 warning；该前置条件对 same-thread 与 cross-thread 调用都成立。
     * @note 跨线程调用时若 marshal 失败，也会返回 `nullptr` 并给出 warning。
     */
    QCNetworkRequestScheduler *schedulerOnOwnerThread() const;

    /**
     * @brief 设置缓存实例
     *
     * 设置后，请求会按各自缓存策略使用该实例。
     * @note manager 不获取缓存所有权，调用方需保证其生命周期
     */
    void setCache(QCNetworkCache *cache);

    /// 获取当前缓存实例。
    QCNetworkCache *cache() const;

private:
    friend class QCNetworkDownloadToDeviceJob;
    friend class QCNetworkResumableDownloadJob;

    Q_DECLARE_PRIVATE(QCNetworkAccessManager)
    QScopedPointer<QCNetworkAccessManagerPrivate> d_ptr;
};

} // namespace QCurl

#endif // QCNETWORKACCESSMANAGER_H
