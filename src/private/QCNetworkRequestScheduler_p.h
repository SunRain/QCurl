// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#ifndef QCNETWORKREQUESTSCHEDULER_P_H
#define QCNETWORKREQUESTSCHEDULER_P_H

#include "QCNetworkAdmissionCore_p.h"

#include <QElapsedTimer>
#include <QHash>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QTimer>

Q_MOC_INCLUDE("QCNetworkReply.h")

namespace QCurl {

class QCNetworkReply;
class QCNetworkRequestSchedulerTestAccess;

// manager 子对象；只管理 Qt 生命周期，调度事实由 m_core 唯一持有。
class Q_DECL_HIDDEN QCNetworkRequestScheduler : public QObject
{
    Q_OBJECT

public:
    explicit QCNetworkRequestScheduler(QObject *parent = nullptr);
    ~QCNetworkRequestScheduler() override;

    [[nodiscard]] bool applyPolicy(const QCNetworkSchedulerPolicy &policy, QString *error = nullptr);
    QCNetworkSchedulerPolicy policy() const;
    QCNetworkSchedulerStatistics statistics() const;
    [[nodiscard]] SchedulerCommandResult scheduleReply(QCNetworkReply *reply,
                                                       const QCNetworkLaneKey &lane,
                                                       QCNetworkRequestPriority priority);
    [[nodiscard]] SchedulerCommandResult deferPendingRequest(QCNetworkReply *reply);
    [[nodiscard]] SchedulerCommandResult undeferRequest(QCNetworkReply *reply);
    [[nodiscard]] SchedulerCommandResult cancelRequest(QCNetworkReply *reply);
    [[nodiscard]] SchedulerCommandResult changePriority(QCNetworkReply *reply,
                                                        QCNetworkRequestPriority priority);
    int cancelLaneRequests(const QCNetworkLaneKey &lane, bool includeRunning);

Q_SIGNALS:
    void requestQueued(QCNetworkReply *reply,
                       const QCNetworkLaneKey &lane,
                       const QString &origin,
                       QCNetworkRequestPriority priority);
    void requestPriorityChanged(QCNetworkReply *reply,
                                const QCNetworkLaneKey &lane,
                                const QString &origin,
                                QCNetworkRequestPriority priority);
    void requestAboutToStart(QCNetworkReply *reply,
                             const QCNetworkLaneKey &lane,
                             const QString &origin,
                             QCNetworkRequestPriority priority);
    void requestStarted(QCNetworkReply *reply,
                        const QCNetworkLaneKey &lane,
                        const QString &origin,
                        QCNetworkRequestPriority priority);
    void requestCancelled(QCNetworkReply *reply,
                          const QCNetworkLaneKey &lane,
                          const QString &origin,
                          QCNetworkRequestPriority priority);
    void queueEmpty();

private:
    Q_DISABLE_COPY_MOVE(QCNetworkRequestScheduler)
    friend class QCNetworkRequestSchedulerTestAccess;

    using RequestId = Internal::SchedulerRequestId;
    /// RequestId 的 Qt 生存绑定；只保存观察和票据，不另存可写调度状态。
    struct ReplyBinding
    {
        // identity 只用于地址相等比较；实际对象访问必须通过 owner-thread 的 guarded borrow。
        QCNetworkReply *identity = nullptr;
        QPointer<QCNetworkReply> reply;
        quint64 startTicket = 0;
        QElapsedTimer elapsed;
        std::optional<Internal::ReplyOutcome> outcome;
        qint64 lastBytesReceived = 0;
        qint64 lastBytesSent     = 0;
        QMetaObject::Connection finishedConnection;
        QMetaObject::Connection destroyedConnection;
        QMetaObject::Connection downloadConnection;
        QMetaObject::Connection uploadConnection;
    };

    RequestId requestId(QCNetworkReply *reply) const;
    SchedulerCommandResult validateCommand(QCNetworkReply *reply, RequestId *id) const;
    void bindReply(RequestId id, QCNetworkReply *reply);
    ReplyBinding unbindReply(RequestId id);
    void processQueue();
    void queuePump();
    void startRequest(const Internal::AdmissionStart &start);
    void dispatchReplyExecution(RequestId id,
                                quint64 ticket,
                                const Internal::ReplySnapshot &snapshot);
    bool isStartTicketValid(RequestId id, quint64 ticket) const;
    bool notifyPendingEmpty(bool emptied);
    void onRequestFinished(RequestId id);
    std::optional<Internal::ReplyOutcome> captureOutcome(RequestId id) const;
    void observeFinished(RequestId id);
    void onReplyDestroyed(RequestId id);
    void connectProgressTracking(RequestId id);
    void updateProgress(RequestId id, qint64 bytes, bool upload);
    bool admissionThrottled();
    void armAdmissionWakeup();
    void updateBandwidthStats();
    int cancelRequests(const QList<RequestId> &ids);

    Internal::AdmissionCore m_core;
    QHash<RequestId, ReplyBinding> m_bindings;
    quint64 m_nextRequestId   = 1;
    quint64 m_nextStartTicket = 1;
    QTimer m_throttleTimer{this};
    QElapsedTimer m_bandwidthWindow;
    qint64 m_bytesTransferredInWindow = 0;
    bool m_pumpQueued                 = false;
};

} // namespace QCurl

#endif // QCNETWORKREQUESTSCHEDULER_P_H
