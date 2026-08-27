/**
 * @file
 * @brief 实现进程期有效的 Test Support provider 槽位。
 */

#include "private/QCNetworkMockProvider_p.h"

#include <atomic>

namespace QCurl {
namespace {

std::atomic<const Internal::QCNetworkMockProvider *> g_provider{nullptr};

} // namespace

bool installNetworkMockProvider(const Internal::QCNetworkMockProvider *provider) noexcept
{
    if (!provider) {
        return false;
    }

    const Internal::QCNetworkMockProvider *expected = nullptr;
    return g_provider.compare_exchange_strong(expected, provider, std::memory_order_release)
           || expected == provider;
}

const Internal::QCNetworkMockProvider *Internal::networkMockProvider() noexcept
{
    return g_provider.load(std::memory_order_acquire);
}

} // namespace QCurl
