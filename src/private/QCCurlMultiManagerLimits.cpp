#include "QCCurlMultiManager.h"

#include "QCNetworkConnectionPoolConfig.h"

#include <QDebug>
#include <QMutexLocker>
#include <QThread>

namespace QCurl {

bool QCCurlMultiManager::applyLimitsConfig(const QCNetworkConnectionPoolConfig &config,
                                           QString *error)
{
    if (QThread::currentThread() != thread() || !m_multiHandle
        || m_isPoisoned.load(std::memory_order_relaxed)
        || m_isShuttingDown.load(std::memory_order_relaxed)) {
        *error = QStringLiteral("multi 配置必须在可用引擎的所属线程应用");
        return false;
    }
    return applyMultiLongOption(CURLMOPT_MAX_TOTAL_CONNECTIONS,
                                "CURLMOPT_MAX_TOTAL_CONNECTIONS",
                                config.multiMaxTotalConnections().value_or(0),
                                m_multiMaxTotalConnections,
                                error)
           && applyMultiLongOption(CURLMOPT_MAX_HOST_CONNECTIONS,
                                   "CURLMOPT_MAX_HOST_CONNECTIONS",
                                   config.multiMaxHostConnections().value_or(0),
                                   m_multiMaxHostConnections,
                                   error)
           && applyMultiLongOption(CURLMOPT_MAX_CONCURRENT_STREAMS,
                                   "CURLMOPT_MAX_CONCURRENT_STREAMS",
                                   config.multiMaxConcurrentStreams().value_or(100),
                                   m_multiMaxConcurrentStreams,
                                   error)
           && applyMultiLongOption(CURLMOPT_MAXCONNECTS,
                                   "CURLMOPT_MAXCONNECTS",
                                   config.multiMaxConnects().value_or(0),
                                   m_multiMaxConnects,
                                   error)
           && applyMultiLongOption(CURLMOPT_PIPELINING,
                                   "CURLMOPT_PIPELINING",
                                   config.multiplexingEnabled() ? CURLPIPE_MULTIPLEX
                                                                : CURLPIPE_NOTHING,
                                   m_multiMultiplexing,
                                   error);
}

bool QCCurlMultiManager::applyMultiLongOption(CURLMoption option,
                                              const char *optionName,
                                              long value,
                                              std::optional<long> &stateSlot,
                                              QString *error)
{
    if (stateSlot == value) {
        return true;
    }
    const CURLMcode rc = curl_multi_setopt(m_multiHandle, option, value);
    if (rc == CURLM_OK) {
        stateSlot = value;
        return true;
    }
    *error = QStringLiteral("multi 设置 %1 失败：%2")
                 .arg(QString::fromLatin1(optionName), QString::fromLatin1(curl_multi_strerror(rc)));
    return false;
}

} // namespace QCurl
