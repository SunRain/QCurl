/**
 * @file
 * @brief 声明线程绑定的 curl multi 管理器。
 */

#ifndef QCCURLMULTIMANAGER_H
#define QCCURLMULTIMANAGER_H

#include <QHash>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QRecursiveMutex>
#include <QSharedPointer>
#include <QSocketNotifier>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <atomic>
#include <curl/curl.h>
#include <limits>
#include <optional>

class QNetworkCookie; // QtNetwork

namespace QCurl {

class QCNetworkReply;                // 前向声明
class QCNetworkAccessManager;        // 前向声明
class QCNetworkConnectionPoolConfig; // 前向声明

/**
 * @brief Socket 信息结构体
 *
 * 存储每个 socket 对应的 QSocketNotifier。
 * libcurl 通过 socket 回调管理网络事件。
 *
 * @internal
 */
struct SocketInfo
{
    curl_socket_t socketfd         = CURL_SOCKET_BAD; ///< Socket 文件描述符
    QSocketNotifier *readNotifier  = nullptr;         ///< 读事件通知器
    QSocketNotifier *writeNotifier = nullptr;         ///< 写事件通知器
    ~SocketInfo()                  = default;
};

/**
 * @brief 每线程一个的 curl multi manager
 *
 * `instance()` 返回当前线程绑定的 manager。manager 在其所属线程内维护
 * `CURLM *`、socket notifier 和活动 reply 集合，并接受跨线程投递的管理操作。
 */
class QCCurlMultiManager : public QObject
{
    Q_OBJECT

public:
    /// 返回当前线程绑定的 manager 实例。
    static QCCurlMultiManager *instance();

    /**
     * @brief 添加异步请求到管理器
     *
     * 将 QCNetworkReply 的 curl easy handle 注册到 multi handle，
     * 开始异步执行网络请求。
     *
     * @param reply 网络响应对象（必须是异步模式）
     *
     * @note 线程安全
     * @note reply 必须有有效的 curl easy handle
     * @note 同一 reply 不能重复添加
     */
    void addReply(QCNetworkReply *reply);

    /**
     * @brief 从管理器移除请求
     *
     * 从 multi handle 移除 curl easy handle，停止请求处理。
     * 通常在请求完成、取消或对象销毁时调用。
     *
     * @param reply 网络响应对象
     *
     * @note 线程安全
     * @note 如果 reply 不在管理器中，操作无效
     */
    void removeReply(QCNetworkReply *reply);

    /**
     * @brief 获取当前活动请求数量
     *
     * @return int 正在运行的请求数量
     *
     * @note 线程安全（原子操作）
     */
    [[nodiscard]] int runningRequestsCount() const noexcept;

    // ==================
    // Cookie bridge（用于与 Qt WebView 等上层 cookie store 互通）
    // ==================

    /**
     * @brief 为指定 manager 导入 cookies
     *
     * 仅在该 manager 开启 shareCookies 时生效；必要时会根据 originUrl
     * 为缺失 domain/path 的 cookie 补全作用域。
     */
    bool importCookiesForManager(const QCNetworkAccessManager *manager,
                                 const QList<QNetworkCookie> &cookies,
                                 const QUrl &originUrl,
                                 QString *error);

    /**
     * @brief 导出指定 manager 当前持有的 cookies
     *
     * @param filterUrl 可选过滤 URL，用于按 host/path 收敛结果
     * @param error 可选错误输出
     * @return 当前 share context 可见的 cookies 列表
     */
    [[nodiscard]] QList<QNetworkCookie> exportCookiesForManager(
        const QCNetworkAccessManager *manager, const QUrl &filterUrl, QString *error);

    /**
     * @brief 清空指定 manager 共享的 cookie store
     *
     * @return true 表示清空成功；未启用 shareCookies 时返回 false 并写入错误
     */
    bool clearAllCookiesForManager(const QCNetworkAccessManager *manager, QString *error);

    /**
     * @brief 触发一次 multi 推进/唤醒
     *
     * 用于处理“resume 后缺少 socket/timer 事件导致不推进”的边缘态。
     * 该方法会确保在管理器线程内触发一次等价的 multi 驱动动作。
     *
     * @note 线程安全：可从任意线程调用；必要时会 marshal 到管理器线程。
     */
    void wakeup();

    /**
     * @brief 应用 multi 级别的资源阀门配置（可安全 no-op）
     *
     * 当前阶段该接口作为“未来扩展点”保留：即使暂不设置任何 CURLMOPT_*，
     * 也必须保证可被调用且不会崩溃/死锁。
     *
     * @note 线程安全：可从任意线程调用；必要时会 marshal 到管理器线程。
     */
    void applyLimitsConfig(const QCNetworkConnectionPoolConfig &config);

Q_SIGNALS:
    /**
     * @brief 请求完成信号（内部使用）
     *
     * 当 libcurl 报告请求完成时发射此信号。
     * QCNetworkReply 对象通过连接此信号来接收完成通知。
     *
     * @param reply 完成的响应对象
     * @param curlCode libcurl 结果码（CURLE_OK 表示成功）
     *
     * @note 信号发射在管理器线程，可能需要跨线程连接
     */
    void requestFinished(QCNetworkReply *reply, int curlCode);

private:
    /// 初始化当前线程绑定的 multi handle 和事件驱动对象。
    explicit QCCurlMultiManager(QObject *parent = nullptr);

    /**
     * @brief 析构函数
     *
     * 清理所有 socket、活动请求和 curl multi handle。
     *
     * @warning 析构时如果还有活动请求会打印警告
     */
    ~QCCurlMultiManager() override;

    // 禁止拷贝和移动
    Q_DISABLE_COPY_MOVE(QCCurlMultiManager)

    // ==================
    // Socket 事件处理
    // ==================

    /**
     * @brief 处理 socket 事件
     *
     * 调用 curl_multi_socket_action() 处理 socket 活动，
     * 然后检查是否有请求完成。
     *
     * @param socketfd socket 文件描述符
     * @param eventsBitmask 事件掩码（CURL_CSELECT_IN/OUT/ERR）
     *
     * @note 内部方法，由 QSocketNotifier 回调触发
     */
    void handleSocketAction(curl_socket_t socketfd, int eventsBitmask);

    /**
     * @brief 检查并处理完成的请求
     *
     * 调用 curl_multi_info_read() 获取完成消息，
     * 并发射 requestFinished() 信号通知 Reply 对象。
     *
     * @note 内部方法，在 handleSocketAction() 后调用
     */
    void checkMultiInfo();

    /**
     * @brief 清理 socket 资源
     *
     * 删除 QSocketNotifier 并从映射表中移除 socket。
     *
     * @param socketfd socket 文件描述符
     *
     * @note 内部方法，在 CURL_POLL_REMOVE 时调用
     */
    void cleanupSocket(curl_socket_t socketfd);

    /**
     * @brief 管理 socket 的 QSocketNotifier
     *
     * 根据 libcurl 的要求创建、启用、禁用或删除 QSocketNotifier。
     *
     * @param socketfd socket 文件描述符
     * @param what libcurl 动作（CURL_POLL_IN/OUT/INOUT/REMOVE/NONE）
     * @param socketInfo 当前 socket 信息（可能为 nullptr）
     *
     * @return int 0 表示成功
     *
     * @note 内部方法，由 curlSocketCallback() 调用
     */
    int manageSocketNotifiers(curl_socket_t socketfd, int what, SocketInfo *socketInfo);

    // ==================
    // libcurl 静态回调（C 接口）
    // ==================

    /**
     * @brief Socket 回调（libcurl 调用）
     *
     * libcurl 在 socket 状态变化时调用此函数，通知应用程序
     * 需要监听或停止监听某个 socket 的 I/O 事件。
     *
     * @param easy curl easy handle
     * @param socketfd socket 文件描述符
     * @param what 动作（CURL_POLL_IN/OUT/INOUT/REMOVE/NONE）
     * @param userp 用户数据（QCCurlMultiManager* 指针）
     * @param socketp socket 私有数据（SocketInfo* 指针）
     *
     * @return int 0 表示成功，-1 表示失败
     */
    static int curlSocketCallback(
        CURL *easy, curl_socket_t socketfd, int what, void *userp, void *socketp);

    /**
     * @brief 定时器回调（libcurl 调用）
     *
     * libcurl 调用此函数设置超时定时器。当定时器触发时，
     * 应用程序需要调用 curl_multi_socket_action() 处理超时事件。
     *
     * @param multi curl multi handle
     * @param timeout_ms 超时毫秒数（-1 表示停止定时器，0 表示立即触发）
     * @param userp 用户数据（QCCurlMultiManager* 指针）
     *
     * @return int 0 表示成功，-1 表示失败
     */
    static int curlTimerCallback(CURLM *multi, long timeout_ms, void *userp);

    // ==================
    // Share handle（M6+，可选）
    // ==================

    struct ShareConfig
    {
        bool dnsCache   = false;
        bool cookies    = false;
        bool sslSession = false;

        [[nodiscard]] bool enabled() const noexcept { return dnsCache || cookies || sslSession; }

        bool operator==(const ShareConfig &other) const noexcept
        {
            return dnsCache == other.dnsCache && cookies == other.cookies
                   && sslSession == other.sslSession;
        }

        bool operator!=(const ShareConfig &other) const noexcept { return !(*this == other); }
    };

    struct ShareContext
    {
        const QCNetworkAccessManager *scopeKey = nullptr;
        CURLSH *share                          = nullptr;
        ShareConfig applied;
        // 待应用配置
        // 为空表示无变更
        std::optional<ShareConfig> pending;

        ShareConfig lastInitAttempt;
        bool lastInitFailed = false;
        QString lastInitError;

        bool scopeDestroyed = false;
        bool pendingDelete  = false;
        int activeUsers     = 0;

        QMutex dnsMutex;
        QMutex cookieMutex;
        QMutex sslMutex;
        QMutex otherMutex;
    };

    static void shareLockCallback(CURL *handle,
                                  curl_lock_data data,
                                  curl_lock_access access,
                                  void *userptr);
    static void shareUnlockCallback(CURL *handle, curl_lock_data data, void *userptr);

    static ShareConfig toShareConfig(const QCNetworkAccessManager *manager);
    static QString shareConfigSummary(const ShareConfig &config);

    ShareContext *getOrCreateShareContextLocked(const QCNetworkAccessManager *manager);
    void onAccessManagerDestroyedLocked(const QCNetworkAccessManager *manager);
    bool applyShareConfigIfIdleLocked(ShareContext *context,
                                      const ShareConfig &desired,
                                      QString *error);
    void cleanupEasyHandleLocked(CURL *easy,
                                 bool removeFromMulti,
                                 bool decrementRunningCount,
                                 const char *context);
    void releaseShareForEasyHandleLocked(CURL *easy);
    void maybeFinalizeShareContextLocked(ShareContext *context);

private:
    // ==================
    // 成员变量
    // ==================

    CURLM *m_multiHandle; ///< libcurl multi handle

    QRecursiveMutex m_mutex; ///< 保护共享资源的互斥锁（允许 libcurl 回调重入）

    QHash<CURL *, QPointer<QCNetworkReply>> m_activeReplies; ///< 活动请求（键：easy handle）

    std::atomic<int> m_runningRequests{0}; ///< 运行中的请求计数（原子）

    std::atomic<bool> m_isShuttingDown{false}; ///< 析构中标记（避免回调重入导致死锁）

    QTimer *m_socketTimer; ///< socket 超时定时器

    QHash<curl_socket_t, QSharedPointer<SocketInfo>> m_socketMap; ///< socket 信息映射

    // ==================
    // multi limits（M3）：上次成功应用的阀值（用于识别“清除设置”的场景）
    // ==================

    std::optional<long> m_multiMaxTotalConnections  = std::nullopt;
    std::optional<long> m_multiMaxHostConnections   = std::nullopt;
    std::optional<long> m_multiMaxConcurrentStreams = std::nullopt;
    std::optional<long> m_multiMaxConnects          = std::nullopt;

    // ==================
    // Share handle（M6+，可选）：按 manager scope 隔离（默认关闭）
    // ==================

    QHash<const QCNetworkAccessManager *, QSharedPointer<ShareContext>> m_shareContexts;
    QHash<CURL *, ShareContext *> m_easyToShareContext;
    QHash<CURL *, bool> m_easyShareOptionSet;
};

} // namespace QCurl

#endif // QCCURLMULTIMANAGER_H
