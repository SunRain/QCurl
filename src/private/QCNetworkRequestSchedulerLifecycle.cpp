// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkReply.h"
#include "QCNetworkRequestScheduler_p.h"

#include <QThread>

namespace QCurl {

QCNetworkRequestScheduler::QCNetworkRequestScheduler(QObject *parent)
    : QObject(parent)
{
    initialize();
    m_bandwidthWindow.start();
    m_throttleTimer.setSingleShot(true);
    connect(&m_throttleTimer,
            &QTimer::timeout,
            this,
            &QCNetworkRequestScheduler::updateBandwidthStats);
}

QCNetworkRequestScheduler::~QCNetworkRequestScheduler()
{
    Q_ASSERT(QThread::currentThread() == thread());
    m_throttleTimer.stop();
    const auto ids = m_bindings.keys();
    for (RequestId id : ids) {
        const ReplyBinding binding = unbindReply(id);
        // 析构不发业务通知；取消交给 reply 自己的事件循环，不重入其在途调用。
        if (binding.reply) {
            QMetaObject::invokeMethod(
                binding.reply.data(),
                [guard = binding.reply]() {
                    if (guard) {
                        guard->cancel();
                    }
                },
                Qt::QueuedConnection);
        }
    }
}

QCNetworkRequestScheduler::RequestId QCNetworkRequestScheduler::requestId(QCNetworkReply *reply) const
{
    for (auto it = m_bindings.cbegin(); it != m_bindings.cend(); ++it) {
        if (it->identity == reply) {
            return it.key();
        }
    }
    return 0;
}

void QCNetworkRequestScheduler::bindReply(RequestId id, QCNetworkReply *reply)
{
    ReplyBinding binding;
    binding.identity            = reply;
    binding.reply               = reply;
    binding.destroyedConnection = connect(reply, &QObject::destroyed, this, [this, id]() {
        onReplyDestroyed(id);
    });
    // 同步复制终态观测，再延后回收；用户 finished 槽立即销毁 reply 时也不会丢失记账。
    binding.finishedConnection = connect(reply, &QCNetworkReply::finished, this, [this, id]() {
        observeFinished(id);
    });
    m_bindings.insert(id, binding);
}

QCNetworkRequestScheduler::ReplyBinding QCNetworkRequestScheduler::unbindReply(RequestId id)
{
    const ReplyBinding binding = m_bindings.take(id);
    QObject::disconnect(binding.finishedConnection);
    QObject::disconnect(binding.destroyedConnection);
    QObject::disconnect(binding.downloadConnection);
    QObject::disconnect(binding.uploadConnection);
    return binding;
}

} // namespace QCurl
