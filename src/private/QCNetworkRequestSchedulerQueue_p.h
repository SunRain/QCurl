/**
 * @file
 * @brief QCNetworkRequestScheduler queue / lane policy helpers.
 */

#ifndef QCNETWORKREQUESTSCHEDULERQUEUE_P_H
#define QCNETWORKREQUESTSCHEDULERQUEUE_P_H

#include "QCNetworkRequestScheduler.h"

#include <QAbstractEventDispatcher>
#include <QDateTime>
#include <QDebug>
#include <QHash>
#include <QList>
#include <QMetaObject>
#include <QThread>
#include <QStringList>
#include <QUrl>

#include <utility>

namespace QCurl {

namespace Internal {

using ReplyKey = QObject *;

enum class ScheduledState {
    Pending,
    Deferred,
    Running,
};

/// 保存请求入队时供调度和信号使用的稳定快照。
struct ReplySnapshot
{
    // 所有 scheduler 信号都复用入队时的快照，避免 reply 后续状态漂移污染观测值。
    QString lane;
    QString hostKey;
    QCNetworkRequestPriority priority = QCNetworkRequestPriority::Normal;
};

/// 保存 reply 结束或取消时提取的结果摘要。
struct ReplyOutcome
{
    bool cancelled       = false;
    qint64 bytesReceived = 0;
};

enum class FinalizeTrigger {
    FinishedSignal,
    ExplicitCancel,
    Destroyed,
};

/// 保存 reply 收尾后需要执行的通知和队列动作。
struct FinalizeResult
{
    ReplySnapshot snapshot;
    bool wasTracked      = false;
    bool emitCancelled   = false;
    bool emitFinished    = false;
    bool shouldKickQueue = false;
};

/// 保存单个 reply 的进度值及相关信号连接。
struct ReplyProgressState
{
    qint64 lastBytesReceived = 0;
    qint64 lastBytesSent     = 0;
    QMetaObject::Connection downloadConnection;
    QMetaObject::Connection uploadConnection;
};

class LaneSchedulingPolicy;
class LaneRuntimePruner;

using SchedulerRequestId = quint64;

QString normalizedLane(const QString &lane);
bool hasEventDispatcher(QThread *thread);
ReplyKey replyKey(QCNetworkReply *reply);
QCNetworkReply *replyFromKey(ReplyKey key);

template <typename Functor>
void invokeOnSchedulerOwnerThread(QCNetworkRequestScheduler *scheduler,
                                  Functor &&functor,
                                  const char *context)
{
    // fire-and-forget 接口统一回 owner thread，避免误触调用线程的 thread-local scheduler。
    if (QThread::currentThread() == scheduler->thread()) {
        std::forward<Functor>(functor)();
        return;
    }

    if (!hasEventDispatcher(scheduler->thread())) {
        qWarning() << context
                   << ": scheduler owner thread has no Qt event dispatcher; queued call is dropped";
        return;
    }

    QMetaObject::invokeMethod(scheduler, std::forward<Functor>(functor), Qt::QueuedConnection);
}

template <typename Result>
Result rejectOffOwnerThreadValue(const QCNetworkRequestScheduler *scheduler,
                                 Result fallback,
                                 const char *context)
{
    Q_UNUSED(scheduler);
    qWarning() << context
               << ": must be called on scheduler owner thread; cross-thread value queries are rejected";
    Q_ASSERT_X(QThread::currentThread() == scheduler->thread(),
               context,
               "QCNetworkRequestScheduler value query must run on its owner thread");
    return fallback;
}

void assertSchedulerOwnerThread(const QCNetworkRequestScheduler *scheduler, const char *context);
ReplyOutcome captureReplyOutcome(QCNetworkReply *reply);
void invokeReplyCancel(QCNetworkReply *reply);
int effectivePort(const QUrl &url);
QString buildHostKey(const QUrl &url);
QCNetworkRequestScheduler::LaneConfig sanitizedLaneConfig(
    QCNetworkRequestScheduler::LaneConfig config);
const QList<QCNetworkRequestPriority> &priorityOrder();
int nestedCounter(const QHash<QString, QHash<QString, int>> &counters,
                  const QString &lane,
                  const QString &hostKey);
void incrementNestedCounter(QHash<QString, QHash<QString, int>> &counters,
                            const QString &lane,
                            const QString &hostKey);
void decrementNestedCounter(QHash<QString, QHash<QString, int>> &counters,
                            const QString &lane,
                            const QString &hostKey);

class SchedulerQueues
{
    friend class LaneSchedulingPolicy;
    friend class LaneRuntimePruner;

public:
    /// 保存队列项的标识、reply、快照和入队时间。
    struct QueuedRequest
    {
        SchedulerRequestId requestId = 0;
        ReplyKey key                 = nullptr;
        QCNetworkReply *reply        = nullptr;
        ReplySnapshot snapshot;
        QDateTime queueTime;
    };

    /// 将请求加入 pending 队列。
    Q_ALWAYS_INLINE void enqueuePending(QueuedRequest request)
    {
        m_pendingRequests.append(std::move(request));
    }

    /// 将 pending 请求移入 deferred 队列。
    [[nodiscard]] Q_ALWAYS_INLINE bool deferPending(ReplyKey key)
    {
        QueuedRequest request;
        if (!takeQueuedRequest(m_pendingRequests, key, &request)) {
            return false;
        }
        m_deferredRequests.append(request);
        return true;
    }

    /// 将 deferred 请求恢复到 pending 队列并返回其最新快照。
    [[nodiscard]] Q_ALWAYS_INLINE bool resumeDeferred(ReplyKey key, QueuedRequest *out)
    {
        QueuedRequest request;
        if (!takeQueuedRequest(m_deferredRequests, key, &request)) {
            return false;
        }
        request.queueTime = QDateTime::currentDateTime();
        if (out) {
            *out = request;
        }
        m_pendingRequests.append(request);
        return true;
    }

    /// 从 pending 队列移除指定请求。
    [[nodiscard]] Q_ALWAYS_INLINE bool takePending(ReplyKey key)
    {
        return takeQueuedRequest(m_pendingRequests, key);
    }

    /// 从 deferred 队列移除指定请求。
    [[nodiscard]] Q_ALWAYS_INLINE bool takeDeferred(ReplyKey key)
    {
        return takeQueuedRequest(m_deferredRequests, key);
    }

    /// 按当前调度策略取出下一项 pending 请求。
    [[nodiscard]] Q_DECL_HIDDEN bool takeNextPending(const QCNetworkRequestScheduler::Config &config,
                                                     QueuedRequest *out);
    /// 更新 pending 请求优先级并返回更新后的快照。
    [[nodiscard]] Q_DECL_HIDDEN bool updatePendingPriority(ReplyKey key,
                                                           QCNetworkRequestPriority priority,
                                                           ReplySnapshot *snapshot);
    /// 返回 pending 队列的只读视图。
    [[nodiscard]] Q_ALWAYS_INLINE const QList<QueuedRequest> &pendingRequests() const
    {
        return m_pendingRequests;
    }

    /// 返回 deferred 队列的只读视图。
    [[nodiscard]] Q_ALWAYS_INLINE const QList<QueuedRequest> &deferredRequests() const
    {
        return m_deferredRequests;
    }

    /// 返回当前仍存活的 running reply。
    [[nodiscard]] QList<QCNetworkReply *> runningRequests() const;

    /// 返回 pending 请求数量。
    [[nodiscard]] Q_ALWAYS_INLINE int pendingCount() const { return m_pendingRequests.size(); }

    /// 返回 running 请求数量。
    [[nodiscard]] Q_ALWAYS_INLINE int runningCount() const { return m_runningRequests.size(); }

    /// 判断 pending 队列是否非空。
    [[nodiscard]] Q_ALWAYS_INLINE bool hasPendingRequests() const
    {
        return !m_pendingRequests.isEmpty();
    }

    /// 清除 lane 配置并重置临时调度状态。
    void clearLaneConfigs();
    /// 确保 lane 已进入轮转状态。
    void ensureLane(const QString &lane);
    /// 保存已清理的 lane 配置。
    void setLaneConfig(const QString &lane, const QCNetworkRequestScheduler::LaneConfig &config);
    /// 返回 lane 配置；未知 lane 使用默认配置。
    [[nodiscard]] QCNetworkRequestScheduler::LaneConfig laneConfigFor(const QString &lane) const;
    /// 将已取出的请求登记为 running 并更新计数。
    void markRunning(const QueuedRequest &request);
    /// 移除 running 请求并回收相关计数。
    [[nodiscard]] bool removeRunning(ReplyKey key, const ReplySnapshot &snapshot);
    /// 重置游标、deficit 和临时 lane 运行状态。
    void resetRuntimeState();

private:
    template <typename Predicate>
    static int findQueuedRequestIndex(const QList<QueuedRequest> &requests, Predicate &&predicate)
    {
        for (int i = 0; i < requests.size(); ++i) {
            if (predicate(requests.at(i))) {
                return i;
            }
        }
        return -1;
    }

    static bool takeQueuedRequest(QList<QueuedRequest> &requests,
                                  ReplyKey key,
                                  QueuedRequest *out = nullptr);
    [[nodiscard]] int selectNextIndex(const QCNetworkRequestScheduler::Config &config);

    QList<QueuedRequest> m_pendingRequests;
    QList<QueuedRequest> m_deferredRequests;
    QList<QueuedRequest> m_runningRequests;
    QHash<QString, int> m_hostConnectionCount;
    QHash<QString, int> m_runningLaneCount;
    QHash<QString, QHash<QString, int>> m_runningLaneHostCount;
    QHash<QString, QCNetworkRequestScheduler::LaneConfig> m_laneConfigs;
    QStringList m_laneOrder;
    QHash<QString, int> m_laneDeficit;
    QHash<QString, QString> m_laneLastStartedHost;

    [[nodiscard]] QStringList rotatedLaneHosts(const QString &lane) const;
    [[nodiscard]] bool hasRunnablePendingExcluding(
        const QString &lane,
        ReplyKey excludeKey,
        const QHash<QString, int> &hostCounts,
        const QCNetworkRequestScheduler::Config &config) const;
    [[nodiscard]] bool hasRunnablePendingForHostExcluding(
        const QString &lane,
        const QString &hostKey,
        ReplyKey excludeKey,
        const QHash<QString, int> &hostCounts,
        const QCNetworkRequestScheduler::Config &config) const;
    [[nodiscard]] bool wouldViolateReservation(
        const QueuedRequest &candidate,
        const QCNetworkRequestScheduler::Config &config) const;

    template <typename Predicate>
    int candidateIndexForLane(const QString &lane, Predicate &&predicate) const
    {
        const QStringList hosts = rotatedLaneHosts(lane);

        for (const auto priority : priorityOrder()) {
            for (const auto &host : hosts) {
                const int index
                    = findQueuedRequestIndex(m_pendingRequests, [&](const QueuedRequest &request) {
                          return request.reply && request.snapshot.lane == lane
                                 && request.snapshot.priority == priority
                                 && request.snapshot.hostKey == host && predicate(request);
                      });
                if (index >= 0) {
                    return index;
                }
            }
        }

        return -1;
    }

    [[nodiscard]] int selectReservationHostIndex(
        const QCNetworkRequestScheduler::Config &config);
    [[nodiscard]] int selectReservationGlobalIndex(
        const QCNetworkRequestScheduler::Config &config);
    [[nodiscard]] int selectBestEffortIndex(const QCNetworkRequestScheduler::Config &config);

    int m_hostReservationCursor   = 0;
    int m_globalReservationCursor = 0;
    int m_bestEffortCursor        = 0;
};

} // namespace Internal

} // namespace QCurl

#endif // QCNETWORKREQUESTSCHEDULERQUEUE_P_H
