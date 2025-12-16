#include "QCNetworkRequest.h"
#include "QCNetworkSslConfig.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkTimeoutConfig.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkRetryPolicy.h"
#include "QCNetworkRequestPriority.h"
#include "QCNetworkCachePolicy.h"

#include <QDebug>
#include <QSharedData>
#include <QUrl>
#include <QMap>

namespace QCurl {

typedef QPair<QByteArray, QByteArray> RawHeaderPair;

/**
 * @brief QCNetworkRequest 的私有数据（Pimpl 模式）
 *
 * 使用 QSharedData 实现写时复制（COW），提高性能
 */
class QCNetworkRequestPrivate : public QSharedData
{
public:
    QCNetworkRequestPrivate()
        : followLocation(true),
          rangeStart(-1),
          rangeEnd(-1),
          reqUrl(QUrl()),
          sslConfig(QCNetworkSslConfig::defaultConfig()),
          proxyConfig(std::nullopt),
          timeoutConfig(QCNetworkTimeoutConfig::defaultConfig()),
          httpVersion(QCNetworkHttpVersion::Http1_1),
          httpVersionExplicit(false),
          retryPolicy(QCNetworkRetryPolicy::noRetry()),
          requestPriority(QCNetworkRequestPriority::Normal),
          cachePolicy(QCNetworkCachePolicy::PreferCache)
    {
    }

    ~QCNetworkRequestPrivate() {}

    // ========== 基础字段 ==========
    bool followLocation;
    int rangeStart;
    int rangeEnd;
    QMap<QByteArray, QByteArray> rawHeaderMap;
    QUrl reqUrl;

    // ========== 高级配置字段 ==========
    QCNetworkSslConfig sslConfig;
    std::optional<QCNetworkProxyConfig> proxyConfig;
    QCNetworkTimeoutConfig timeoutConfig;
    QCNetworkHttpVersion httpVersion;
    bool httpVersionExplicit;

    // ========== 重试策略字段 ==========
    QCNetworkRetryPolicy retryPolicy;

    // ========== 请求优先级字段 ==========
    QCNetworkRequestPriority requestPriority;

    // ========== 缓存策略字段 ==========
    QCNetworkCachePolicy cachePolicy;
};

// ========== 构造和析构 ==========

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

QCNetworkRequest::~QCNetworkRequest()
{
}

// ========== 运算符 ==========

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

    return lhs->followLocation == rhs->followLocation
            && lhs->reqUrl == rhs->reqUrl
            && lhs->rawHeaderMap == rhs->rawHeaderMap
            && lhs->rangeStart == rhs->rangeStart
            && lhs->rangeEnd == rhs->rangeEnd
            && lhs->httpVersion == rhs->httpVersion;
    // Note: sslConfig, proxyConfig, timeoutConfig 不参与比较
    // 因为它们是请求执行配置而非请求标识
}

bool QCNetworkRequest::operator!=(const QCNetworkRequest &other) const
{
    return !(*this == other);
}

// ========== 基础配置 ==========

QUrl QCNetworkRequest::url() const
{
    return d.data()->reqUrl;
}

QCNetworkRequest& QCNetworkRequest::setFollowLocation(bool followLocation)
{
    d.data()->followLocation = followLocation;
    return *this;
}

bool QCNetworkRequest::followLocation() const
{
    return d.data()->followLocation;
}

QCNetworkRequest& QCNetworkRequest::setRawHeader(const QByteArray &headerName, const QByteArray &headerValue)
{
    d.data()->rawHeaderMap.insert(headerName, headerValue);
    return *this;
}

QList<QByteArray> QCNetworkRequest::rawHeaderList() const
{
    return d.data()->rawHeaderMap.keys();
}

QByteArray QCNetworkRequest::rawHeader(const QByteArray &headerName) const
{
    return d.data()->rawHeaderMap.value(headerName);
}

QCNetworkRequest& QCNetworkRequest::setRange(int start, int end)
{
    d.data()->rangeStart = start;
    d.data()->rangeEnd = end;
    return *this;
}

int QCNetworkRequest::rangeStart() const
{
    return d.data()->rangeStart;
}

int QCNetworkRequest::rangeEnd() const
{
    return d.data()->rangeEnd;
}

// ========== 高级配置 ==========

QCNetworkRequest& QCNetworkRequest::setSslConfig(const QCNetworkSslConfig &config)
{
    d.data()->sslConfig = config;
    return *this;
}

QCNetworkSslConfig QCNetworkRequest::sslConfig() const
{
    return d.data()->sslConfig;
}

QCNetworkRequest& QCNetworkRequest::setProxyConfig(const QCNetworkProxyConfig &config)
{
    d.data()->proxyConfig = config;
    return *this;
}

std::optional<QCNetworkProxyConfig> QCNetworkRequest::proxyConfig() const
{
    return d.data()->proxyConfig;
}

QCNetworkRequest& QCNetworkRequest::setTimeoutConfig(const QCNetworkTimeoutConfig &config)
{
    d.data()->timeoutConfig = config;
    return *this;
}

QCNetworkTimeoutConfig QCNetworkRequest::timeoutConfig() const
{
    return d.data()->timeoutConfig;
}

QCNetworkRequest& QCNetworkRequest::setHttpVersion(QCNetworkHttpVersion version)
{
    d.data()->httpVersion = version;
    d.data()->httpVersionExplicit = true;
    return *this;
}

QCNetworkHttpVersion QCNetworkRequest::httpVersion() const
{
    return d.data()->httpVersion;
}

bool QCNetworkRequest::isHttpVersionExplicit() const noexcept
{
    return d.data()->httpVersionExplicit;
}

QCNetworkRequest& QCNetworkRequest::setRetryPolicy(const QCNetworkRetryPolicy &policy)
{
    d.data()->retryPolicy = policy;
    return *this;
}

QCNetworkRetryPolicy QCNetworkRequest::retryPolicy() const
{
    return d.data()->retryPolicy;
}

// ========== 便捷方法 ==========

QCNetworkRequest& QCNetworkRequest::setTimeout(std::chrono::milliseconds timeout)
{
    d.data()->timeoutConfig.totalTimeout = timeout;
    return *this;
}

QCNetworkRequest& QCNetworkRequest::setConnectTimeout(std::chrono::milliseconds timeout)
{
    d.data()->timeoutConfig.connectTimeout = timeout;
    return *this;
}

// ========== 请求优先级 ==========

QCNetworkRequest& QCNetworkRequest::setPriority(QCNetworkRequestPriority priority)
{
    d.data()->requestPriority = priority;
    return *this;
}

QCNetworkRequestPriority QCNetworkRequest::priority() const
{
    return d.data()->requestPriority;
}

// ========== 缓存策略 ==========

QCNetworkRequest& QCNetworkRequest::setCachePolicy(QCNetworkCachePolicy policy)
{
    d.data()->cachePolicy = policy;
    return *this;
}

QCNetworkCachePolicy QCNetworkRequest::cachePolicy() const
{
    return d.data()->cachePolicy;
}

// ========== 调试输出 ==========

QDebug operator <<(QDebug dbg, const QCNetworkRequest &req)
{
    dbg.nospace() << "QCNetworkRequest("
                  << "url=" << req.url().toString()
                  << ", followLocation=" << req.followLocation()
                  << ", range=" << req.rangeStart() << "-" << req.rangeEnd()
                  << ", httpVersion=" << static_cast<int>(req.httpVersion())
                  << ")";
    return dbg.space();
}

} //namespace QCurl
