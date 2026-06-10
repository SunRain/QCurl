#include "QCNetworkRequest.h"

#include "private/QCNetworkRequestPrivate_p.h"

#include <QDebug>

#include <chrono>
#include <optional>

namespace QCurl {

QCNetworkRequest::QCNetworkRequest()
    : d(new QCNetworkRequestPrivate())
{
}

QCNetworkRequest::QCNetworkRequest(const QUrl &url)
    : d(new QCNetworkRequestPrivate())
{
    d.data()->reqUrl = url;
}

QCNetworkRequest::QCNetworkRequest(const QCNetworkRequest &other)
    : d(other.d)
{
}

QCNetworkRequest::~QCNetworkRequest() = default;

QCNetworkRequest &QCNetworkRequest::operator=(const QCNetworkRequest &other)
{
    if (this != &other) {
        d = other.d;
    }
    return *this;
}

bool QCNetworkRequest::operator==(const QCNetworkRequest &other) const
{
    const QCNetworkRequestPrivate *lhs = d.constData();
    const QCNetworkRequestPrivate *rhs = other.d.constData();

    return lhs->redirectConfig.followLocation() == rhs->redirectConfig.followLocation()
           && lhs->reqUrl == rhs->reqUrl && lhs->rawHeaderMap == rhs->rawHeaderMap
           && lhs->rangeStart == rhs->rangeStart && lhs->rangeEnd == rhs->rangeEnd
           && lhs->httpVersion == rhs->httpVersion && lhs->lane == rhs->lane;
}

bool QCNetworkRequest::operator!=(const QCNetworkRequest &other) const
{
    return !(*this == other);
}

QUrl QCNetworkRequest::url() const
{
    return d.constData()->reqUrl;
}

QCNetworkRequest &QCNetworkRequest::setUrl(const QUrl &url)
{
    d.data()->reqUrl = url;
    return *this;
}

QCNetworkRequest &QCNetworkRequest::setFollowLocation(bool followLocation)
{
    d.data()->redirectConfig.setFollowLocation(followLocation);
    return *this;
}

bool QCNetworkRequest::followLocation() const
{
    return d.constData()->redirectConfig.followLocation();
}

QCNetworkRequest &QCNetworkRequest::setMaxRedirects(int maxRedirects)
{
    d.data()->redirectConfig.setMaxRedirects(maxRedirects);
    return *this;
}

std::optional<int> QCNetworkRequest::maxRedirects() const
{
    return d.constData()->redirectConfig.maxRedirects();
}

QCNetworkRequest &QCNetworkRequest::setPostRedirectPolicy(QCNetworkPostRedirectPolicy policy)
{
    d.data()->redirectConfig.setPostRedirectPolicy(policy);
    return *this;
}

QCNetworkPostRedirectPolicy QCNetworkRequest::postRedirectPolicy() const
{
    return d.constData()->redirectConfig.postRedirectPolicy();
}

QCNetworkRequest &QCNetworkRequest::setAutoRefererEnabled(bool enabled)
{
    d.data()->redirectConfig.setAutoRefererEnabled(enabled);
    return *this;
}

bool QCNetworkRequest::autoRefererEnabled() const
{
    return d.constData()->redirectConfig.autoRefererEnabled();
}

QCNetworkRequest &QCNetworkRequest::setReferer(const QString &referer)
{
    d.data()->redirectConfig.setReferer(referer);
    return *this;
}

QString QCNetworkRequest::referer() const
{
    return d.constData()->redirectConfig.referer();
}

QCNetworkRequest &QCNetworkRequest::setAllowUnrestrictedSensitiveHeadersOnRedirect(bool enabled)
{
    d.data()->redirectConfig.setAllowUnrestrictedSensitiveHeadersOnRedirect(enabled);
    return *this;
}

bool QCNetworkRequest::allowUnrestrictedSensitiveHeadersOnRedirect() const
{
    return d.constData()->redirectConfig.allowUnrestrictedSensitiveHeadersOnRedirect();
}

QCNetworkRequest &QCNetworkRequest::setRawHeader(const QByteArray &headerName,
                                                 const QByteArray &headerValue)
{
    d.data()->rawHeaderMap.insert(headerName, headerValue);
    return *this;
}

QList<QByteArray> QCNetworkRequest::rawHeaderList() const
{
    return d.constData()->rawHeaderMap.keys();
}

QByteArray QCNetworkRequest::rawHeader(const QByteArray &headerName) const
{
    return d.constData()->rawHeaderMap.value(headerName);
}

QCNetworkRequest &QCNetworkRequest::setRange(int start, int end)
{
    d.data()->rangeStart = start;
    d.data()->rangeEnd = end;
    return *this;
}

int QCNetworkRequest::rangeStart() const
{
    return d.constData()->rangeStart;
}

int QCNetworkRequest::rangeEnd() const
{
    return d.constData()->rangeEnd;
}

QCNetworkRequest &QCNetworkRequest::setSslConfig(const QCNetworkSslConfig &config)
{
    d.data()->sslConfig = config;
    return *this;
}

QCNetworkSslConfig QCNetworkRequest::sslConfig() const
{
    return d.constData()->sslConfig;
}

QCNetworkRequest &QCNetworkRequest::setProxyConfig(const QCNetworkProxyConfig &config)
{
    d.data()->proxyConfig = config;
    return *this;
}

std::optional<QCNetworkProxyConfig> QCNetworkRequest::proxyConfig() const
{
    return d.constData()->proxyConfig;
}

QCNetworkRequest &QCNetworkRequest::setTimeoutConfig(const QCNetworkTimeoutConfig &config)
{
    d.data()->timeoutConfig = config;
    return *this;
}

QCNetworkTimeoutConfig QCNetworkRequest::timeoutConfig() const
{
    return d.constData()->timeoutConfig;
}

QCNetworkRequest &QCNetworkRequest::setHttpVersion(QCNetworkHttpVersion version)
{
    d.data()->httpVersion = version;
    d.data()->httpVersionExplicit = true;
    return *this;
}

QCNetworkHttpVersion QCNetworkRequest::httpVersion() const
{
    return d.constData()->httpVersion;
}

bool QCNetworkRequest::isHttpVersionExplicit() const noexcept
{
    return d.constData()->httpVersionExplicit;
}

QCNetworkRequest &QCNetworkRequest::setRetryPolicy(const QCNetworkRetryPolicy &policy)
{
    d.data()->retryPolicy = policy;
    d.data()->retryPolicyExplicit = true;
    return *this;
}

QCNetworkRetryPolicy QCNetworkRequest::retryPolicy() const
{
    return d.constData()->retryPolicy;
}

bool QCNetworkRequest::isRetryPolicyExplicit() const noexcept
{
    return d.constData()->retryPolicyExplicit;
}

QCNetworkRequest &QCNetworkRequest::setHttpAuth(const QCNetworkHttpAuthConfig &config)
{
    d.data()->httpAuthConfig = config;
    return *this;
}

std::optional<QCNetworkHttpAuthConfig> QCNetworkRequest::httpAuth() const
{
    return d.constData()->httpAuthConfig;
}

QCNetworkRequest &QCNetworkRequest::clearHttpAuth()
{
    d.data()->httpAuthConfig.reset();
    return *this;
}

QCNetworkRequest &QCNetworkRequest::setRedirectConfig(const QCNetworkRedirectConfig &config)
{
    d.data()->redirectConfig = config;
    return *this;
}

QCNetworkRedirectConfig QCNetworkRequest::redirectConfig() const
{
    return d.constData()->redirectConfig;
}

QCNetworkRequest &QCNetworkRequest::setTransferConfig(const QCNetworkTransferConfig &config)
{
    d.data()->transferConfig = config;
    return *this;
}

QCNetworkTransferConfig QCNetworkRequest::transferConfig() const
{
    return d.constData()->transferConfig;
}

QCNetworkRequest &QCNetworkRequest::setTimeout(std::chrono::milliseconds timeout)
{
    d.data()->timeoutConfig.setTotalTimeout(timeout);
    return *this;
}

QCNetworkRequest &QCNetworkRequest::setConnectTimeout(std::chrono::milliseconds timeout)
{
    d.data()->timeoutConfig.setConnectTimeout(timeout);
    return *this;
}

QCNetworkRequest &QCNetworkRequest::setLane(const QCNetworkLaneKey &lane)
{
    d.data()->lane = lane;
    return *this;
}

QCNetworkLaneKey QCNetworkRequest::lane() const
{
    return d.constData()->lane;
}

QCNetworkRequest &QCNetworkRequest::setPriority(QCNetworkRequestPriority priority)
{
    d.data()->requestPriority = priority;
    return *this;
}

QCNetworkRequestPriority QCNetworkRequest::priority() const
{
    return d.constData()->requestPriority;
}

QCNetworkRequest &QCNetworkRequest::setCachePolicy(QCNetworkCachePolicy policy)
{
    d.data()->cachePolicy = policy;
    return *this;
}

QCNetworkCachePolicy QCNetworkRequest::cachePolicy() const
{
    return d.constData()->cachePolicy;
}

QDebug operator<<(QDebug dbg, const QCNetworkRequest &req)
{
    dbg.nospace() << "QCNetworkRequest("
                  << "url=" << req.url().toString() << ", followLocation=" << req.followLocation()
                  << ", range=" << req.rangeStart() << "-" << req.rangeEnd()
                  << ", lane=" << req.lane().name()
                  << ", httpVersion=" << static_cast<int>(req.httpVersion()) << ")";
    return dbg.space();
}

} // namespace QCurl
