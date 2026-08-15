// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkReply.h"
#include "QCNetworkRequestScheduler.h"
#include "private/QCNetworkRequestSchedulerPrivate_p.h"

#include <QMutexLocker>
#include <QPointer>
#include <QThread>
#include <QTimer>

#include <utility>

namespace {

void registerQCNetworkRequestPriorityMetaTypeOnLoad()
{
    // 静态库不会自动抽取只有全局构造器的目标文件；把注册锚点放在调度器生命周期
    // 所在目标文件，确保 scheduler API consumer 在进入 main 前获得 canonical metatype。
    QCurl::initialize();
}

} // namespace

Q_CONSTRUCTOR_FUNCTION(registerQCNetworkRequestPriorityMetaTypeOnLoad)

namespace QCurl {

#ifdef QCURL_ENABLE_TEST_HOOKS
QCNetworkRequestScheduler *QCNetworkRequestScheduler::instanceForTesting()
{
    static thread_local QCNetworkRequestScheduler instance;
    return &instance;
}
#endif

QCNetworkRequestScheduler::QCNetworkRequestScheduler(QObject *parent)
    : QObject(parent)
    , m_impl(new Impl)
{
    initialize();
    m_impl->throttleTimer = new QTimer(this);
    m_impl->throttleTimer->setInterval(1000);
    connect(m_impl->throttleTimer,
            &QTimer::timeout,
            this,
            &QCNetworkRequestScheduler::updateBandwidthStats);
}

/// 析构期只清空内部调度状态并投递 reply 取消，不发射调度器业务信号。
QCNetworkRequestScheduler::~QCNetworkRequestScheduler()
{
    Internal::assertSchedulerOwnerThread(this,
                                         "QCNetworkRequestScheduler::~QCNetworkRequestScheduler");

    QList<QPointer<QCNetworkReply>> replies;
    {
        QMutexLocker locker(&m_impl->mutex);
        const QList<Internal::ReplyKey> keys = m_impl->replyStates.keys();
        replies.reserve(keys.size());
        for (Internal::ReplyKey key : keys) {
            const Internal::FinalizeResult result
                = m_impl->finalizeReplyLocked(key, Internal::FinalizeTrigger::ExplicitCancel);
            if (result.wasTracked) {
                replies.append(Internal::replyFromKey(key));
            }
        }

        m_impl->bytesTransferredInWindow = 0;
        m_impl->queues.resetRuntimeState();
    }

    for (const auto &reply : std::as_const(replies)) {
        if (reply) {
            Internal::invokeReplyCancel(reply.data());
        }
    }
}

} // namespace QCurl
