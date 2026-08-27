/**
 * @file
 * @brief 实现 Blocking Extras 复用 Core 句柄与协议策略的最小私有桥接。
 */

#include "private/QCBlockingHandleBridge_p.h"

#include "CurlFeatureProbe.h"
#include "QCCurlHandleManager.h"
#include "private/QCNetworkProtocolPolicy_p.h"

namespace QCurl {

QCBlockingHandleBridge::QCBlockingHandleBridge()
    : m_manager(new QCCurlHandleManager)
{}

QCBlockingHandleBridge::~QCBlockingHandleBridge() = default;

CURL *QCBlockingHandleBridge::handle() const noexcept
{
    return m_manager->handle();
}

QString QCBlockingHandleBridge::initializationError() const
{
    return m_manager->initializationError();
}

QCBlockingRuntimeAvailability blockingRuntimeAvailability()
{
    const auto availability = CurlFeatureProbe::instance().minimumRuntimeAvailability();
    return {availability.supported, availability.reason};
}

bool validateBlockingUrl(const QUrl &url, QString *error)
{
    return Internal::QCNetworkProtocolPolicy::validateCoreUrl(url, error);
}

bool resolveBlockingInitialProtocols(const std::optional<QStringList> &requested,
                                     QStringList *effective,
                                     QString *error)
{
    return Internal::QCNetworkProtocolPolicy::resolveInitialProtocols(requested, effective, error);
}

bool resolveBlockingRedirectProtocols(const std::optional<QStringList> &requested,
                                      QStringList *effective,
                                      QString *error)
{
    return Internal::QCNetworkProtocolPolicy::resolveRedirectProtocols(requested, effective, error);
}

} // namespace QCurl
