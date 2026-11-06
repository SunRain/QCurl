/**
 * @file
 * @brief 实现 Blocking Extras 复用 Core 句柄与运行时能力的私有桥接。
 */

#include "CurlFeatureProbe.h"
#include "QCCurlHandleManager.h"
#include "private/QCBlockingHandleBridge_p.h"

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

} // namespace QCurl
