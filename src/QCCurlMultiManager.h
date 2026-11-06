/**
 * @file
 * @brief 声明线程绑定的 curl multi 管理器。
 */

#ifndef QCCURLMULTIMANAGER_H
#define QCCURLMULTIMANAGER_H

#include "QCCookie.h"
#include "QCCurlHandleManager.h"
#include "QCNetworkError.h"
#include "private/QCCurlPersistentTransferBridge_p.h"
#include "private/QCCurlShareCookieResult_p.h"

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
#include <memory>
#include <optional>

// moc 需要完整 reply 类型来生成信号参数元类型代码。
Q_MOC_INCLUDE("QCNetworkReply.h")

namespace QCurl {

class QCNetworkReply;                // 前向声明
class QCNetworkAccessManager;        // 前向声明
class QCNetworkConnectionPoolConfig; // 前向声明
class QCCurlMultiTransferRecord;
struct QCNetworkReplyTransferState;
struct QCCurlMultiDomain;
class QCNetworkRequest;
class QCCurlMultiManagerTestAccess;
struct SocketInfo;
namespace Internal {
class QCCurlShareCookieStore;
}

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

    /// 接收安全 detached handle；HTTP 隔离通知以无效 handle 表示不转移所有权。
    using TransferCompletionHandler
        = std::function<void(QCCurlHandleManager &&handle, CURLcode result, long httpStatus)>;
    using TransferToken                       = QCCurlTransferToken;
    using TransferPersistentCompletionHandler = QCCurlPersistentTransferCompletionHandler;

    /// 仅组织本次 HTTP admission 的依赖；state 与 reply 共享，不复制请求快照。
    struct HttpAdmission
    {
        QCNetworkReply *observer              = nullptr;
        const QCNetworkAccessManager *manager = nullptr;
        QSharedPointer<QCNetworkReplyTransferState> state;
    };

    enum class HttpAdmissionState {
        Accepted,
        Rejected,
        Quarantined,
    };

    /// 拒绝时仅归还已证明安全的 handle；隔离结果不携带 handle。
    struct HttpAdmissionResult
    {
        HttpAdmissionState state = HttpAdmissionState::Rejected;
        NetworkError error       = NetworkError::InvalidRequest;
        QString errorMessage;
        QStringList warnings;
        TransferToken token = 0;
        std::optional<QCCurlHandleManager> returnedHandle;
    };

    /**
     * @brief 在 owner thread 接管 HTTP easy handle，返回注册事实而不修改 reply。
     *
     * observer、manager（可空）与调用者必须同属本线程；拒绝错误线程时不读取它们。
     * completion 只接收已 detach 且已解除 record 回调的 handle；无效 handle 表示
     * ownership 不可证明，此时不得清零活动 token 或 cleanup 原 handle。
     */
    [[nodiscard]] Q_DECL_HIDDEN HttpAdmissionResult
    admitHttpReply(QCCurlHandleManager &&handle,
                   const HttpAdmission &admission,
                   TransferCompletionHandler completion);

    /// 注册不绑定 reply 的传输；调用方须保持自己的回调上下文存活至完成 detach。
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

    /// 按 opaque token 请求移除；跨线程或 libcurl 回调内仅排队，成功 detach 后才交还所有权。
    Q_DECL_HIDDEN void removeTransfer(TransferToken token);

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
    [[nodiscard]] Q_DECL_HIDDEN Internal::CookieCommandResult importCookiesForManager(
        const QCNetworkAccessManager *manager,
        const QList<QCCookie> &cookies,
        const QUrl &originUrl);

    /**
     * @brief 导出指定 manager 当前持有的 cookies
     *
     * @param filterUrl 可选过滤 URL，用于按 host/path 收敛结果
     * @return 结构化结果；成功时 `cookies` 为空表示没有匹配 cookie
     */
    [[nodiscard]] Q_DECL_HIDDEN Internal::CookieCommandResult exportCookiesForManager(
        const QCNetworkAccessManager *manager, const QUrl &filterUrl);

    /**
     * @brief 清空指定 manager 共享的 cookie store
     *
     * @return 结构化结果；FLUSH 失败明确返回 PersistenceFailed
     */
    [[nodiscard]] Q_DECL_HIDDEN Internal::CookieCommandResult clearAllCookiesForManager(
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

    // 由 access manager 在其 owner thread 同步最新期望，避免无新请求时重放过时 pending。
    Q_DECL_HIDDEN void synchronizeShareConfig(const QCNetworkAccessManager *manager);

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

    void handleSocketAction(QCCurlMultiDomain *domain, curl_socket_t socketfd, int eventsBitmask);
    void checkMultiInfo(QCCurlMultiDomain *domain);
    void cleanupSocket(QCCurlMultiDomain *domain, curl_socket_t socketfd);
    int manageSocketNotifiers(QCCurlMultiDomain *domain,
                              curl_socket_t socketfd,
                              int what,
                              SocketInfo *socketInfo);

    static int curlSocketCallback(
        CURL *easy, curl_socket_t socketfd, int what, void *userp, void *socketp);

    static int curlTimerCallback(CURLM *multi, long timeout_ms, void *userp);

    [[nodiscard]] bool configureMultiCallbacks(QCCurlMultiDomain *domain, const char *context);
    void disableMultiCallbacks(QCCurlMultiDomain *domain);
    using Domain = std::shared_ptr<QCCurlMultiDomain>;
    [[nodiscard]] Domain createDomain(QString *error);
    [[nodiscard]] Domain selectDomain(const QCNetworkRequest &request, QString *error);
    [[nodiscard]] QList<Domain> allDomains() const;
    void retireDomain(QCCurlMultiDomain *domain);
    void stopDomainEvents(QCCurlMultiDomain *domain);
    void reclaimIdleDomains();
    void wakeupDomain(const Domain &domain);

    [[nodiscard]] bool consumeShareResultLocked(const Internal::ShareCallResult &result);
    [[nodiscard]] bool prepareCookieCommandLocked(const QCNetworkAccessManager *manager,
                                                  Internal::CookieCommandResult &result);
    [[nodiscard]] Internal::CookieCommandResult publishCookieCommandLocked(
        Internal::CookieCommandResult result);
    void openShareScopeLocked(const QCNetworkAccessManager *manager);
    [[nodiscard]] Internal::ShareCallResult bindShareToHttpLocked(
        const QCNetworkAccessManager *manager, const QCCurlMultiTransferRecord &record);
    [[nodiscard]] bool detachShareBindingLocked(CURL *easy, const char *operation);

    [[nodiscard]] HttpAdmissionResult admitHttpRecordLocked(
        const QCNetworkAccessManager *manager, QSharedPointer<QCCurlMultiTransferRecord> record);
    [[nodiscard]] HttpAdmissionResult rejectHttpAdmissionLocked(
        QSharedPointer<QCCurlMultiTransferRecord> record, HttpAdmissionResult result);
    [[nodiscard]] TransferToken commitTransferRecordLocked(
        const QSharedPointer<QCCurlMultiTransferRecord> &record);

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
    /// 返回 true 才表示 easy 已从所属 multi 安全移除；失败时不转移所有权。
    [[nodiscard]] bool removeTransferHandleLocked(
        const QSharedPointer<QCCurlMultiTransferRecord> &record,
        CURLcode result,
        const char *context);
    void retryDetachTransferRecord(const QSharedPointer<QCCurlMultiTransferRecord> &record,
                                   CURLcode result,
                                   const QString &context);
    /// 将 manager 与进程 runtime 一并置为 fail-closed poisoned 状态。
    void poisonLocked(const char *context, CURLMcode code);
    /// 将尚未登记但 ownership 已不可证明的对象图标记为 NonCallable 并隔离。
    void quarantineUnregisteredTransferLocked(
        QSharedPointer<QCCurlMultiTransferRecord> &&record) noexcept;
    void retainPoisonedObjectGraph() noexcept;
    void dispatchFinishedTransfer(FinishedTransfer &&transfer);

    [[nodiscard]] SocketInfo *ensureSocketInfo(QCCurlMultiDomain *domain,
                                               curl_socket_t socketfd,
                                               SocketInfo *socketInfo);
    void updateReadNotifier(QCCurlMultiDomain *domain, SocketInfo *socketInfo, int what);
    void updateWriteNotifier(QCCurlMultiDomain *domain, SocketInfo *socketInfo, int what);

    void cleanupActiveHandlesForShutdown(const QList<CURL *> &activeHandles);
    void disableSocketsForShutdown(const QList<QSharedPointer<SocketInfo>> &sockets);

    [[nodiscard]] bool addEasyToMultiLocked(QCCurlMultiDomain *domain, CURL *easy, QString *error);
    [[nodiscard]] Q_DECL_HIDDEN bool registerTransferRecord(
        const QSharedPointer<QCCurlMultiTransferRecord> &transfer,
        TransferToken *tokenOut,
        QString *error);

    [[nodiscard]] bool applyLimitsConfig(QCCurlMultiDomain *domain,
                                         const QCNetworkConnectionPoolConfig &config,
                                         QString *error);
    [[nodiscard]] bool applyMultiLongOption(QCCurlMultiDomain *domain,
                                            CURLMoption option,
                                            const char *optionName,
                                            long value,
                                            std::optional<long> &stateSlot,
                                            QString *error);
    void shutdown(bool retainPoisonedGraph);
    void shutdownTransportGraph();
    void unregisterRuntimeParticipant() noexcept;
    void releaseRuntimeParticipation() noexcept;

private:
    // ==================
    // 成员变量
    // ==================

    Internal::RuntimeLease m_runtimeLease; ///< 覆盖 multi/share/callback 对象图的 runtime lease。
    quint64 m_runtimeParticipant = 0;      ///< 仅在 owner thread 注册和注销的 teardown token。
    Domain m_baseDomain;                   ///< 无覆盖 Isolated/native transfer 的固定域。
    Domain m_nativeDomain;                 ///< 同线程固定 NativeShared 域，不参与 LRU。
    QHash<QStringList, Domain> m_isolatedDomains; ///< 完整有效规则到独立 CURLM 的映射。
    bool m_isReady = false;                       ///< multi handle 与事件回调均已就绪
    QString m_initializationError;                ///< manager 不可用时的初始化错误

    QRecursiveMutex m_mutex; ///< 保护共享资源的互斥锁（允许 libcurl 回调重入）

    QHash<CURL *, QSharedPointer<QCCurlMultiTransferRecord>> m_activeTransfers;
    ///< manager-owned 活动传输记录（键：easy handle）

    TransferToken m_nextTransferToken = 1; ///< opaque transfer token，0 保留为无效值

    std::atomic<int> m_runningRequests{0}; ///< 运行中的请求计数（原子）

    std::atomic<bool> m_isShuttingDown{false}; ///< 析构中标记（避免回调重入导致死锁）
    std::atomic<bool> m_isPoisoned{false};     ///< detach 失败后无法证明原生所有权。

    QTimer *m_domainReaper = nullptr; ///< 仅回收历史空闲域，不限制真实活动连接总数。

    // 整图保留共享同一模块，使 share、临时 easy 与回调 userdata 一起进入 quarantine。
    QSharedPointer<Internal::QCCurlShareCookieStore> m_shareCookieStore;

#ifdef QCURL_ENABLE_TEST_HOOKS
    int m_testCompletionCount      = 0;
    bool m_testCompletionHadHandle = false;
    int m_testSocketActionCount    = 0;
#endif
};

} // namespace QCurl

#endif // QCCURLMULTIMANAGER_H
