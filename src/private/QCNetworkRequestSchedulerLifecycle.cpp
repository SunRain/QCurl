// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkRequestScheduler.h"

#include "private/QCNetworkRequestSchedulerPrivate_p.h"

#include <QTimer>

namespace QCurl {

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

} // namespace QCurl
