// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkRequestScheduler.h"

#include "private/QCNetworkRequestSchedulerPrivate_p.h"

#include <QThread>
#include <QTimer>

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

QCNetworkRequestScheduler::~QCNetworkRequestScheduler()
{
    cancelAllRequests();
}

} // namespace QCurl
