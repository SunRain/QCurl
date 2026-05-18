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

struct ReplySnapshot
{
    // 所有 scheduler 信号都复用入队时的快照，避免 reply 后续状态漂移污染观测值。
    QString lane;
    QString hostKey;
    QCNetworkRequestPriority priority = QCNetworkRequestPriority::Normal;
};

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

struct FinalizeResult
{
    ReplySnapshot snapshot;
    bool wasTracked      = false;
    bool emitCancelled   = false;
    bool emitFinished    = false;
    bool shouldKickQueue = false;
};

struct ReplyProgressState
{
    qint64 lastBytesReceived = 0;
    qint64 lastBytesSent     = 0;
    QMetaObject::Connection downloadConnection;
    QMetaObject::Connection uploadConnection;
};

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
public:
    struct QueuedRequest
    {
        ReplyKey key = nullptr;
        QCNetworkReply *reply = nullptr;
        ReplySnapshot snapshot;
        QDateTime queueTime;
    };

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

    void ensureLane(const QString &lane);
    void setLaneConfig(const QString &lane,
                       const QCNetworkRequestScheduler::LaneConfig &config);
    [[nodiscard]] QCNetworkRequestScheduler::LaneConfig laneConfigFor(
        const QString &lane) const;
    [[nodiscard]] int selectNextIndex(const QCNetworkRequestScheduler::Config &config);
    void markRunning(const QueuedRequest &request);
    [[nodiscard]] bool removeRunning(QCNetworkReply *reply, const ReplySnapshot &snapshot);
    void resetRuntimeState();

    QList<QueuedRequest> pendingRequests;
    QList<QueuedRequest> deferredRequests;
    QList<QCNetworkReply *> runningRequests;
    QHash<QString, int> hostConnectionCount;
    QHash<QString, int> runningLaneCount;
    QHash<QString, QHash<QString, int>> runningLaneHostCount;
    QHash<QString, QCNetworkRequestScheduler::LaneConfig> laneConfigs;
    QStringList laneOrder;
    QHash<QString, int> laneDeficit;
    QHash<QString, QString> laneLastStartedHost;

private:
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
                const int index = findQueuedRequestIndex(
                    pendingRequests,
                    [&](const QueuedRequest &request) {
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

    int hostReservationCursor   = 0;
    int globalReservationCursor = 0;
    int bestEffortCursor        = 0;
};

} // namespace Internal

} // namespace QCurl

#endif // QCNETWORKREQUESTSCHEDULERQUEUE_P_H
