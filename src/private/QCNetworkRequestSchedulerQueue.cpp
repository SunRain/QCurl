/**
 * @file
 * @brief QCNetworkRequestScheduler queue / lane policy implementation.
 */

#include "private/QCNetworkRequestSchedulerQueue_p.h"

#include "QCNetworkReply.h"

#include <QPointer>

namespace QCurl {

namespace Internal {

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
    if (scheme == QStringLiteral("https") || scheme == QStringLiteral("wss")) {
        return 443;
    }
    if (scheme == QStringLiteral("http") || scheme == QStringLiteral("ws")) {
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
    return QStringLiteral("%1://%2:%3").arg(scheme, host).arg(effectivePort(url));
}

QCNetworkRequestScheduler::LaneConfig sanitizedLaneConfig(
    QCNetworkRequestScheduler::LaneConfig config)
{
    if (config.weight() <= 0) {
        config.setWeight(1);
    }
    if (config.quantum() <= 0) {
        config.setQuantum(1);
    }
    if (config.reservedGlobal() < 0) {
        config.setReservedGlobal(0);
    }
    if (config.reservedPerHost() < 0) {
        config.setReservedPerHost(0);
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

bool SchedulerQueues::takeQueuedRequest(QList<QueuedRequest> &requests,
                                        ReplyKey key,
                                        QueuedRequest *out)
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

void SchedulerQueues::ensureLane(const QString &lane)
{
    if (!laneOrder.contains(lane)) {
        laneOrder.append(lane);
    }
    if (!laneDeficit.contains(lane)) {
        laneDeficit.insert(lane, 0);
    }
}

void SchedulerQueues::clearLaneConfigs()
{
    laneConfigs.clear();
    resetRuntimeState();
}

void SchedulerQueues::setLaneConfig(const QString &lane,
                                    const QCNetworkRequestScheduler::LaneConfig &config)
{
    laneConfigs.insert(lane, config);
    ensureLane(lane);
}

QCNetworkRequestScheduler::LaneConfig SchedulerQueues::laneConfigFor(const QString &lane) const
{
    return laneConfigs.value(lane, QCNetworkRequestScheduler::LaneConfig{});
}

int SchedulerQueues::selectNextIndex(const QCNetworkRequestScheduler::Config &config)
{
    int nextIndex = selectReservationHostIndex(config);
    if (nextIndex < 0) {
        nextIndex = selectReservationGlobalIndex(config);
    }
    if (nextIndex < 0) {
        nextIndex = selectBestEffortIndex(config);
    }
    return nextIndex;
}

void SchedulerQueues::markRunning(const QueuedRequest &request)
{
    runningRequests.append(request.reply);
    runningLaneCount[request.snapshot.lane]++;
    hostConnectionCount[request.snapshot.hostKey]++;
    incrementNestedCounter(runningLaneHostCount, request.snapshot.lane, request.snapshot.hostKey);
    laneLastStartedHost.insert(request.snapshot.lane, request.snapshot.hostKey);
}

bool SchedulerQueues::removeRunning(QCNetworkReply *reply, const ReplySnapshot &snapshot)
{
    if (!runningRequests.removeOne(reply)) {
        return false;
    }

    runningLaneCount[snapshot.lane]--;
    if (runningLaneCount.value(snapshot.lane, 0) <= 0) {
        runningLaneCount.remove(snapshot.lane);
    }

    hostConnectionCount[snapshot.hostKey]--;
    if (hostConnectionCount.value(snapshot.hostKey, 0) <= 0) {
        hostConnectionCount.remove(snapshot.hostKey);
    }

    decrementNestedCounter(runningLaneHostCount, snapshot.lane, snapshot.hostKey);
    return true;
}

void SchedulerQueues::resetRuntimeState()
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

} // namespace Internal

} // namespace QCurl
