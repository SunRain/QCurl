// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkRequestScheduler.h"

#include "QCNetworkReply.h"

#include <QAbstractEventDispatcher>
#include <QDateTime>
#include <QHash>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <utility>

namespace QCurl {

namespace {

using ReplyKey = QObject *;

enum class ScheduledState {
    Pending,
    Deferred,
    Running,
};

struct ReplySnapshot
{
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
    bool wasTracked     = false;
    bool emitCancelled  = false;
    bool emitFinished   = false;
    bool shouldKickQueue = false;
};

struct ReplyProgressState
{
    qint64 lastBytesReceived = 0;
    qint64 lastBytesSent     = 0;
    QMetaObject::Connection downloadConnection;
    QMetaObject::Connection uploadConnection;
};

QString normalizedLane(const QString &lane)
{
    return lane.trimmed();
}

bool hasEventDispatcher(QThread *thread)
{
    return (thread != nullptr) && (QAbstractEventDispatcher::instance(thread) != nullptr);
}

ReplyKey replyKey(QCNetworkReply *reply)
{
    return static_cast<QObject *>(reply);
}

QCNetworkReply *replyFromKey(ReplyKey key)
{
    return static_cast<QCNetworkReply *>(key);
}

template <typename Functor>
void invokeOnSchedulerOwnerThread(QCNetworkRequestScheduler *scheduler,
                                  Functor &&functor,
                                  const char *context)
{
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

template <typename Result, typename Functor>
Result invokeOnSchedulerOwnerThreadBlocking(QCNetworkRequestScheduler *scheduler,
                                            Result fallback,
                                            Functor &&functor,
                                            const char *context)
{
    if (QThread::currentThread() == scheduler->thread()) {
        return std::forward<Functor>(functor)();
    }

    if (!hasEventDispatcher(scheduler->thread())) {
        qWarning() << context
                   << ": scheduler owner thread has no Qt event dispatcher; blocking call is rejected";
        return fallback;
    }

    Result result = fallback;
    const bool invoked = QMetaObject::invokeMethod(
        scheduler,
        [functor = std::forward<Functor>(functor), &result]() mutable { result = functor(); },
        Qt::BlockingQueuedConnection);
    if (!invoked) {
        qWarning() << context << ": failed to marshal call back to scheduler owner thread";
        return fallback;
    }

    return result;
}

void assertSchedulerOwnerThread(const QCNetworkRequestScheduler *scheduler, const char *context)
{
    Q_ASSERT_X(QThread::currentThread() == scheduler->thread(),
               context,
               "QCNetworkRequestScheduler must run on its owner thread");
}

ReplyOutcome captureReplyOutcome(QCNetworkReply *reply)
{
    ReplyOutcome outcome;
    if (!reply) {
        return outcome;
    }

    outcome.cancelled = reply->state() == ReplyState::Cancelled
                        || reply->error() == NetworkError::OperationCancelled;
    outcome.bytesReceived = reply->bytesReceived();
    return outcome;
}

void invokeReplyCancel(QCNetworkReply *reply)
{
    if (!reply) {
        return;
    }

    QPointer<QCNetworkReply> safeReply(reply);
    QMetaObject::invokeMethod(
        reply,
        [safeReply]() {
            if (safeReply) {
                safeReply->cancel();
            }
        },
        Qt::QueuedConnection);
}

int effectivePort(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    const int explicitPort = url.port(-1);
    if (explicitPort > 0) {
        return explicitPort;
    }
    if (scheme == QStringLiteral("https")) {
        return 443;
    }
    if (scheme == QStringLiteral("http")) {
        return 80;
    }
    if (scheme == QStringLiteral("wss")) {
        return 443;
    }
    if (scheme == QStringLiteral("ws")) {
        return 80;
    }
    return 0;
}

QString buildHostKey(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    QString host = url.host().toLower();
    if (host.contains(QLatin1Char(':')) && !host.startsWith(QLatin1Char('['))) {
        host = QStringLiteral("[%1]").arg(host);
    }
    const int port = effectivePort(url);
    return QStringLiteral("%1://%2:%3").arg(scheme, host).arg(port);
}

QCNetworkRequestScheduler::LaneConfig sanitizedLaneConfig(
    QCNetworkRequestScheduler::LaneConfig config)
{
    if (config.weight <= 0) {
        config.weight = 1;
    }
    if (config.quantum <= 0) {
        config.quantum = 1;
    }
    if (config.reservedGlobal < 0) {
        config.reservedGlobal = 0;
    }
    if (config.reservedPerHost < 0) {
        config.reservedPerHost = 0;
    }
    return config;
}

const QList<QCNetworkRequestPriority> &priorityOrder()
{
    static const QList<QCNetworkRequestPriority> kOrder = {
        QCNetworkRequestPriority::Critical,
        QCNetworkRequestPriority::VeryHigh,
        QCNetworkRequestPriority::High,
        QCNetworkRequestPriority::Normal,
        QCNetworkRequestPriority::Low,
        QCNetworkRequestPriority::VeryLow,
    };
    return kOrder;
}

int nestedCounter(const QHash<QString, QHash<QString, int>> &counters,
                  const QString &lane,
                  const QString &hostKey)
{
    return counters.value(lane).value(hostKey, 0);
}

void incrementNestedCounter(QHash<QString, QHash<QString, int>> &counters,
                            const QString &lane,
                            const QString &hostKey)
{
    counters[lane][hostKey]++;
}

void decrementNestedCounter(QHash<QString, QHash<QString, int>> &counters,
                            const QString &lane,
                            const QString &hostKey)
{
    auto laneIt = counters.find(lane);
    if (laneIt == counters.end()) {
        return;
    }

    auto hostIt = laneIt->find(hostKey);
    if (hostIt == laneIt->end()) {
        return;
    }

    (*hostIt)--;
    if (*hostIt <= 0) {
        laneIt->erase(hostIt);
    }
    if (laneIt->isEmpty()) {
        counters.erase(laneIt);
    }
}

} // namespace

struct QCNetworkRequestScheduler::Impl
{
    struct QueuedRequest
    {
        ReplyKey key           = nullptr;
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
                                  QueuedRequest *out = nullptr)
    {
        const int index = findQueuedRequestIndex(
            requests, [key](const QueuedRequest &request) { return request.key == key; });
        if (index < 0) {
            return false;
        }

        if (out) {
            *out = requests.at(index);
        }
        requests.removeAt(index);
        return true;
    }

    static QStringList rotatedLaneHosts(const QList<QueuedRequest> &requests,
                                        const QString &lane,
                                        const QString &lastStartedHost)
    {
        QStringList hosts;
        QSet<QString> seenHosts;

        for (const auto &request : requests) {
            if (!request.reply || request.snapshot.lane != lane) {
                continue;
            }
            if (seenHosts.contains(request.snapshot.hostKey)) {
                continue;
            }
            hosts.append(request.snapshot.hostKey);
            seenHosts.insert(request.snapshot.hostKey);
        }

        if (hosts.size() <= 1 || lastStartedHost.isEmpty()) {
            return hosts;
        }

        const int lastIndex = hosts.indexOf(lastStartedHost);
        if (lastIndex < 0) {
            return hosts;
        }

        QStringList rotated;
        rotated.reserve(hosts.size());
        for (int i = 1; i <= hosts.size(); ++i) {
            rotated.append(hosts.at((lastIndex + i) % hosts.size()));
        }
        return rotated;
    }

    QCNetworkRequestScheduler::LaneConfig laneConfigFor(const QString &lane) const
    {
        return laneConfigs.value(lane, QCNetworkRequestScheduler::LaneConfig{});
    }

    bool hasRunnablePendingExcluding(const QString &lane,
                                     ReplyKey excludeKey,
                                     const QHash<QString, int> &hostCounts) const
    {
        for (const auto &request : pendingRequests) {
            if (!request.reply || request.key == excludeKey) {
                continue;
            }
            if (request.snapshot.lane != lane) {
                continue;
            }
            if (hostCounts.value(request.snapshot.hostKey, 0) < config.maxRequestsPerHost) {
                return true;
            }
        }
        return false;
    }

    bool hasRunnablePendingForHostExcluding(const QString &lane,
                                            const QString &hostKey,
                                            ReplyKey excludeKey,
                                            const QHash<QString, int> &hostCounts) const
    {
        for (const auto &request : pendingRequests) {
            if (!request.reply || request.key == excludeKey) {
                continue;
            }
            if (request.snapshot.lane != lane || request.snapshot.hostKey != hostKey) {
                continue;
            }
            if (hostCounts.value(hostKey, 0) < config.maxRequestsPerHost) {
                return true;
            }
        }
        return false;
    }

    bool wouldViolateReservation(const QueuedRequest &candidate) const
    {
        QHash<QString, int> hostCounts = hostConnectionCount;
        hostCounts[candidate.snapshot.hostKey]++;
        if (hostCounts.value(candidate.snapshot.hostKey, 0) > config.maxRequestsPerHost) {
            return true;
        }

        QHash<QString, int> laneCounts = runningLaneCount;
        laneCounts[candidate.snapshot.lane]++;

        QHash<QString, QHash<QString, int>> laneHostCounts = runningLaneHostCount;
        incrementNestedCounter(laneHostCounts, candidate.snapshot.lane, candidate.snapshot.hostKey);

        const int freeGlobalAfter = config.maxConcurrentRequests - (runningRequests.size() + 1);
        int globalDemand          = 0;

        QHash<QString, int> hostDemand;
        for (const auto &lane : laneOrder) {
            const auto laneCfg = laneConfigFor(lane);
            if (laneCfg.reservedGlobal > 0
                && hasRunnablePendingExcluding(lane, candidate.key, hostCounts)) {
                globalDemand += qMax(0, laneCfg.reservedGlobal - laneCounts.value(lane, 0));
            }

            if (laneCfg.reservedPerHost <= 0) {
                continue;
            }

            QSet<QString> hosts;
            for (const auto &request : pendingRequests) {
                if (!request.reply || request.key == candidate.key) {
                    continue;
                }
                if (request.snapshot.lane != lane) {
                    continue;
                }
                hosts.insert(request.snapshot.hostKey);
            }

            for (const auto &hostKey : hosts) {
                if (!hasRunnablePendingForHostExcluding(lane, hostKey, candidate.key, hostCounts)) {
                    continue;
                }
                hostDemand[hostKey]
                    += qMax(0, laneCfg.reservedPerHost - nestedCounter(laneHostCounts, lane, hostKey));
            }
        }

        if (globalDemand > freeGlobalAfter) {
            return true;
        }

        for (auto it = hostDemand.cbegin(); it != hostDemand.cend(); ++it) {
            const int freeHostAfter = config.maxRequestsPerHost - hostCounts.value(it.key(), 0);
            if (it.value() > freeHostAfter) {
                return true;
            }
        }

        return false;
    }

    template <typename Predicate>
    int candidateIndexForLane(const QString &lane, Predicate &&predicate) const
    {
        const QStringList hosts = rotatedLaneHosts(
            pendingRequests, lane, laneLastStartedHost.value(lane));

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

    int selectReservationHostIndex()
    {
        QStringList activeLanes;
        for (const auto &lane : laneOrder) {
            const auto laneCfg = laneConfigFor(lane);
            if (laneCfg.reservedPerHost <= 0) {
                continue;
            }

            const bool active = findQueuedRequestIndex(
                pendingRequests,
                [&](const QueuedRequest &request) {
                    return request.reply && request.snapshot.lane == lane
                           && hostConnectionCount.value(request.snapshot.hostKey, 0)
                                  < config.maxRequestsPerHost
                           && nestedCounter(runningLaneHostCount, lane, request.snapshot.hostKey)
                                  < laneCfg.reservedPerHost;
                })
                >= 0;

            if (active) {
                activeLanes.append(lane);
            }
        }

        if (activeLanes.isEmpty()) {
            return -1;
        }

        const int laneCount = laneOrder.size();
        for (int attempt = 0; attempt < laneCount; ++attempt) {
            const QString &lane = laneOrder.at((hostReservationCursor + attempt) % laneCount);
            if (!activeLanes.contains(lane)) {
                continue;
            }

            const auto laneCfg = laneConfigFor(lane);
            const int index    = candidateIndexForLane(
                lane,
                [&](const QueuedRequest &request) {
                    return hostConnectionCount.value(request.snapshot.hostKey, 0)
                               < config.maxRequestsPerHost
                           && nestedCounter(runningLaneHostCount, lane, request.snapshot.hostKey)
                                  < laneCfg.reservedPerHost;
                });
            if (index >= 0) {
                hostReservationCursor = (laneOrder.indexOf(lane) + 1) % laneCount;
                return index;
            }
        }

        return -1;
    }

    int selectReservationGlobalIndex()
    {
        QStringList activeLanes;
        for (const auto &lane : laneOrder) {
            const auto laneCfg = laneConfigFor(lane);
            if (laneCfg.reservedGlobal <= 0) {
                continue;
            }

            if (runningLaneCount.value(lane, 0) >= laneCfg.reservedGlobal) {
                continue;
            }

            const bool active = findQueuedRequestIndex(
                pendingRequests,
                [&](const QueuedRequest &request) {
                    return request.reply && request.snapshot.lane == lane
                           && hostConnectionCount.value(request.snapshot.hostKey, 0)
                                  < config.maxRequestsPerHost;
                })
                >= 0;
            if (active) {
                activeLanes.append(lane);
            }
        }

        if (activeLanes.isEmpty()) {
            return -1;
        }

        const int laneCount = laneOrder.size();
        for (int attempt = 0; attempt < laneCount; ++attempt) {
            const QString &lane = laneOrder.at((globalReservationCursor + attempt) % laneCount);
            if (!activeLanes.contains(lane)) {
                continue;
            }

            const int index = candidateIndexForLane(
                lane,
                [&](const QueuedRequest &request) {
                    return hostConnectionCount.value(request.snapshot.hostKey, 0)
                           < config.maxRequestsPerHost;
                });
            if (index >= 0) {
                globalReservationCursor = (laneOrder.indexOf(lane) + 1) % laneCount;
                return index;
            }
        }

        return -1;
    }

    int selectBestEffortIndex()
    {
        if (laneOrder.isEmpty()) {
            return -1;
        }

        const int laneCount = laneOrder.size();
        for (int attempt = 0; attempt < laneCount; ++attempt) {
            const int laneIndex = (bestEffortCursor + attempt) % laneCount;
            const QString &lane = laneOrder.at(laneIndex);

            const bool laneHasPending = findQueuedRequestIndex(
                pendingRequests,
                [&](const QueuedRequest &request) {
                    return request.reply && request.snapshot.lane == lane;
                })
                >= 0;
            if (!laneHasPending) {
                continue;
            }

            const auto laneCfg = laneConfigFor(lane);
            if (laneDeficit.value(lane, 0) < 1) {
                laneDeficit[lane] += qMax(1, laneCfg.weight * qMax(1, laneCfg.quantum));
            }

            if (laneDeficit.value(lane, 0) < 1) {
                continue;
            }

            const int index = candidateIndexForLane(
                lane,
                [&](const QueuedRequest &request) {
                    return hostConnectionCount.value(request.snapshot.hostKey, 0)
                               < config.maxRequestsPerHost
                           && !wouldViolateReservation(request);
                });
            if (index >= 0) {
                laneDeficit[lane] -= 1;
                if (laneDeficit.value(lane, 0) < 1) {
                    bestEffortCursor = (laneIndex + 1) % laneCount;
                } else {
                    bestEffortCursor = laneIndex;
                }
                return index;
            }

            laneDeficit[lane] = 0;
        }

        return -1;
    }

    mutable QMutex mutex;
    QCNetworkRequestScheduler::Config config;
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
    int hostReservationCursor   = 0;
    int globalReservationCursor = 0;
    int bestEffortCursor        = 0;
    QCNetworkRequestScheduler::Statistics stats;
    QHash<ReplyKey, QDateTime> requestStartTimes;
    QHash<ReplyKey, ReplySnapshot> replySnapshots;
    QHash<ReplyKey, ScheduledState> replyStates;
    QHash<ReplyKey, ReplyProgressState> replyProgressStates;
    QSet<ReplyKey> cancelledReplies;
    quint64 nextStartTicket = 1;
    QHash<ReplyKey, quint64> startTickets;
    QTimer *throttleTimer           = nullptr;
    qint64 bytesTransferredInWindow = 0;

    void disconnectProgressTracking(ReplyKey key)
    {
        auto it = replyProgressStates.find(key);
        if (it == replyProgressStates.end()) {
            return;
        }

        QObject::disconnect(it->downloadConnection);
        QObject::disconnect(it->uploadConnection);
        replyProgressStates.erase(it);
    }

    void clearReplyTracking(ReplyKey key)
    {
        disconnectProgressTracking(key);
        requestStartTimes.remove(key);
        replyStates.remove(key);
        replySnapshots.remove(key);
        cancelledReplies.remove(key);
        startTickets.remove(key);
    }

    bool isStartTicketValidLocked(ReplyKey key, quint64 ticket) const
    {
        if (!key) {
            return false;
        }

        if (cancelledReplies.contains(key)) {
            return false;
        }

        const auto stateIt = replyStates.constFind(key);
        if (stateIt == replyStates.constEnd() || stateIt.value() != ScheduledState::Running) {
            return false;
        }

        const auto ticketIt = startTickets.constFind(key);
        if (ticketIt == startTickets.constEnd() || ticketIt.value() != ticket) {
            return false;
        }

        return true;
    }

    void resetSchedulingRuntimeState()
    {
        QStringList configuredLaneOrder;
        configuredLaneOrder.reserve(laneOrder.size());
        for (const QString &lane : std::as_const(laneOrder)) {
            if (!laneConfigs.contains(lane) || configuredLaneOrder.contains(lane)) {
                continue;
            }
            configuredLaneOrder.append(lane);
        }

        laneOrder = configuredLaneOrder;
        laneDeficit.clear();
        for (const QString &lane : std::as_const(laneOrder)) {
            laneDeficit.insert(lane, 0);
        }
        laneLastStartedHost.clear();
        hostReservationCursor   = 0;
        globalReservationCursor = 0;
        bestEffortCursor        = 0;
    }
    FinalizeResult finalizeReplyLocked(ReplyKey key,
                                       FinalizeTrigger trigger,
                                       const ReplyOutcome &outcome = {})
    {
        FinalizeResult result;
        if (!key) {
            return result;
        }

        if (!replySnapshots.contains(key) && !replyStates.contains(key)
            && !cancelledReplies.contains(key)) {
            return result;
        }

        result.wasTracked = true;
        result.snapshot   = replySnapshots.value(key);

        if (trigger == FinalizeTrigger::FinishedSignal && cancelledReplies.remove(key)) {
            disconnectProgressTracking(key);
            return result;
        }

        const ScheduledState state = replyStates.value(key, ScheduledState::Pending);

        if (state == ScheduledState::Pending) {
            if (takeQueuedRequest(pendingRequests, key)) {
                stats.pendingRequests = pendingRequests.size();
                result.shouldKickQueue = trigger != FinalizeTrigger::ExplicitCancel;
            }
        } else if (state == ScheduledState::Deferred) {
            if (takeQueuedRequest(deferredRequests, key)) {
                result.shouldKickQueue = trigger == FinalizeTrigger::Destroyed;
            }
        } else if (runningRequests.removeOne(replyFromKey(key))) {
            stats.runningRequests = runningRequests.size();
            runningLaneCount[result.snapshot.lane]--;
            if (runningLaneCount.value(result.snapshot.lane, 0) <= 0) {
                runningLaneCount.remove(result.snapshot.lane);
            }
            hostConnectionCount[result.snapshot.hostKey]--;
            if (hostConnectionCount.value(result.snapshot.hostKey, 0) <= 0) {
                hostConnectionCount.remove(result.snapshot.hostKey);
            }
            decrementNestedCounter(
                runningLaneHostCount, result.snapshot.lane, result.snapshot.hostKey);
            result.shouldKickQueue = true;
        }

        if (trigger == FinalizeTrigger::ExplicitCancel) {
            disconnectProgressTracking(key);
            requestStartTimes.remove(key);
            replyStates.remove(key);
            replySnapshots.remove(key);
            startTickets.remove(key);
            cancelledReplies.insert(key);
            stats.cancelledRequests++;
            result.emitCancelled = true;
            return result;
        }

        if (trigger == FinalizeTrigger::FinishedSignal) {
            if (outcome.cancelled) {
                stats.cancelledRequests++;
                result.emitCancelled = true;
            } else if (state == ScheduledState::Running) {
                const QDateTime finishTime = QDateTime::currentDateTime();
                const QDateTime startTime  = requestStartTimes.value(key);
                const qint64 duration      = startTime.isValid() ? startTime.msecsTo(finishTime) : 0;

                stats.completedRequests++;
                if (stats.completedRequests == 1) {
                    stats.avgResponseTime = duration;
                } else {
                    stats.avgResponseTime
                        = (stats.avgResponseTime * (stats.completedRequests - 1) + duration)
                          / stats.completedRequests;
                }
                stats.totalBytesReceived += outcome.bytesReceived;
                result.emitFinished = true;
            }
        }

        clearReplyTracking(key);
        return result;
    }
};

QCNetworkRequestScheduler *QCNetworkRequestScheduler::instance()
{
    static thread_local QCNetworkRequestScheduler instance;
    return &instance;
}

QCNetworkRequestScheduler::QCNetworkRequestScheduler(QObject *parent)
    : QObject(parent)
    , m_impl(new Impl)
{
    registerQCNetworkRequestPriorityMetaType();
    m_impl->throttleTimer = new QTimer(this);
    m_impl->throttleTimer->setInterval(1000);
    connect(m_impl->throttleTimer,
            &QTimer::timeout,
            this,
            &QCNetworkRequestScheduler::updateBandwidthStats);
    m_impl->throttleTimer->start();
}

QCNetworkRequestScheduler::~QCNetworkRequestScheduler()
{
    cancelAllRequests();
}

void QCNetworkRequestScheduler::setConfig(const Config &config)
{
    if (QThread::currentThread() != thread()) {
        invokeOnSchedulerOwnerThread(
            this, [this, config]() { setConfig(config); }, "QCNetworkRequestScheduler::setConfig");
        return;
    }
    assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::setConfig");

    {
        QMutexLocker locker(&m_impl->mutex);
        m_impl->config = config;
    }

    processQueue();
}

QCNetworkRequestScheduler::Config QCNetworkRequestScheduler::config() const
{
    if (QThread::currentThread() != thread()) {
        return invokeOnSchedulerOwnerThreadBlocking(
            const_cast<QCNetworkRequestScheduler *>(this),
            Config{},
            [this]() { return config(); },
            "QCNetworkRequestScheduler::config");
    }
    assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::config");

    QMutexLocker locker(&m_impl->mutex);
    return m_impl->config;
}

void QCNetworkRequestScheduler::setLaneConfig(const QString &lane, const LaneConfig &config)
{
    if (QThread::currentThread() != thread()) {
        invokeOnSchedulerOwnerThread(this,
                                     [this, lane, config]() { setLaneConfig(lane, config); },
                                     "QCNetworkRequestScheduler::setLaneConfig");
        return;
    }
    assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::setLaneConfig");

    const QString laneKey = normalizedLane(lane);
    const LaneConfig sanitized = sanitizedLaneConfig(config);

    {
        QMutexLocker locker(&m_impl->mutex);
        m_impl->laneConfigs.insert(laneKey, sanitized);
        if (!m_impl->laneOrder.contains(laneKey)) {
            m_impl->laneOrder.append(laneKey);
        }
        if (!m_impl->laneDeficit.contains(laneKey)) {
            m_impl->laneDeficit.insert(laneKey, 0);
        }
    }

    processQueue();
}

QCNetworkRequestScheduler::LaneConfig QCNetworkRequestScheduler::laneConfig(const QString &lane) const
{
    if (QThread::currentThread() != thread()) {
        return invokeOnSchedulerOwnerThreadBlocking(
            const_cast<QCNetworkRequestScheduler *>(this),
            LaneConfig{},
            [this, lane]() { return laneConfig(lane); },
            "QCNetworkRequestScheduler::laneConfig");
    }
    assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::laneConfig");

    QMutexLocker locker(&m_impl->mutex);
    return m_impl->laneConfigs.value(normalizedLane(lane), LaneConfig{});
}

void QCNetworkRequestScheduler::scheduleReply(QCNetworkReply *reply,
                                              const QString &lane,
                                              QCNetworkRequestPriority priority)
{
    if (!reply) {
        return;
    }

    if (QThread::currentThread() != thread()) {
        QPointer<QCNetworkReply> safeReply(reply);
        invokeOnSchedulerOwnerThread(
            this,
            [this, safeReply, lane, priority]() {
                if (safeReply) {
                    scheduleReply(safeReply.data(), lane, priority);
                }
            },
            "QCNetworkRequestScheduler::scheduleReply");
        return;
    }
    assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::scheduleReply");
    Q_ASSERT_X(reply->thread() == thread(),
               "QCNetworkRequestScheduler::scheduleReply",
               "reply must live on the scheduler owner thread");

    const ReplyKey key = replyKey(reply);
    const QString laneKey = normalizedLane(lane);
    const ReplySnapshot snapshot = {laneKey, buildHostKey(reply->url()), priority};

    {
        QMutexLocker locker(&m_impl->mutex);

        if (!m_impl->laneOrder.contains(laneKey)) {
            m_impl->laneOrder.append(laneKey);
        }
        if (!m_impl->laneDeficit.contains(laneKey)) {
            m_impl->laneDeficit.insert(laneKey, 0);
        }

        m_impl->replySnapshots.insert(key, snapshot);
        m_impl->replyStates.insert(key, ScheduledState::Pending);
        m_impl->pendingRequests.append({key, reply, snapshot, QDateTime::currentDateTime()});
        m_impl->stats.pendingRequests = m_impl->pendingRequests.size();
    }

    connect(reply, &QObject::destroyed, this, &QCNetworkRequestScheduler::onReplyDestroyed);

    QPointer<QCNetworkReply> safeReply(reply);
    connect(
        reply,
        &QCNetworkReply::finished,
        this,
        [this, safeReply]() {
            if (safeReply) {
                onRequestFinished(safeReply.data());
            }
        },
        Qt::QueuedConnection);

    emit requestQueued(reply, snapshot.lane, snapshot.hostKey, snapshot.priority);
    processQueue();
}

void QCNetworkRequestScheduler::processQueue()
{
    if (QThread::currentThread() != thread()) {
        invokeOnSchedulerOwnerThread(
            this, [this]() { processQueue(); }, "QCNetworkRequestScheduler::processQueue");
        return;
    }
    assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::processQueue");

    QList<QCNetworkReply *> toStart;
    bool shouldEmitQueueEmpty         = false;
    bool shouldEmitBandwidthThrottled = false;
    qint64 throttledBytesPerSec       = 0;

    {
        QMutexLocker locker(&m_impl->mutex);

        while (true) {
            if (m_impl->runningRequests.size() >= m_impl->config.maxConcurrentRequests) {
                break;
            }

            if (m_impl->config.enableThrottling && m_impl->config.maxBandwidthBytesPerSec > 0
                && m_impl->bytesTransferredInWindow >= m_impl->config.maxBandwidthBytesPerSec) {
                shouldEmitBandwidthThrottled = true;
                throttledBytesPerSec         = m_impl->bytesTransferredInWindow;
                break;
            }

            int nextIndex = m_impl->selectReservationHostIndex();
            if (nextIndex < 0) {
                nextIndex = m_impl->selectReservationGlobalIndex();
            }
            if (nextIndex < 0) {
                nextIndex = m_impl->selectBestEffortIndex();
            }
            if (nextIndex < 0) {
                break;
            }

            const Impl::QueuedRequest request = m_impl->pendingRequests.takeAt(nextIndex);
            m_impl->stats.pendingRequests     = m_impl->pendingRequests.size();
            m_impl->runningRequests.append(request.reply);
            m_impl->replyStates[request.key] = ScheduledState::Running;
            m_impl->requestStartTimes.insert(request.key, QDateTime::currentDateTime());
            m_impl->runningLaneCount[request.snapshot.lane]++;
            m_impl->hostConnectionCount[request.snapshot.hostKey]++;
            incrementNestedCounter(
                m_impl->runningLaneHostCount, request.snapshot.lane, request.snapshot.hostKey);
            m_impl->laneLastStartedHost.insert(request.snapshot.lane, request.snapshot.hostKey);
            m_impl->stats.runningRequests = m_impl->runningRequests.size();

            toStart.append(request.reply);
        }

        shouldEmitQueueEmpty = m_impl->pendingRequests.isEmpty();
    }

    if (shouldEmitBandwidthThrottled) {
        emit bandwidthThrottled(throttledBytesPerSec);
    }

    for (auto *reply : std::as_const(toStart)) {
        startRequest(reply);
    }

    if (shouldEmitQueueEmpty) {
        emit queueEmpty();
    }
}

void QCNetworkRequestScheduler::startRequest(QCNetworkReply *reply)
{
    if (!reply) {
        return;
    }

    if (QThread::currentThread() != thread()) {
        QPointer<QCNetworkReply> safeReply(reply);
        invokeOnSchedulerOwnerThread(
            this,
            [this, safeReply]() {
                if (safeReply) {
                    startRequest(safeReply.data());
                }
            },
            "QCNetworkRequestScheduler::startRequest");
        return;
    }
    assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::startRequest");
    Q_ASSERT_X(reply->thread() == thread(),
               "QCNetworkRequestScheduler::startRequest",
               "reply must live on the scheduler owner thread");

    const ReplyKey key = replyKey(reply);
    ReplySnapshot snapshot;
    quint64 startTicket = 0;
    {
        QMutexLocker locker(&m_impl->mutex);
        m_impl->disconnectProgressTracking(key);
        auto &progressState = m_impl->replyProgressStates[key];
        progressState.downloadConnection = connect(
            reply,
            &QCNetworkReply::downloadProgress,
            this,
            [this, key](qint64 bytesReceived, qint64 /*bytesTotal*/) {
                QMutexLocker locker(&m_impl->mutex);
                auto it = m_impl->replyProgressStates.find(key);
                if (it == m_impl->replyProgressStates.end()) {
                    return;
                }

                const qint64 delta = qMax<qint64>(0, bytesReceived - it->lastBytesReceived);
                it->lastBytesReceived = bytesReceived;
                m_impl->bytesTransferredInWindow += delta;
            },
            Qt::QueuedConnection);

        progressState.uploadConnection = connect(
            reply,
            &QCNetworkReply::uploadProgress,
            this,
            [this, key](qint64 bytesSent, qint64 /*bytesTotal*/) {
                QMutexLocker locker(&m_impl->mutex);
                auto it = m_impl->replyProgressStates.find(key);
                if (it == m_impl->replyProgressStates.end()) {
                    return;
                }

                const qint64 delta = qMax<qint64>(0, bytesSent - it->lastBytesSent);
                it->lastBytesSent = bytesSent;
                m_impl->bytesTransferredInWindow += delta;
            },
            Qt::QueuedConnection);

        snapshot = m_impl->replySnapshots.value(key);
        startTicket = m_impl->nextStartTicket++;
        m_impl->startTickets.insert(key, startTicket);
    }

    QPointer<QCNetworkRequestScheduler> safeScheduler(this);
    QPointer<QCNetworkReply> safeReply(reply);
    QMetaObject::invokeMethod(
        reply,
        [safeScheduler, safeReply, snapshot, key, startTicket]() {
            if (!safeScheduler || !safeReply) {
                return;
            }

            auto isStartStillValid = [&]() -> bool {
                QMutexLocker locker(&safeScheduler->m_impl->mutex);
                return safeScheduler->m_impl->isStartTicketValidLocked(key, startTicket);
            };

            if (!isStartStillValid()) {
                return;
            }

            if (safeReply->state() == ReplyState::Cancelled || safeReply->isFinished()) {
                return;
            }

            Q_EMIT safeScheduler->requestAboutToStart(safeReply.data(),
                                                      snapshot.lane,
                                                      snapshot.hostKey);

            if (!safeScheduler || !safeReply) {
                return;
            }

            // 允许在 requestAboutToStart 的 direct slot 中触发显式 cancel；
            // 该路径必须阻止后续 execute() 与 requestStarted。
            if (!isStartStillValid()) {
                return;
            }

            if (safeReply->state() == ReplyState::Cancelled || safeReply->isFinished()) {
                return;
            }

            safeReply->execute();

            if (!safeScheduler || !safeReply) {
                return;
            }

            // 防止“显式取消已生效 → 仍发 started”的违约序列。
            if (!isStartStillValid()) {
                return;
            }

            Q_EMIT safeScheduler->requestStarted(safeReply.data(), snapshot.lane, snapshot.hostKey);
        },
        Qt::QueuedConnection);
}

bool QCNetworkRequestScheduler::deferPendingRequest(QCNetworkReply *reply)
{
    if (!reply) {
        return false;
    }

    if (QThread::currentThread() != thread()) {
        QPointer<QCNetworkReply> safeReply(reply);
        return invokeOnSchedulerOwnerThreadBlocking(
            this,
            false,
            [this, safeReply]() {
                return safeReply ? deferPendingRequest(safeReply.data()) : false;
            },
            "QCNetworkRequestScheduler::deferPendingRequest");
    }
    assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::deferPendingRequest");

    const ReplyKey key = replyKey(reply);
    bool deferred = false;

    {
        QMutexLocker locker(&m_impl->mutex);

        Impl::QueuedRequest request;
        if (Impl::takeQueuedRequest(m_impl->pendingRequests, key, &request)) {
            m_impl->deferredRequests.append(request);
            m_impl->replyStates[key]      = ScheduledState::Deferred;
            m_impl->stats.pendingRequests = m_impl->pendingRequests.size();
            deferred = true;
        }
    }

    if (deferred) {
        processQueue();
    }

    return deferred;
}

void QCNetworkRequestScheduler::undeferRequest(QCNetworkReply *reply)
{
    if (!reply) {
        return;
    }

    if (QThread::currentThread() != thread()) {
        QPointer<QCNetworkReply> safeReply(reply);
        invokeOnSchedulerOwnerThread(
            this,
            [this, safeReply]() {
                if (safeReply) {
                    undeferRequest(safeReply.data());
                }
            },
            "QCNetworkRequestScheduler::undeferRequest");
        return;
    }
    assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::undeferRequest");

    const ReplyKey key = replyKey(reply);
    Impl::QueuedRequest request;
    {
        QMutexLocker locker(&m_impl->mutex);
        if (!Impl::takeQueuedRequest(m_impl->deferredRequests, key, &request)) {
            return;
        }
        request.queueTime = QDateTime::currentDateTime();
        m_impl->pendingRequests.append(request);
        m_impl->replyStates[key]      = ScheduledState::Pending;
        m_impl->stats.pendingRequests = m_impl->pendingRequests.size();
    }

    emit requestQueued(
        reply, request.snapshot.lane, request.snapshot.hostKey, request.snapshot.priority);
    processQueue();
}

void QCNetworkRequestScheduler::cancelRequest(QCNetworkReply *reply)
{
    if (!reply) {
        return;
    }

    if (QThread::currentThread() != thread()) {
        QPointer<QCNetworkReply> safeReply(reply);
        invokeOnSchedulerOwnerThread(
            this,
            [this, safeReply]() {
                if (safeReply) {
                    cancelRequest(safeReply.data());
                }
            },
            "QCNetworkRequestScheduler::cancelRequest");
        return;
    }
    assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::cancelRequest");

    const ReplyKey key = replyKey(reply);
    FinalizeResult result;

    {
        QMutexLocker locker(&m_impl->mutex);
        result = m_impl->finalizeReplyLocked(key, FinalizeTrigger::ExplicitCancel);
    }

    if (!result.wasTracked) {
        return;
    }

    invokeReplyCancel(reply);
    if (result.emitCancelled) {
        emit requestCancelled(reply, result.snapshot.lane, result.snapshot.hostKey);
    }

    if (result.shouldKickQueue) {
        processQueue();
    }
}

void QCNetworkRequestScheduler::cancelAllRequests()
{
    if (QThread::currentThread() != thread()) {
        invokeOnSchedulerOwnerThread(
            this,
            [this]() { cancelAllRequests(); },
            "QCNetworkRequestScheduler::cancelAllRequests");
        return;
    }
    assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::cancelAllRequests");

    QList<QCNetworkReply *> replies;
    QList<ReplySnapshot> snapshots;
    QList<ReplyKey> keysToCancel;

    {
        QMutexLocker locker(&m_impl->mutex);

        for (const auto &request : std::as_const(m_impl->pendingRequests)) {
            if (request.reply && !keysToCancel.contains(request.key)) {
                keysToCancel.append(request.key);
            }
        }
        for (const auto &request : std::as_const(m_impl->deferredRequests)) {
            if (request.reply && !keysToCancel.contains(request.key)) {
                keysToCancel.append(request.key);
            }
        }
        for (auto *reply : std::as_const(m_impl->runningRequests)) {
            const ReplyKey key = replyKey(reply);
            if (reply && !keysToCancel.contains(key)) {
                keysToCancel.append(key);
            }
        }

        for (ReplyKey key : std::as_const(keysToCancel)) {
            const FinalizeResult result
                = m_impl->finalizeReplyLocked(key, FinalizeTrigger::ExplicitCancel);
            if (result.wasTracked) {
                replies.append(replyFromKey(key));
                snapshots.append(result.snapshot);
            }
        }

        m_impl->bytesTransferredInWindow = 0;
        m_impl->resetSchedulingRuntimeState();
    }

    for (int i = 0; i < replies.size(); ++i) {
        auto *reply = replies.at(i);
        const ReplySnapshot &snapshot = snapshots.at(i);
        invokeReplyCancel(reply);
        emit requestCancelled(reply, snapshot.lane, snapshot.hostKey);
    }

    emit queueEmpty();
}

int QCNetworkRequestScheduler::cancelLaneRequests(const QString &lane, CancelLaneScope scope)
{
    if (QThread::currentThread() != thread()) {
        return invokeOnSchedulerOwnerThreadBlocking(
            this,
            0,
            [this, lane, scope]() { return cancelLaneRequests(lane, scope); },
            "QCNetworkRequestScheduler::cancelLaneRequests");
    }
    assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::cancelLaneRequests");

    const QString laneKey = normalizedLane(lane);
    QList<QCNetworkReply *> replies;
    QList<ReplySnapshot> snapshots;
    bool shouldKickQueue = false;
    QList<ReplyKey> keysToCancel;

    {
        QMutexLocker locker(&m_impl->mutex);

        auto collectKeysFromQueue = [&](const QList<Impl::QueuedRequest> &queue) {
            for (const auto &request : queue) {
                if (!request.reply || request.snapshot.lane != laneKey
                    || keysToCancel.contains(request.key)) {
                    continue;
                }
                keysToCancel.append(request.key);
            }
        };

        collectKeysFromQueue(m_impl->pendingRequests);
        collectKeysFromQueue(m_impl->deferredRequests);

        if (scope == CancelLaneScope::PendingAndRunning) {
            for (auto *reply : std::as_const(m_impl->runningRequests)) {
                const ReplyKey key = replyKey(reply);
                const ReplySnapshot snapshot = m_impl->replySnapshots.value(key);
                if (reply && snapshot.lane == laneKey && !keysToCancel.contains(key)) {
                    keysToCancel.append(key);
                }
            }
        }

        for (ReplyKey key : std::as_const(keysToCancel)) {
            const FinalizeResult result
                = m_impl->finalizeReplyLocked(key, FinalizeTrigger::ExplicitCancel);
            if (result.wasTracked) {
                replies.append(replyFromKey(key));
                snapshots.append(result.snapshot);
                shouldKickQueue = shouldKickQueue || result.shouldKickQueue;
            }
        }
    }

    for (int i = 0; i < replies.size(); ++i) {
        auto *reply = replies.at(i);
        const ReplySnapshot &snapshot = snapshots.at(i);
        invokeReplyCancel(reply);
        emit requestCancelled(reply, snapshot.lane, snapshot.hostKey);
    }

    if (shouldKickQueue) {
        processQueue();
    }

    return replies.size();
}

void QCNetworkRequestScheduler::changePriority(QCNetworkReply *reply,
                                               QCNetworkRequestPriority newPriority)
{
    if (!reply) {
        return;
    }

    if (QThread::currentThread() != thread()) {
        QPointer<QCNetworkReply> safeReply(reply);
        invokeOnSchedulerOwnerThread(
            this,
            [this, safeReply, newPriority]() {
                if (safeReply) {
                    changePriority(safeReply.data(), newPriority);
                }
            },
            "QCNetworkRequestScheduler::changePriority");
        return;
    }
    assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::changePriority");

    const ReplyKey key = replyKey(reply);
    ReplySnapshot snapshot;
    bool requeued = false;
    {
        QMutexLocker locker(&m_impl->mutex);
        const int index = Impl::findQueuedRequestIndex(
            m_impl->pendingRequests,
            [key](const Impl::QueuedRequest &request) { return request.key == key; });
        if (index < 0) {
            return;
        }

        m_impl->pendingRequests[index].snapshot.priority = newPriority;
        m_impl->replySnapshots[key].priority             = newPriority;
        snapshot                                         = m_impl->pendingRequests.at(index).snapshot;
        requeued                                         = true;
    }

    if (requeued) {
        emit requestQueued(reply, snapshot.lane, snapshot.hostKey, snapshot.priority);
        processQueue();
    }
}

QCNetworkRequestScheduler::Statistics QCNetworkRequestScheduler::statistics() const
{
    if (QThread::currentThread() != thread()) {
        return invokeOnSchedulerOwnerThreadBlocking(
            const_cast<QCNetworkRequestScheduler *>(this),
            Statistics{},
            [this]() { return statistics(); },
            "QCNetworkRequestScheduler::statistics");
    }
    assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::statistics");

    QMutexLocker locker(&m_impl->mutex);
    return m_impl->stats;
}

QList<QCNetworkReply *> QCNetworkRequestScheduler::pendingRequests() const
{
    if (QThread::currentThread() != thread()) {
        return invokeOnSchedulerOwnerThreadBlocking(
            const_cast<QCNetworkRequestScheduler *>(this),
            QList<QCNetworkReply *>{},
            [this]() { return pendingRequests(); },
            "QCNetworkRequestScheduler::pendingRequests");
    }
    assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::pendingRequests");

    QMutexLocker locker(&m_impl->mutex);

    QList<QCNetworkReply *> result;
    result.reserve(m_impl->pendingRequests.size());
    for (const auto &request : std::as_const(m_impl->pendingRequests)) {
        if (request.reply) {
            result.append(request.reply);
        }
    }
    return result;
}

QList<QCNetworkReply *> QCNetworkRequestScheduler::runningRequests() const
{
    if (QThread::currentThread() != thread()) {
        return invokeOnSchedulerOwnerThreadBlocking(
            const_cast<QCNetworkRequestScheduler *>(this),
            QList<QCNetworkReply *>{},
            [this]() { return runningRequests(); },
            "QCNetworkRequestScheduler::runningRequests");
    }
    assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::runningRequests");

    QMutexLocker locker(&m_impl->mutex);
    return m_impl->runningRequests;
}

void QCNetworkRequestScheduler::onRequestFinished(QCNetworkReply *reply)
{
    if (!reply) {
        return;
    }

    if (QThread::currentThread() != thread()) {
        QPointer<QCNetworkReply> safeReply(reply);
        invokeOnSchedulerOwnerThread(
            this,
            [this, safeReply]() {
                if (safeReply) {
                    onRequestFinished(safeReply.data());
                }
            },
            "QCNetworkRequestScheduler::onRequestFinished");
        return;
    }
    assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::onRequestFinished");
    Q_ASSERT_X(reply->thread() == thread(),
               "QCNetworkRequestScheduler::onRequestFinished",
               "reply must live on the scheduler owner thread");

    const ReplyKey key        = replyKey(reply);
    const ReplyOutcome outcome = captureReplyOutcome(reply);
    FinalizeResult result;
    {
        QMutexLocker locker(&m_impl->mutex);
        result = m_impl->finalizeReplyLocked(key, FinalizeTrigger::FinishedSignal, outcome);
    }

    if (!result.wasTracked) {
        return;
    }

    if (result.emitCancelled) {
        emit requestCancelled(reply, result.snapshot.lane, result.snapshot.hostKey);
    }

    if (result.emitFinished) {
        emit requestFinished(reply, result.snapshot.lane, result.snapshot.hostKey);
    }

    if (result.shouldKickQueue) {
        processQueue();
    }
}

void QCNetworkRequestScheduler::updateBandwidthStats()
{
    if (QThread::currentThread() != thread()) {
        invokeOnSchedulerOwnerThread(
            this,
            [this]() { updateBandwidthStats(); },
            "QCNetworkRequestScheduler::updateBandwidthStats");
        return;
    }
    assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::updateBandwidthStats");

    bool shouldProcess = false;
    {
        QMutexLocker locker(&m_impl->mutex);
        m_impl->bytesTransferredInWindow = 0;
        shouldProcess = !m_impl->pendingRequests.isEmpty();
    }

    if (shouldProcess) {
        processQueue();
    }
}

void QCNetworkRequestScheduler::onReplyDestroyed(QObject *obj)
{
    if (!obj) {
        return;
    }

    if (QThread::currentThread() != thread()) {
        QPointer<QObject> safeObject(obj);
        invokeOnSchedulerOwnerThread(
            this,
            [this, safeObject]() {
                if (safeObject) {
                    onReplyDestroyed(safeObject.data());
                }
            },
            "QCNetworkRequestScheduler::onReplyDestroyed");
        return;
    }
    assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::onReplyDestroyed");

    const ReplyKey key = obj;
    FinalizeResult result;
    {
        QMutexLocker locker(&m_impl->mutex);
        result = m_impl->finalizeReplyLocked(key, FinalizeTrigger::Destroyed);
    }

    if (result.shouldKickQueue) {
        QMetaObject::invokeMethod(this, [this]() { processQueue(); }, Qt::QueuedConnection);
    }
}

bool QCNetworkRequestScheduler::removeFromQueue(QCNetworkReply *reply)
{
    if (!reply) {
        return false;
    }

    if (QThread::currentThread() != thread()) {
        QPointer<QCNetworkReply> safeReply(reply);
        return invokeOnSchedulerOwnerThreadBlocking(
            this,
            false,
            [this, safeReply]() { return safeReply ? removeFromQueue(safeReply.data()) : false; },
            "QCNetworkRequestScheduler::removeFromQueue");
    }
    assertSchedulerOwnerThread(this, "QCNetworkRequestScheduler::removeFromQueue");

    QMutexLocker locker(&m_impl->mutex);
    return Impl::takeQueuedRequest(m_impl->pendingRequests, replyKey(reply));
}

} // namespace QCurl
