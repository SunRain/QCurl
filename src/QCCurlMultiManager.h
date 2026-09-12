/**
 * @file
 * @brief 声明线程绑定的 curl multi 管理器。
 */

#ifndef QCCURLMULTIMANAGER_H
#define QCCURLMULTIMANAGER_H

#include "QCCookie.h"
#include "QCCurlHandleManager.h"
#include "private/QCCookieStoreResult_p.h"
#include "private/QCCurlMultiManagerShareState_p.h"
#include "private/QCCurlPersistentTransferBridge_p.h"

#include <QHash>
#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QPointer>
#include <QRecursiveMutex>
#include <QSharedPointer>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <atomic>
#include <curl/curl.h>
#include <functional>
#include <limits>
#include <optional>

// moc 需要完整 reply 类型来生成信号参数元类型代码。
Q_MOC_INCLUDE("QCNetworkReply.h")

namespace QCurl {

class QCNetworkReply;                // 前向声明
class QCNetworkAccessManager;        // 前向声明
class QCNetworkConnectionPoolConfig; // 前向声明
class QCCurlMultiTransferRecord;
class QCCurlMultiManagerTestAccess;
struct SocketInfo;

/**
 * @brief 每线程一个的 curl multi manager
 *
 * `instance()` 返回当前线程绑定的 manager。manager 只在所属线程内维护
 * `CURLM *`、socket notifier 和活动 reply 集合。
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
     * @note 线程合同：仅允许在 manager owner thread 调用；错误线程调用同步返回，且不会读取
     * reply 或改变 manager 状态。
     * @note reply 必须有有效的 curl easy handle
     * @note 同一 reply 不能重复添加
     */
    void addReply(QCNetworkReply *reply);

    /**
     * @brief 注册不绑定 QCNetworkReply 的 multi 传输。
     *
     * 传输完成或被延迟移除后，manager 在完成 multi detach 后把 easy handle
     * 移交给 completion handler。调用方不得在回调前销毁传输上下文。
     */
    using TransferCompletionHandler
        = std::function<void(QCCurlHandleManager &&handle, CURLcode result, long httpStatus)>;
    using TransferToken                       = QCCurlTransferToken;
    using TransferPersistentCompletionHandler = QCCurlPersistentTransferCompletionHandler;
    [[nodiscard]] Q_DECL_HIDDEN bool addTransfer(QCCurlHandleManager &&handle,
                                                 TransferCompletionHandler completion,
                                                 TransferToken *token = nullptr,
                                                 QString *error       = nullptr);

    /**
     * @brief 注册在握手完成后继续保持 easy handle 的 multi 传输。
     *
     * 该路径用于 CONNECT_ONLY WebSocket：multi 报告握手完成后只通知调用方，
     * 仍由内部 transfer record 持有 easy handle，直到调用 removeTransfer()。
     * 这样不会触发 libcurl 对 connect-only connection 的提前关闭。
     */
    [[nodiscard]] Q_DECL_HIDDEN bool addPersistentTransfer(
        QCCurlHandleManager &&handle,
        TransferPersistentCompletionHandler completion,
        TransferToken *token = nullptr,
        QString *error       = nullptr);

    /// 按 opaque token 请求移除 manager-owned 传输。
    Q_DECL_HIDDEN void removeTransfer(TransferToken token);

    /**
     * @brief 从管理器移除请求
     *
     * 从 multi handle 移除 curl easy handle，停止请求处理。
     * 通常在请求完成、取消或对象销毁时调用。
     *
     * @param reply 网络响应对象
     *
     * @note 线程合同：仅允许在 manager owner thread 调用；错误线程调用同步返回，且不会读取
     * reply 或改变 manager 状态。
     * @note 如果 reply 不在管理器中，操作无效
     */
    void removeReply(QCNetworkReply *reply);

    /**
     * @brief 按 manager-owned transfer record 请求 deferred detach。
     *
     * 该入口不依赖 reply 仍然存活，供 reply 析构和取消路径使用。
     */
    Q_DECL_HIDDEN void removeTransferRecord(QCCurlMultiTransferRecord *record);

    /**
     * @brief 获取当前活动请求数量
     *
     * @return int 正在运行的请求数量
     *
     * @note 线程安全（原子操作）
     */
    [[nodiscard]] int runningRequestsCount() const noexcept;

    /// 返回 multi engine 是否完成全局与 multi 初始化。
    [[nodiscard]] bool isReady() const noexcept { return m_isReady; }

    /// 返回 multi 初始化失败的统一诊断。
    [[nodiscard]] QString initializationError() const { return m_initializationError; }

#ifdef QCURL_ENABLE_TEST_HOOKS
    [[nodiscard]] Q_DECL_HIDDEN int activeRepliesCountForTest();
    [[nodiscard]] Q_DECL_HIDDEN bool isPoisonedForTest() const noexcept;
    [[nodiscard]] Q_DECL_HIDDEN static int quarantinedGraphCountForTest() noexcept;
    [[nodiscard]] Q_DECL_HIDDEN static int quarantinedPendingTransfersForTest() noexcept;
    [[nodiscard]] Q_DECL_HIDDEN static bool quarantineIsNonCallableForTest() noexcept;
    [[nodiscard]] QCURL_EXPORT bool addTransferForTest(TransferToken *token, QString *error);
    QCURL_EXPORT void processUnknownDoneForTest();
    [[nodiscard]] QCURL_EXPORT int completionCountForTest() const noexcept;
    [[nodiscard]] QCURL_EXPORT bool completionHadHandleForTest() const noexcept;
    [[nodiscard]] QCURL_EXPORT int socketActionCountForTest() const noexcept;
    Q_DECL_HIDDEN void shutdownForTest();
#endif

    // ==================
    // Cookie bridge（用于与 Qt WebView 等上层 cookie store 互通）
    // ==================

    /**
     * @brief 为指定 manager 导入 cookies
     *
     * 仅在该 manager 开启 shareCookies 时生效；必要时会根据 originUrl
     * 为缺失 domain/path 的 cookie 补全作用域。失败结果区分预校验拒绝、
     * 已回滚、持久化失败和 store poisoned。
     */
    [[nodiscard]] Internal::CookieStoreResult importCookiesForManager(
        const QCNetworkAccessManager *manager,
        const QList<QCCookie> &cookies,
        const QUrl &originUrl);

    /**
     * @brief 导出指定 manager 当前持有的 cookies
     *
     * @param filterUrl 可选过滤 URL，用于按 host/path 收敛结果
     * @return 结构化结果；成功时 `cookies` 为空表示没有匹配 cookie
     */
    [[nodiscard]] Internal::CookieStoreResult exportCookiesForManager(
        const QCNetworkAccessManager *manager, const QUrl &filterUrl);

    /**
     * @brief 清空指定 manager 共享的 cookie store
     *
     * @return 结构化结果；FLUSH 失败明确返回 PersistenceFailed
     */
    [[nodiscard]] Internal::CookieStoreResult clearAllCookiesForManager(
        const QCNetworkAccessManager *manager);

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
     * @brief 在 owner thread 应用原生 multi 配置，失败时拒绝本次 admission。
     *
     * 空配置恢复默认限制，不重建 multi、不终止已有连接。
     */
    [[nodiscard]] bool applyLimitsConfig(const QCNetworkConnectionPoolConfig &config,
                                         QString *error);

Q_SIGNALS:
    /**
     * @brief 请求完成信号（内部使用）
     *
     * 当 libcurl 报告请求完成时发射此信号。
     *
     * @note 该信号仅用于内部 debug/观测（例如统计 CURLcode 分布），不作为
     *       QCNetworkReply 完成回调的 SSOT。
     *       Reply 的完成/重试逻辑通过点对点投递到 Reply 线程执行。
     *
     * @param reply 完成的响应对象
     * @param curlCode libcurl 结果码（CURLE_OK 表示成功）
     *
     * @note 信号发射在管理器线程，可能需要跨线程连接
     */
    void requestFinished(QCNetworkReply *reply, int curlCode);

private:
    Q_DISABLE_COPY_MOVE(QCCurlMultiManager)
#ifdef QCURL_ENABLE_TEST_HOOKS
    friend class QCCurlMultiManagerTestAccess;
#endif

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

    void handleSocketAction(curl_socket_t socketfd, int eventsBitmask);
    void checkMultiInfo();
    void cleanupSocket(curl_socket_t socketfd);
    int manageSocketNotifiers(curl_socket_t socketfd, int what, SocketInfo *socketInfo);

    static int curlSocketCallback(
        CURL *easy, curl_socket_t socketfd, int what, void *userp, void *socketp);

    static int curlTimerCallback(CURLM *multi, long timeout_ms, void *userp);

    [[nodiscard]] bool configureMultiCallbacks(const char *context);
    void disableMultiCallbacks();

    struct AddReplyResult
    {
        bool success = false;
        QString errorMessage;
    };

    /// 描述 share 配置后 easy 是否仍允许进入 libcurl multi。
    enum class ShareApplyState {
        Callable,
        NonCallable,
    };

    /// 返回 share apply 的可调用状态与稳定失败诊断。
    struct ShareApplyResult
    {
        ShareApplyState state = ShareApplyState::Callable;
        QString errorMessage;
    };

    [[nodiscard]] Q_DECL_HIDDEN AddReplyResult tryAddReplyOnOwnerThread(QCNetworkReply *reply);
    Q_DECL_HIDDEN void queueAddReplyFailure(const QPointer<QCNetworkReply> &reply,
                                            const QString &message);

    struct FinishedTransfer
    {
        QPointer<QCNetworkReply> reply;
        std::optional<QCCurlHandleManager> detachedHandle;
        TransferCompletionHandler completion;
        QSharedPointer<QCCurlMultiTransferRecord> persistentRecord;
        TransferPersistentCompletionHandler persistentCompletion;
        CURLcode curlCode   = CURLE_OK;
        long httpStatusCode = 0;
    };

    [[nodiscard]] std::optional<FinishedTransfer> takeFinishedTransferLocked(CURLMsg *message);
    [[nodiscard]] Q_DECL_HIDDEN std::optional<FinishedTransfer> detachTransferRecordLocked(
        QCCurlMultiTransferRecord *record, CURLcode result, const char *context);
    void retryDetachTransferRecord(const QSharedPointer<QCCurlMultiTransferRecord> &record,
                                   CURLcode result,
                                   const QString &context);
    /// 将 manager 与进程 runtime 一并置为 fail-closed poisoned 状态。
    void poisonLocked(const char *context, CURLMcode code);
    /// 将尚未登记但 ownership 已不可证明的对象图标记为 NonCallable 并隔离。
    void quarantineUnregisteredTransferLocked(
        QSharedPointer<QCCurlMultiTransferRecord> &&record,
        CURL *easy,
        Internal::QCCurlMultiManagerShareContext *shareContext) noexcept;
    void retainPoisonedObjectGraph() noexcept;
    void dispatchFinishedTransfer(FinishedTransfer &&transfer);

    [[nodiscard]] SocketInfo *ensureSocketInfo(curl_socket_t socketfd, SocketInfo *socketInfo);
    void updateReadNotifier(SocketInfo *socketInfo, int what);
    void updateWriteNotifier(SocketInfo *socketInfo, int what);

    using ShareConfig  = Internal::QCCurlMultiManagerShareConfig;
    using ShareContext = Internal::QCCurlMultiManagerShareContext;

    static void shareLockCallback(CURL *handle,
                                  curl_lock_data data,
                                  curl_lock_access access,
                                  void *userptr);
    static void shareUnlockCallback(CURL *handle, curl_lock_data data, void *userptr);

    static ShareConfig toShareConfig(const QCNetworkAccessManager *manager);
    static QString shareConfigSummary(const ShareConfig &config);

    ShareContext *getOrCreateShareContextLocked(const QCNetworkAccessManager *manager);
    void onAccessManagerDestroyedLocked(const QCNetworkAccessManager *manager);
    [[nodiscard]] bool applyShareConfigIfIdleLocked(ShareContext *context,
                                                    const ShareConfig &desired,
                                                    QString *error);
    [[nodiscard]] bool initializeShareContextLocked(ShareContext *context,
                                                    const ShareConfig &desired,
                                                    QString *error);
    [[nodiscard]] bool cleanupShareHandleLocked(ShareContext *context, const char *operation);
    void cleanupActiveHandlesForShutdown(const QList<CURL *> &activeHandles);
    void disableSocketsForShutdown(const QList<QSharedPointer<SocketInfo>> &sockets);
    void cleanupShareContextsForShutdown(const QList<QSharedPointer<ShareContext>> &shareContexts);
    void releaseShareForEasyHandleLocked(CURL *easy);
    [[nodiscard]] bool detachShareBindingLocked(CURL *easy, const char *operation);
    void maybeFinalizeShareContextLocked(ShareContext *context);

    [[nodiscard]] CURL *validatedEasyHandle(QCNetworkReply *reply) const;
    [[nodiscard]] bool addEasyToMultiLocked(CURL *easy);
    [[nodiscard]] Q_DECL_HIDDEN bool addEasyToMultiLocked(CURL *easy, QString *error);
    [[nodiscard]] Q_DECL_HIDDEN bool registerTransferRecord(
        const QSharedPointer<QCCurlMultiTransferRecord> &transfer,
        TransferToken *tokenOut,
        QString *error);

    [[nodiscard]] ShareContext *prepareShareForReplyLocked(QCNetworkReply *reply, CURL *easy);
    [[nodiscard]] ShareApplyResult applyShareToEasyLocked(QCNetworkReply *reply,
                                                          CURL *easy,
                                                          ShareContext *shareContext);
    void resetShareOnEasyIfNeeded(CURL *easy);
    [[nodiscard]] ShareContext *prepareCookieContextLocked(const QCNetworkAccessManager *manager,
                                                           const ShareConfig &desired,
                                                           Internal::CookieStoreResult *failure);

    [[nodiscard]] bool applyMultiLongOption(CURLMoption option,
                                            const char *optionName,
                                            long value,
                                            std::optional<long> &stateSlot,
                                            QString *error);
    void shutdown(bool retainPoisonedGraph);
    void unregisterRuntimeParticipant() noexcept;
    void releaseRuntimeParticipation() noexcept;

private:
    // ==================
    // 成员变量
    // ==================

    Internal::RuntimeLease m_runtimeLease; ///< 覆盖 multi/share/callback 对象图的 runtime lease。
    quint64 m_runtimeParticipant = 0;      ///< 仅在 owner thread 注册和注销的 teardown token。
    CURLM *m_multiHandle;                  ///< libcurl multi handle
    bool m_isReady = false;                ///< multi handle 与事件回调均已就绪
    QString m_initializationError;         ///< manager 不可用时的初始化错误

    QRecursiveMutex m_mutex; ///< 保护共享资源的互斥锁（允许 libcurl 回调重入）

    QHash<CURL *, QSharedPointer<QCCurlMultiTransferRecord>> m_activeTransfers;
    ///< manager-owned 活动传输记录（键：easy handle）

    TransferToken m_nextTransferToken = 1; ///< opaque transfer token，0 保留为无效值

    std::atomic<int> m_runningRequests{0}; ///< 运行中的请求计数（原子）

    std::atomic<bool> m_isShuttingDown{false}; ///< 析构中标记（避免回调重入导致死锁）
    std::atomic<bool> m_isPoisoned{false};     ///< ownership cannot be proved after detach failure

    QTimer *m_socketTimer; ///< socket 超时定时器

    QHash<curl_socket_t, QSharedPointer<SocketInfo>> m_socketMap; ///< socket 信息映射

    // ==================
    // multi limits（M3）：上次成功应用的阀值（用于识别“清除设置”的场景）
    // ==================

    std::optional<long> m_multiMaxTotalConnections  = std::nullopt;
    std::optional<long> m_multiMaxHostConnections   = std::nullopt;
    std::optional<long> m_multiMaxConcurrentStreams = std::nullopt;
    std::optional<long> m_multiMaxConnects          = std::nullopt;
    std::optional<long> m_multiMultiplexing         = std::nullopt;

    // ==================
    // Share handle（M6+，可选）：按 manager scope 隔离（默认关闭）
    // ==================

    QHash<const QCNetworkAccessManager *, QSharedPointer<ShareContext>> m_shareContexts;
    QHash<CURL *, ShareContext *> m_easyToShareContext;
    QHash<CURL *, bool> m_easyShareOptionSet;

#ifdef QCURL_ENABLE_TEST_HOOKS
    int m_testCompletionCount      = 0;
    bool m_testCompletionHadHandle = false;
    int m_testSocketActionCount    = 0;
#endif
};

} // namespace QCurl

#endif // QCCURLMULTIMANAGER_H
