/**
 * @file
 * @brief 声明网络访问管理器与发送接口。
 */

#ifndef QCNETWORKACCESSMANAGER_H
#define QCNETWORKACCESSMANAGER_H

#include "QCGlobal.h"
#include "QCNetworkHttpMethod.h"

#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QNetworkCookie>
#include <QObject>
#include <QScopedPointer>
#include <QUrl>

#include <functional>

class QTimer;
class QSocketNotifier;

namespace QCurl {

class QCNetworkRequest;
class QCNetworkReply;
class QCNetworkAccessManagerPrivate;
class QCNetworkRequestScheduler;
class QCMultipartFormData;
class QCNetworkCache;
class QCNetworkLogger;
class QCNetworkMiddleware;
class QCNetworkMockHandler;
class QCURL_EXPORT QCNetworkAccessManager : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief 构造网络访问管理器
     *
     * 构造时会确保 libcurl 全局状态已初始化，但不会主动创建请求对象。
     */
    explicit QCNetworkAccessManager(QObject *parent = nullptr);

    /**
     * @brief 析构网络访问管理器
     *
     * 已创建的 reply 仍遵循 Qt 对象树和 deleteLater() 生命周期。
     */
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

    /**
     * @brief 配置共享 cookie 文件路径与打开模式
     *
     * 该设置只更新 manager 持有的配置；是否实际参与请求取决于请求路径
     * 和 cookie share 配置。
     */
    void setCookieFilePath(const QString &cookieFilePath,
                           CookieFileModeFlag flag = CookieFileModeFlag::ReadWrite);

    // ==================
    // Cookie bridge
    // ==================

    /**
     * @brief 导入 cookies 到当前 manager 的 cookie store（仅在 shareCookies 开启时可用）
     *
     * 该 API 不依赖 Qt WebEngine。若 cookie 的 domain/path 为空且提供
     * originUrl，会使用 originUrl.host() 与 "/" 补全。
     *
     * @param cookies 需要导入的 cookies
     * @param originUrl 可选的来源 URL，用于补全 domain/path
     * @param error 可选：失败原因输出
     * @return bool true 表示导入成功
     */
    bool importCookies(const QList<QNetworkCookie> &cookies,
                       const QUrl &originUrl = QUrl(),
                       QString *error        = nullptr);

    /**
     * @brief 导出当前 manager 的 cookies（仅在 shareCookies 开启时可用）
     *
     * @param filterUrl 可选的过滤 URL，按 host/path 收敛结果
     * @param error 可选：失败原因输出
     * @return QList<QNetworkCookie> 导出的 cookies（失败返回空列表）
     */
    [[nodiscard]] QList<QNetworkCookie> exportCookies(const QUrl &filterUrl = QUrl(),
                                                      QString *error        = nullptr) const;

    /**
     * @brief 清空当前 manager 的 cookie store（仅在 shareCookies 开启时可用）
     *
     * @param error 可选：失败原因输出
     * @return bool true 表示清空成功
     */
    bool clearAllCookies(QString *error = nullptr);

    struct ShareHandleConfig
    {
        bool shareDnsCache   = false; ///< 是否共享 DNS cache
        bool shareCookies    = false; ///< 是否共享 cookies
        bool shareSslSession = false; ///< 是否共享 SSL session

        /// 返回是否至少启用了一个 share 维度。
        [[nodiscard]] bool enabled() const noexcept
        {
            return shareDnsCache || shareCookies || shareSslSession;
        }
    };

    struct HstsAltSvcCacheConfig
    {
        QString hstsFilePath;   ///< HSTS cache 文件路径
        QString altSvcFilePath; ///< Alt-Svc cache 文件路径

        /// 返回是否配置了任一持久化 cache 文件。
        [[nodiscard]] bool enabled() const noexcept
        {
            return !hstsFilePath.isEmpty() || !altSvcFilePath.isEmpty();
        }
    };

    /**
     * @brief 配置 multi share handle（默认关闭）
     *
     * 仅对异步 multi 路径生效。未显式开启时不会创建 CURLSH*，也不会
     * 为请求设置 CURLOPT_SHARE。
     */
    void setShareHandleConfig(const ShareHandleConfig &config);

    /**
     * @brief 获取当前 share handle 配置
     */
    [[nodiscard]] ShareHandleConfig shareHandleConfig() const noexcept;

    /**
     * @brief 配置 HSTS/Alt-Svc cache 持久化（默认关闭，显式 opt-in）
     *
     * 未显式设置时不会配置 CURLOPT_HSTS/CURLOPT_ALTSVC。缓存路径由调用方
     * 提供，并由调用方负责目录生命周期。
     */
    void setHstsAltSvcCacheConfig(const HstsAltSvcCacheConfig &config);

    /**
     * @brief 获取当前 HSTS/Alt-Svc cache 配置
     */
    [[nodiscard]] HstsAltSvcCacheConfig hstsAltSvcCacheConfig() const noexcept;

    // ==================
    // 核心 API：返回统一的 QCNetworkReply
    // ==================

    /**
     * @brief 发送 HEAD 请求（异步）
     * @param request 请求配置
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     * @note 默认会立即启动；若已启用请求调度器，则按最终 request.priority() 排队后自动启动
     */
    QCNetworkReply *sendHead(const QCNetworkRequest &request);

    /**
     * @brief 发送 GET 请求（异步）
     * @param request 请求配置
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     * @note 默认会立即启动；若已启用请求调度器，则按最终 request.priority() 排队后自动启动
     */
    QCNetworkReply *sendGet(const QCNetworkRequest &request);

    /**
     * @brief 发送 POST 请求（异步）
     * @param request 请求配置
     * @param data 请求体数据
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     * @note 默认会立即启动；若已启用请求调度器，则按最终 request.priority() 排队后自动启动
     */
    QCNetworkReply *sendPost(const QCNetworkRequest &request, const QByteArray &data);

    /**
     * @brief 发送 PUT 请求（异步）
     * @param request 请求配置
     * @param data 请求体数据
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     * @note 默认会立即启动；若已启用请求调度器，则按最终 request.priority() 排队后自动启动
     */
    QCNetworkReply *sendPut(const QCNetworkRequest &request, const QByteArray &data);

    /**
     * @brief 发送 DELETE 请求（异步）
     * @param request 请求配置
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     * @note 默认会立即启动；若已启用请求调度器，则按最终 request.priority() 排队后自动启动
     */
    QCNetworkReply *sendDelete(const QCNetworkRequest &request);

    /**
     * @brief 发送 DELETE 请求（异步，可携带请求体）
     * @param request 请求配置
     * @param data 请求体数据
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     * @note 默认会立即启动；若已启用请求调度器，则按最终 request.priority() 排队后自动启动
     */
    QCNetworkReply *sendDelete(const QCNetworkRequest &request, const QByteArray &data);

    /**
     * @brief 发送 PATCH 请求（异步）
     * @param request 请求配置
     * @param data 请求体数据
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     * @note 默认会立即启动；若已启用请求调度器，则按最终 request.priority() 排队后自动启动
     */
    QCNetworkReply *sendPatch(const QCNetworkRequest &request, const QByteArray &data);

    /**
     * @brief 发送 GET 请求（同步，阻塞）
     * @param request 请求配置
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     * @warning 会阻塞当前线程直到请求完成，不要在 UI 线程中调用
     */
    QCNetworkReply *sendGetSync(const QCNetworkRequest &request);

    /**
     * @brief 发送 POST 请求（同步，阻塞）
     * @param request 请求配置
     * @param data 请求体数据
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     * @warning 会阻塞当前线程直到请求完成，不要在 UI 线程中调用
     */
    QCNetworkReply *sendPostSync(const QCNetworkRequest &request, const QByteArray &data);

    // ==================
    // 日志系统、中间件、取消令牌
    // ==================

    /**
     * @brief 设置日志记录器
     * @param logger 日志记录器对象（管理器不持有所有权，由调用者管理生命周期）
     */
    void setLogger(QCNetworkLogger *logger);

    /**
     * @brief 获取当前日志记录器
     * @return 日志记录器指针，如果未设置则返回 nullptr
     */
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

    /**
     * @brief 获取 debug trace 开关状态
     */
    [[nodiscard]] bool debugTraceEnabled() const noexcept;

    /**
     * @brief 添加请求/响应中间件
     * @param middleware 中间件对象（管理器不持有所有权）
     */
    void addMiddleware(QCNetworkMiddleware *middleware);

    /**
     * @brief 移除中间件
     * @param middleware 要移除的中间件对象
     */
    void removeMiddleware(QCNetworkMiddleware *middleware);

    /**
     * @brief 清空所有中间件
     */
    void clearMiddlewares();

    /**
     * @brief 获取所有中间件
     * @return 中间件列表
     */
    QList<QCNetworkMiddleware *> middlewares() const;

    // ==================
    // 快捷方法、Mock 工具
    // ==================

    /**
     * @brief 发送 JSON POST 请求
     * @param url 请求 URL
     * @param json JSON 对象
     * @return 网络响应对象
     */
    QCNetworkReply *postJson(const QUrl &url, const QJsonObject &json);

    /**
     * @brief 上传单个文件
     * @param url 请求 URL
     * @param filePath 文件路径
     * @param fieldName multipart 字段名
     * @return 网络响应对象
     */
    QCNetworkReply *uploadFile(const QUrl &url,
                               const QString &filePath,
                               const QString &fieldName);

    /**
     * @brief 下载文件到指定路径
     * @param url 请求 URL
     * @param savePath 保存路径
     * @return 网络响应对象；网络完成后会在 `finished` 回调中尝试把当前 reply body 写入 `savePath`
     * @note 写文件失败仅输出 `qWarning()`，不会把 reply 转成错误状态
     */
    QCNetworkReply *downloadFile(const QUrl &url, const QString &savePath);

    /**
     * @brief 设置 Mock 处理器（用于单元测试）
     * @param handler Mock 处理器对象（管理器不持有所有权）
     */
    void setMockHandler(QCNetworkMockHandler *handler);

    /**
     * @brief 获取当前 Mock 处理器
     * @return Mock 处理器指针，如果未设置则返回 nullptr
     */
    QCNetworkMockHandler *mockHandler() const;

    // ==================
    // 请求优先级调度
    // ==================

    /**
     * @brief 启用/禁用请求调度器
     *
     * 启用后，异步 `send*()` 会经过调度器管理；关闭后直接创建并执行 reply。
     *
     * @param enabled true 为启用调度器，false 为禁用
     */
    void enableRequestScheduler(bool enabled);

    /**
     * @brief 检查调度器是否已启用
     *
     * @return true 如果调度器已启用
     */
    bool isSchedulerEnabled() const;

    /**
     * @brief 获取调度器实例
     *
     * 返回当前线程的调度器实例，用于配置并发限制、带宽限制等策略。
     */
    QCNetworkRequestScheduler *scheduler() const;

    /**
     * @brief POST URL-encoded 表单数据（快捷方法）
     *
     * 自动 URL 编码表单字段并设置
     * `Content-Type: application/x-www-form-urlencoded`。
     *
     * @param url 请求 URL
     * @param formData 表单字段键值对
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     */
    QCNetworkReply *postForm(const QUrl &url, const QMap<QString, QString> &formData);

    // ==================
    // Multipart/form-data 支持
    // ==================

    /**
     * @brief POST Multipart/form-data 数据（快捷方法）
     *
     * 该重载会根据 `formData` 自动设置 `Content-Type`。
     *
     * @param url 请求 URL
     * @param formData Multipart 表单数据
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     */
    QCNetworkReply *postMultipart(const QUrl &url, const QCMultipartFormData &formData);

    /**
     * @brief POST Multipart/form-data 数据（带请求配置）
     *
     * 调用前会复制 `request`，并使用 `formData` 覆盖 `Content-Type`。
     *
     * @param request 请求配置（可设置超时、SSL、代理等）
     * @param formData Multipart 表单数据
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     */
    QCNetworkReply *postMultipart(const QCNetworkRequest &request,
                                  const QCMultipartFormData &formData);

    // ==================
    // 文件操作便捷 API
    // ==================

    /**
     * @brief 上传文件（简单版）
     *
     * 使用 multipart/form-data 上传单个文件，字段名固定为 `"file"`。
     *
     * @param url 上传 URL
     * @param filePath 本地文件路径
     * @return 网络响应对象；若文件无法加入 multipart，reply 会以
     * `InvalidRequest` 结束
     */
    QCNetworkReply *uploadFile(const QUrl &url, const QString &filePath);

    // ==================
    // 流式下载/上传 API
    // ==================

    /**
     * @brief 下载文件到 QIODevice（流式下载）
     *
     * 不会将整个文件加载到内存，而是边下载边写入设备。
     * 适用于大文件下载。
     *
     * @param url 下载 URL
     * @param device 目标设备（如 QFile、QBuffer），必须可写
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     *
     * @note 所有权：device 由调用方管理，QCurl 不会关闭或释放该设备
     * @note 生命周期：device 必须在请求完成前保持有效；中途销毁、不可写或写入失败
     * 会让 reply 以可诊断错误结束
     * @note 线程约束：device 必须与对应 reply 处于同一线程
     */
    QCNetworkReply *downloadToDevice(const QUrl &url, QIODevice *device);

    /**
     * @brief 从 QIODevice 上传数据（流式上传）
     *
     * 不会将整个文件加载到内存，而是边读取边上传。
     * 适用于大文件上传。
     *
     * @param url 上传 URL
     * @param fieldName 字段名
     * @param device 源设备（如 QFile），必须可读
     * @param fileName 文件名（会出现在请求中）
     * @param mimeType MIME 类型
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     *
     * @note 所有权：device 由调用方管理，QCurl 不会关闭或释放该设备
     * @note 生命周期：device 必须在请求完成前保持有效；中途销毁、不可读或读取失败
     * 会让 reply 以可诊断错误结束
     * @note 线程约束：device 必须与对应 reply 处于同一线程
     */
    QCNetworkReply *uploadFromDevice(const QUrl &url,
                                     const QString &fieldName,
                                     QIODevice *device,
                                     const QString &fileName,
                                     const QString &mimeType);

    /**
     * @brief 下载文件（支持断点续传）
     *
     * 目标文件已存在且 `overwrite == false` 时，会尝试从现有大小继续下载。
     *
     * @param url 下载 URL
     * @param savePath 保存路径
     * @param overwrite 如果文件已存在，是否覆盖（默认 false，即断点续传）
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     * @note 服务器必须支持 HTTP Range 请求（检查响应头 Accept-Ranges: bytes）
     * @warning 如果服务器不支持 Range 请求，会从头开始下载（覆盖现有文件）
     */
    QCNetworkReply *downloadFileResumable(const QUrl &url,
                                          const QString &savePath,
                                          bool overwrite = false);

    // ==================
    // 缓存管理
    // ==================

    /**
     * @brief 设置缓存实例
     *
     * 设置后，请求会按各自缓存策略使用该实例。
     *
     * @param cache 缓存实例（QCNetworkMemoryCache 或 QCNetworkDiskCache）
     *
     * @note manager 不获取缓存所有权，调用方需保证其生命周期
     */
    void setCache(QCNetworkCache *cache);

    /**
     * @brief 获取当前缓存实例
     * @return 缓存指针，如果未设置返回 nullptr
     */
    QCNetworkCache *cache() const;

    /**
     * @brief 设置磁盘缓存路径（便捷方法）
     *
     * 自动创建 `QCNetworkDiskCache` 实例并将其挂到当前 manager。
     *
     * @param path 缓存目录路径
     * @param maxSize 最大缓存大小（字节），默认 50MB
     * @note 会替换现有的缓存实例
     */
    void setCachePath(const QString &path, qint64 maxSize = 50 * 1024 * 1024);

signals:

public slots:

private:
    using ReplyFactory = std::function<QCNetworkReply *()>;

    QCNetworkReply *dispatchSendRequest(const QCNetworkRequest &request,
                                        HttpMethod method,
                                        bool async,
                                        const QByteArray &body,
                                        const char *apiName,
                                        const ReplyFactory &impl);
    QCNetworkReply *dispatchManagedSendRequest(const QCNetworkRequest &request,
                                               HttpMethod method,
                                               bool async,
                                               const QByteArray &body,
                                               const char *apiName);
    QCNetworkReply *createReply(const QCNetworkRequest &request,
                                HttpMethod method,
                                bool async,
                                const QByteArray &body,
                                QObject *parent);
    QCNetworkReply *createManagedReply(const QCNetworkRequest &request,
                                       HttpMethod method,
                                       bool async,
                                       const QByteArray &body,
                                       const QList<QCNetworkMiddleware *> &middlewares);
    QCNetworkReply *createNoEventLoopErrorReply(const QCNetworkRequest &request,
                                                HttpMethod method,
                                                const QByteArray &body,
                                                QObject *parent,
                                                const char *apiName);
    void applyReplyDefaults(QCNetworkReply *reply) const;
    void prepareManagedReply(QCNetworkReply *reply,
                             const QList<QCNetworkMiddleware *> &middlewares) const;

    Q_DECLARE_PRIVATE(QCNetworkAccessManager)
    QScopedPointer<QCNetworkAccessManagerPrivate> d_ptr;
    CookieFileModeFlag m_cookieModeFlag;
    QString m_cookieFilePath;
    bool m_schedulerEnabled;
    QCNetworkCache *m_cache;
    ShareHandleConfig m_shareHandleConfig;
    HstsAltSvcCacheConfig m_hstsAltSvcCacheConfig;
};

} // namespace QCurl
#endif // QCNETWORKACCESSMANAGER_H
