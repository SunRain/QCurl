// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkRequestScheduler_p.h"

#include <QThread>

namespace QCurl {

bool QCNetworkRequestScheduler::applyPolicy(const QCNetworkSchedulerPolicy &policy, QString *error)
{
    if (QThread::currentThread() != thread()) {
        if (error) {
            *error = QStringLiteral("scheduler policy requires owner thread");
        }
        return false;
    }
    const qint64 previousBudget = m_core.policy().admissionByteBudget();
    if (!m_core.applyPolicy(policy, error)) {
        return false;
    }
    if (previousBudget != m_core.policy().admissionByteBudget()) {
        m_bytesTransferredInWindow = 0;
        m_bandwidthWindow.restart();
    }
    processQueue();
    return true;
}

QCNetworkSchedulerPolicy QCNetworkRequestScheduler::policy() const
{
    Q_ASSERT(QThread::currentThread() == thread());
    return m_core.policy();
}

QCNetworkSchedulerStatistics QCNetworkRequestScheduler::statistics() const
{
    Q_ASSERT(QThread::currentThread() == thread());
    return m_core.statistics();
}

} // namespace QCurl
