// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkReply.h"
#include "QCNetworkRequestScheduler_p.h"

#include <QThread>

#include <limits>

namespace QCurl {

void QCNetworkRequestScheduler::connectProgressTracking(RequestId id)
{
    auto it = m_bindings.find(id);
    if (it == m_bindings.end() || !it->reply) {
        return;
    }
    QObject::disconnect(it->downloadConnection);
    QObject::disconnect(it->uploadConnection);
    it->downloadConnection = connect(
        it->reply.data(),
        &QCNetworkReply::downloadProgress,
        this,
        [this, id](qint64 bytes, qint64) { updateProgress(id, bytes, false); },
        Qt::AutoConnection);
    it->uploadConnection = connect(
        it->reply.data(),
        &QCNetworkReply::uploadProgress,
        this,
        [this, id](qint64 bytes, qint64) { updateProgress(id, bytes, true); },
        Qt::AutoConnection);
}

void QCNetworkRequestScheduler::updateProgress(RequestId id, qint64 bytes, bool upload)
{
    Q_ASSERT(QThread::currentThread() == thread());
    auto it = m_bindings.find(id);
    if (it == m_bindings.end()) {
        return;
    }
    qint64 &previous   = upload ? it->lastBytesSent : it->lastBytesReceived;
    bytes              = qMax<qint64>(bytes, 0);
    const qint64 delta = bytes > previous ? bytes - previous : 0;
    previous           = bytes;
    if (m_bandwidthWindow.elapsed() >= 1000) {
        m_throttleTimer.stop();
        m_bandwidthWindow.restart();
        m_bytesTransferredInWindow = 0;
        if (m_core.hasRunnablePending()) {
            queuePump();
        }
    }
    // 达到可表达上界后门控必已生效；饱和计数避免大进度值导致有符号溢出。
    const qint64 room = std::numeric_limits<qint64>::max() - m_bytesTransferredInWindow;
    m_bytesTransferredInWindow += qMin(delta, room);
}

bool QCNetworkRequestScheduler::admissionThrottled()
{
    if (m_bandwidthWindow.elapsed() >= 1000) {
        m_throttleTimer.stop();
        m_bandwidthWindow.restart();
        m_bytesTransferredInWindow = 0;
    }
    const qint64 budget = m_core.policy().admissionByteBudget();
    return budget > 0 && m_bytesTransferredInWindow >= budget;
}

void QCNetworkRequestScheduler::armAdmissionWakeup()
{
    if (!m_throttleTimer.isActive()) {
        const qint64 remaining = qMax<qint64>(1, 1000 - m_bandwidthWindow.elapsed());
        m_throttleTimer.start(int(remaining));
    }
}

void QCNetworkRequestScheduler::updateBandwidthStats()
{
    Q_ASSERT(QThread::currentThread() == thread());
    m_bytesTransferredInWindow = 0;
    m_bandwidthWindow.restart();
    processQueue();
}

} // namespace QCurl
