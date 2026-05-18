#include "QCCurlMultiManager.h"

#include "QCNetworkConnectionPoolConfig.h"

#include <QDebug>
#include <QMutexLocker>
#include <QThread>

namespace QCurl {

void QCCurlMultiManager::applyLimitsConfig(const QCNetworkConnectionPoolConfig &config)
{
    if (m_isShuttingDown.load(std::memory_order_relaxed)) {
        return;
    }

    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(
            this, [this, config]() { applyLimitsConfig(config); }, Qt::QueuedConnection);
        return;
    }

    if (!m_multiHandle) {
        return;
    }

    const std::optional<long> newMaxTotal    = config.multiMaxTotalConnections();
    const std::optional<long> newMaxHost     = config.multiMaxHostConnections();
    const std::optional<long> newMaxStreams  = config.multiMaxConcurrentStreams();
    const std::optional<long> newMaxConnects = config.multiMaxConnects();

    const bool clearRequested = (!newMaxTotal.has_value() && m_multiMaxTotalConnections.has_value())
                                || (!newMaxHost.has_value() && m_multiMaxHostConnections.has_value())
                                || (!newMaxStreams.has_value()
                                    && m_multiMaxConcurrentStreams.has_value())
                                || (!newMaxConnects.has_value() && m_multiMaxConnects.has_value());
    if (clearRequested && !canRecreateMultiHandleLocked()) {
        warnDeferredMultiLimitReset();
    } else if (clearRequested && recreateMultiHandleForLimits()) {
        clearMultiLimitState();
    }

    if (newMaxTotal.has_value()) {
        applyMultiLongOption(CURLMOPT_MAX_TOTAL_CONNECTIONS,
                             "CURLMOPT_MAX_TOTAL_CONNECTIONS",
                             newMaxTotal.value(),
                             m_multiMaxTotalConnections);
    }
    if (newMaxHost.has_value()) {
        applyMultiLongOption(CURLMOPT_MAX_HOST_CONNECTIONS,
                             "CURLMOPT_MAX_HOST_CONNECTIONS",
                             newMaxHost.value(),
                             m_multiMaxHostConnections);
    }
    if (newMaxStreams.has_value()) {
        applyMultiLongOption(CURLMOPT_MAX_CONCURRENT_STREAMS,
                             "CURLMOPT_MAX_CONCURRENT_STREAMS",
                             newMaxStreams.value(),
                             m_multiMaxConcurrentStreams);
    }
    if (newMaxConnects.has_value()) {
        applyMultiLongOption(
            CURLMOPT_MAXCONNECTS, "CURLMOPT_MAXCONNECTS", newMaxConnects.value(), m_multiMaxConnects);
    }
}

bool QCCurlMultiManager::canRecreateMultiHandleLocked()
{
    QMutexLocker locker(&m_mutex);
    return m_activeReplies.isEmpty() && m_socketMap.isEmpty()
           && (m_runningRequests.load(std::memory_order_relaxed) == 0);
}

void QCCurlMultiManager::applyMultiLongOption(CURLMoption option,
                                              const char *optionName,
                                              long value,
                                              std::optional<long> &stateSlot)
{
    const CURLMcode rc = curl_multi_setopt(m_multiHandle, option, value);
    if (rc == CURLM_OK) {
        stateSlot = value;
        return;
    }

    if (rc == CURLM_UNKNOWN_OPTION) {
        qWarning() << "QCCurlMultiManager capability warning: libcurl 不支持" << optionName
                   << "(" << curl_multi_strerror(rc) << ")";
        return;
    }

    qWarning() << "QCCurlMultiManager: Failed to set" << optionName << "("
               << curl_multi_strerror(rc) << ")";
}

void QCCurlMultiManager::clearMultiLimitState()
{
    m_multiMaxTotalConnections.reset();
    m_multiMaxHostConnections.reset();
    m_multiMaxConcurrentStreams.reset();
    m_multiMaxConnects.reset();
}

void QCCurlMultiManager::warnDeferredMultiLimitReset() const
{
    qWarning() << "QCCurlMultiManager::applyLimitsConfig: Some multi limits were cleared, but "
                  "active requests exist; cannot reset multi handle safely "
                  "(limits keep previous values until restart)";
}

} // namespace QCurl
