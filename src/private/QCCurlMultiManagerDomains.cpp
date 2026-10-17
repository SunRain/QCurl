#include "QCCurlMultiManager.h"
#include "QCNetworkRequest.h"
#include "private/QCCurlMultiDomain_p.h"
#include "private/QCCurlOptionAdapter_p.h"

#include <QMap>
#include <QThread>

#include <algorithm>
#include <utility>

namespace QCurl {
namespace {

constexpr qsizetype kMaxIdleDomains = 16;
constexpr auto kMaxIdleDomainAge    = std::chrono::seconds(30);

QStringList effectiveResolveKey(const QCNetworkRequest &request)
{
    QMap<QString, QString> effective;
    for (const auto &entry : request.resolveOverride().value_or(QStringList{})) {
        const auto colon = entry.lastIndexOf(QLatin1Char(':'));
        effective.insert(entry.left(colon), entry.mid(colon + 1));
    }
    QStringList key;
    for (auto it = effective.cbegin(); it != effective.cend(); ++it) {
        key.append(it.key() + QLatin1Char(':') + it.value());
    }
    return key;
}

} // namespace

QCCurlMultiManager::Domain QCCurlMultiManager::createDomain(QString *error)
{
    Q_ASSERT(QThread::currentThread() == thread());
    auto domain      = std::make_shared<QCCurlMultiDomain>();
    domain->owner    = this;
    domain->weakSelf = domain;
    domain->multi    = Internal::CurlOptions::createMultiHandle();
    if (!domain->multi) {
        *error = QStringLiteral("curl_multi_init 失败");
        return {};
    }
    domain->timer = new QTimer(this);
    domain->timer->setSingleShot(true);
    const std::weak_ptr<QCCurlMultiDomain> weakDomain(domain);
    connect(domain->timer, &QTimer::timeout, this, [this, weakDomain]() {
        if (const auto retained = weakDomain.lock()) {
            handleSocketAction(retained.get(), CURL_SOCKET_TIMEOUT, 0);
        }
    });
    if (!configureMultiCallbacks(domain.get(), "QCCurlMultiManager")) {
        *error = QStringLiteral("配置 curl multi 回调失败");
        retireDomain(domain.get());
        return {};
    }
    return domain;
}

QCCurlMultiManager::Domain QCCurlMultiManager::selectDomain(const QCNetworkRequest &request,
                                                            QString *error)
{
    reclaimIdleDomains();
    if (request.resolveIsolation() == QCNetworkResolveIsolation::NativeShared) {
        if (!m_nativeDomain) {
            m_nativeDomain = createDomain(error);
        }
        return m_nativeDomain;
    }
    if (!request.resolveOverride()) {
        return m_baseDomain;
    }
    const auto key   = effectiveResolveKey(request);
    const auto found = m_isolatedDomains.constFind(key);
    if (found != m_isolatedDomains.cend()) {
        return found.value();
    }
    auto domain = createDomain(error);
    if (domain) {
        m_isolatedDomains.insert(key, domain);
    }
    return domain;
}

QList<QCCurlMultiManager::Domain> QCCurlMultiManager::allDomains() const
{
    auto domains = m_isolatedDomains.values();
    if (m_baseDomain) {
        domains.append(m_baseDomain);
    }
    if (m_nativeDomain) {
        domains.append(m_nativeDomain);
    }
    return domains;
}

void QCCurlMultiManager::stopDomainEvents(QCCurlMultiDomain *domain)
{
    if (domain->timer) {
        domain->timer->stop();
    }
    disableSocketsForShutdown(domain->sockets.values());
}

void QCCurlMultiManager::retireDomain(QCCurlMultiDomain *domain)
{
    Q_ASSERT(QThread::currentThread() == thread());
    stopDomainEvents(domain);
    disableMultiCallbacks(domain);
    for (const auto &socket : std::as_const(domain->sockets)) {
        if (socket->readNotifier) {
            socket->readNotifier->deleteLater();
        }
        if (socket->writeNotifier) {
            socket->writeNotifier->deleteLater();
        }
    }
    domain->sockets.clear();
    if (domain->timer) {
        domain->timer->deleteLater();
        domain->timer = nullptr;
    }
    if (domain->multi) {
        const auto result = curl_multi_cleanup(domain->multi);
        if (result != CURLM_OK) {
            poisonLocked("QCCurlMultiManager::retireDomain", result);
            return;
        }
        domain->multi = nullptr;
    }
    domain->owner = nullptr;
}

void QCCurlMultiManager::reclaimIdleDomains()
{
    if (m_isPoisoned.load(std::memory_order_relaxed)
        || m_isShuttingDown.load(std::memory_order_relaxed)) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    QList<QStringList> idleKeys;
    for (auto it = m_isolatedDomains.begin(); it != m_isolatedDomains.end(); ++it) {
        auto &domain = it.value();
        if (domain.use_count() != 1) {
            domain->idleSince.reset();
        } else {
            if (!domain->idleSince) {
                domain->idleSince = now;
            }
            idleKeys.append(it.key());
        }
    }
    std::sort(idleKeys.begin(), idleKeys.end(), [this](const auto &left, const auto &right) {
        return m_isolatedDomains[left]->idleSince < m_isolatedDomains[right]->idleSince;
    });
    qsizetype remaining = idleKeys.size();
    for (const auto &key : idleKeys) {
        auto &domain = m_isolatedDomains[key];
        if (remaining <= kMaxIdleDomains && now - *domain->idleSince < kMaxIdleDomainAge) {
            break;
        }
        retireDomain(domain.get());
        if (m_isPoisoned.load(std::memory_order_relaxed)) {
            return;
        }
        m_isolatedDomains.remove(key);
        --remaining;
    }
}

} // namespace QCurl
