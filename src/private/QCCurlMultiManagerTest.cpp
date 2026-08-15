/**
 * @file
 * @brief 实现 curl multi manager 的通用测试观测入口。
 */

#include "QCCurlMultiManager.h"

#ifdef QCURL_ENABLE_TEST_HOOKS

#include <QMutexLocker>
#include <QThread>

namespace QCurl {

int QCCurlMultiManager::activeRepliesCountForTest()
{
    Q_ASSERT(QThread::currentThread() == thread());
    QMutexLocker locker(&m_mutex);
    return m_activeTransfers.size();
}

bool QCCurlMultiManager::isPoisonedForTest() const noexcept
{
    return m_isPoisoned.load(std::memory_order_relaxed);
}

int QCCurlMultiManager::completionCountForTest() const noexcept
{
    return m_testCompletionCount;
}

bool QCCurlMultiManager::completionHadHandleForTest() const noexcept
{
    return m_testCompletionHadHandle;
}

int QCCurlMultiManager::socketActionCountForTest() const noexcept
{
    return m_testSocketActionCount;
}

} // namespace QCurl

#endif // QCURL_ENABLE_TEST_HOOKS
