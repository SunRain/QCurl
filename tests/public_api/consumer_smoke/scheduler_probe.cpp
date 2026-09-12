// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "contract_probes.h"

#include <QCNetworkAccessManager.h>
#include <QCNetworkReply.h>
#include <QCNetworkRequest.h>
#include <QCNetworkSchedulerPolicy.h>
#include <QEventLoop>
#include <QPointer>
#include <QTimer>

using namespace QCurl;

namespace {

bool awaitFinished(QCNetworkReply *reply)
{
    if (reply->isFinished()) {
        return true;
    }
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QCNetworkReply::finished, &loop, &QEventLoop::quit);
    deadline.start(2000);
    loop.exec();
    return reply->isFinished();
}

struct Observations
{
    int queued         = 0;
    int changed        = 0;
    int started        = 0;
    int cancelled      = 0;
    int empty          = 0;
    bool commandFailed = false;
    QCNetworkLaneKey lane;
    QString origin;
    QCNetworkRequestPriority priority = QCNetworkRequestPriority::Normal;
};

void observe(QCNetworkAccessManager &manager, Observations &events)
{
    QObject::connect(&manager,
                     &QCNetworkAccessManager::schedulerRequestQueued,
                     &manager,
                     [&](QCNetworkReply *,
                         const QCNetworkLaneKey &lane,
                         const QString &origin,
                         QCNetworkRequestPriority priority) {
                         ++events.queued;
                         events.lane     = lane;
                         events.origin   = origin;
                         events.priority = priority;
                     });
    QObject::connect(&manager,
                     &QCNetworkAccessManager::schedulerRequestPriorityChanged,
                     &manager,
                     [&](QCNetworkReply *) { ++events.changed; });
    QObject::connect(&manager,
                     &QCNetworkAccessManager::schedulerRequestCancelled,
                     &manager,
                     [&](QCNetworkReply *) { ++events.cancelled; });
    QObject::connect(&manager, &QCNetworkAccessManager::schedulerPendingQueueEmpty, &manager, [&]() {
        ++events.empty;
    });
    QObject::connect(&manager,
                     &QCNetworkAccessManager::schedulerRequestAboutToStart,
                     &manager,
                     [&](QCNetworkReply *reply) {
                         if (reply->url().path() == QStringLiteral("/intercept")) {
                             events.commandFailed = manager.cancelScheduledRequest(reply)
                                                    != SchedulerCommandResult::Applied;
                         }
                     });
    QObject::connect(&manager,
                     &QCNetworkAccessManager::schedulerRequestStarted,
                     &manager,
                     [&](QCNetworkReply *reply) {
                         ++events.started;
                         events.commandFailed = manager.cancelScheduledRequest(reply)
                                                != SchedulerCommandResult::Applied;
                     });
}

bool pendingCommands(QCNetworkAccessManager &manager, Observations &events)
{
    auto *blocker = manager.get(
        QCNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:9/blocker"))));
    QCNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:9/pending")));
    request.setLane(QCNetworkLaneKey::control());
    request.setPriority(QCNetworkRequestPriority::Low);
    QPointer<QCNetworkReply> reply = manager.get(request);
    if (events.queued != 2 || events.priority != QCNetworkRequestPriority::Low
        || manager.schedulerStatistics().pendingRequests() != 1) {
        return false;
    }
    if (manager.setScheduledRequestPriority(reply.data(), QCNetworkRequestPriority::High)
            != SchedulerCommandResult::Applied
        || manager.setScheduledRequestPriority(reply.data(), QCNetworkRequestPriority::High)
               != SchedulerCommandResult::NoChange
        || events.changed != 1 || events.queued != 2) {
        return false;
    }
    const int emptyBefore = events.empty;
    if (manager.deferScheduledRequest(reply.data()) != SchedulerCommandResult::Applied
        || manager.deferScheduledRequest(reply.data()) != SchedulerCommandResult::InvalidState
        || events.empty != emptyBefore + 1
        || manager.undeferScheduledRequest(reply.data()) != SchedulerCommandResult::Applied
        || events.queued != 3 || events.priority != QCNetworkRequestPriority::High) {
        return false;
    }
    const auto cancelled
        = manager.cancelLaneRequests(QCNetworkLaneKey::control(),
                                     QCNetworkAccessManager::SchedulerCancelScope::PendingOnly);
    const auto empty
        = manager.cancelLaneRequests(QCNetworkLaneKey::control(),
                                     QCNetworkAccessManager::SchedulerCancelScope::PendingOnly);
    if (!cancelled.isSuccess() || cancelled.cancelledRequests() != 1 || !empty.isSuccess()
        || empty.cancelledRequests() != 0
        || manager.cancelScheduledRequest(reply.data()) != SchedulerCommandResult::NotTracked) {
        return false;
    }
    delete reply.data();
    return reply.isNull() && events.lane == QCNetworkLaneKey::control()
           && events.origin == QStringLiteral("http://127.0.0.1:9")
           && manager.cancelScheduledRequest(blocker) == SchedulerCommandResult::Applied;
}

} // namespace

int runSchedulerProbe()
{
    Observations events;
    QCNetworkAccessManager manager;
    manager.enableRequestScheduler(true);
    auto policy = manager.schedulerPolicy();
    policy.setMaxConcurrentRequests(1);
    if (!manager.setSchedulerPolicy(policy)) {
        return 80;
    }
    observe(manager, events);
    if (!pendingCommands(manager, events)) {
        return 81;
    }
    auto *intercept = manager.get(
        QCNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:9/intercept"))));
    if (!awaitFinished(intercept) || events.commandFailed || events.started != 0) {
        return 82;
    }
    auto *started = manager.get(
        QCNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:9/started"))));
    if (!awaitFinished(started) || events.commandFailed || events.started != 1
        || manager.schedulerStatistics().runningRequests() != 0) {
        return 83;
    }
    return 0;
}
