#include "QCCurlMultiManager.h"
#include "QCNetworkConnectionPoolConfig.h"
#include "private/QCCurlMultiDomain_p.h"
#include "private/QCCurlOptionAdapter_p.h"

#include <QDebug>
#include <QMutexLocker>
#include <QThread>

namespace QCurl {

bool QCCurlMultiManager::applyLimitsConfig(const QCNetworkConnectionPoolConfig &config,
                                           QString *error)
{
    return applyLimitsConfig(m_baseDomain.get(), config, error);
}

bool QCCurlMultiManager::applyLimitsConfig(QCCurlMultiDomain *domain,
                                           const QCNetworkConnectionPoolConfig &config,
                                           QString *error)
{
    if (QThread::currentThread() != thread() || !domain || !domain->multi
        || m_isPoisoned.load(std::memory_order_relaxed)
        || m_isShuttingDown.load(std::memory_order_relaxed)) {
        *error = QStringLiteral("multi 配置必须在可用引擎的所属线程应用");
        return false;
    }
    return applyMultiLongOption(domain,
                                CURLMOPT_MAX_TOTAL_CONNECTIONS,
                                "CURLMOPT_MAX_TOTAL_CONNECTIONS",
                                config.multiMaxTotalConnections().value_or(0),
                                domain->maxTotalConnections,
                                error)
           && applyMultiLongOption(domain,
                                   CURLMOPT_MAX_HOST_CONNECTIONS,
                                   "CURLMOPT_MAX_HOST_CONNECTIONS",
                                   config.multiMaxHostConnections().value_or(0),
                                   domain->maxHostConnections,
                                   error)
           && applyMultiLongOption(domain,
                                   CURLMOPT_MAX_CONCURRENT_STREAMS,
                                   "CURLMOPT_MAX_CONCURRENT_STREAMS",
                                   config.multiMaxConcurrentStreams().value_or(100),
                                   domain->maxConcurrentStreams,
                                   error)
           && applyMultiLongOption(domain,
                                   CURLMOPT_MAXCONNECTS,
                                   "CURLMOPT_MAXCONNECTS",
                                   config.multiMaxConnects().value_or(0),
                                   domain->maxConnects,
                                   error)
           && applyMultiLongOption(domain,
                                   CURLMOPT_PIPELINING,
                                   "CURLMOPT_PIPELINING",
                                   config.multiplexingEnabled() ? CURLPIPE_MULTIPLEX
                                                                : CURLPIPE_NOTHING,
                                   domain->multiplexing,
                                   error);
}

bool QCCurlMultiManager::applyMultiLongOption(QCCurlMultiDomain *domain,
                                              CURLMoption option,
                                              const char *optionName,
                                              long value,
                                              std::optional<long> &stateSlot,
                                              QString *error)
{
    if (stateSlot == value) {
        return true;
    }
    const CURLMcode rc = Internal::CurlOptions::shouldForceMultiFailure(optionName)
                             ? CURLM_UNKNOWN_OPTION
                             : curl_multi_setopt(domain->multi, option, value);
    if (rc == CURLM_OK) {
        stateSlot = value;
        return true;
    }
    *error = QStringLiteral("multi 设置 %1 失败：%2")
                 .arg(QString::fromLatin1(optionName), QString::fromLatin1(curl_multi_strerror(rc)));
    return false;
}

} // namespace QCurl
