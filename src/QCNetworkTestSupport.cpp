/**
 * @file
 * @brief 实现显式 Test Support 的 manager 绑定入口。
 */

#include "QCNetworkTestSupport.h"

#include "QCNetworkAccessManager.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkMockHandler_p.h"
#include "private/QCNetworkMockProvider_p.h"

#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>

namespace QCurl::TestSupport {
namespace {

struct MockBindingRegistry
{
    QMutex mutex;
    QHash<const QCNetworkAccessManager *, QCNetworkMockHandler *> handlers;
    QSet<const QCNetworkAccessManager *> connectedManagers;
};

MockBindingRegistry &bindingRegistry()
{
    static MockBindingRegistry registry;
    return registry;
}

void *handlerForManager(const QCNetworkAccessManager *manager)
{
    return mockHandler(manager);
}

bool captureEnabled(void *handler)
{
    return static_cast<QCNetworkMockHandler *>(handler)->captureEnabled();
}

int captureBodyPreviewLimit(void *handler)
{
    return static_cast<QCNetworkMockHandler *>(handler)->captureBodyPreviewLimit();
}

void recordRequest(void *handler, const Internal::QCNetworkCapturedRequestSnapshot &snapshot)
{
    QCNetworkCapturedRequest captured;
    captured.setUrl(snapshot.url);
    captured.setMethod(snapshot.method);
    captured.setCustomMethod(snapshot.customMethod);
    captured.setHeaders(snapshot.headers);
    captured.setBodyPreview(snapshot.bodyPreview);
    captured.setBodySize(snapshot.bodySize);
    captured.setFollowLocation(snapshot.followLocation);
    if (snapshot.connectTimeoutMs.has_value()) {
        captured.setConnectTimeoutMs(snapshot.connectTimeoutMs.value());
    }
    if (snapshot.totalTimeoutMs.has_value()) {
        captured.setTotalTimeoutMs(snapshot.totalTimeoutMs.value());
    }
    static_cast<QCNetworkMockHandler *>(handler)->recordRequest(captured);
}

bool hasMock(void *handler, HttpMethod method, const QUrl &url)
{
    return static_cast<QCNetworkMockHandler *>(handler)->hasMock(method, url);
}

bool consumeMock(void *handler,
                 HttpMethod method,
                 const QUrl &url,
                 Internal::QCNetworkMockData &out)
{
    return Internal::QCNetworkMockHandlerAccess::consumeMock(
        *static_cast<QCNetworkMockHandler *>(handler), method, url, out);
}

int globalDelay(void *handler)
{
    return static_cast<QCNetworkMockHandler *>(handler)->globalDelay();
}

const Internal::QCNetworkMockProvider kMockProvider{
    handlerForManager,
    captureEnabled,
    captureBodyPreviewLimit,
    recordRequest,
    hasMock,
    consumeMock,
    globalDelay,
};

void ensureProviderInstalled()
{
    const bool installed = installNetworkMockProvider(&kMockProvider);
    Q_ASSERT(installed);
    Q_UNUSED(installed)
}

} // namespace

void setMockHandler(QCNetworkAccessManager *manager, QCNetworkMockHandler *handler)
{
    if (!manager) {
        return;
    }

    ensureProviderInstalled();
    auto &registry = bindingRegistry();
    bool needsDestroyedConnection = false;
    {
        QMutexLocker locker(&registry.mutex);
        needsDestroyedConnection = !registry.connectedManagers.contains(manager);
        registry.connectedManagers.insert(manager);
        if (handler) {
            registry.handlers.insert(manager, handler);
        } else {
            registry.handlers.remove(manager);
        }
    }

    if (needsDestroyedConnection) {
        QObject::connect(manager, &QObject::destroyed, [](QObject *object) {
            auto &registry = bindingRegistry();
            QMutexLocker locker(&registry.mutex);
            const auto *manager = static_cast<QCNetworkAccessManager *>(object);
            registry.handlers.remove(manager);
            registry.connectedManagers.remove(manager);
        });
    }
}

QCNetworkMockHandler *mockHandler(const QCNetworkAccessManager *manager)
{
    if (!manager) {
        return nullptr;
    }

    auto &registry = bindingRegistry();
    QMutexLocker locker(&registry.mutex);
    return registry.handlers.value(manager, nullptr);
}

} // namespace QCurl::TestSupport
