#include "QCNetworkRequest.h"

#include "QCNetworkCachePolicy.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkRequestPriority.h"
#include "QCNetworkRetryPolicy.h"
#include "QCNetworkSslConfig.h"
#include "QCNetworkTimeoutConfig.h"

#include <QDebug>
#include <QIODevice>
#include <QMap>
#include <QPointer>
#include <QSharedData>
#include <QUrl>

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
        : followLocation(true)
        , rangeStart(-1)
        , rangeEnd(-1)
        , reqUrl(QUrl())
        , maxRedirects(std::nullopt)
        , postRedirectPolicy(QCNetworkPostRedirectPolicy::Default)
        , autoRefererEnabled(false)
        , referer(QString())
        , allowUnrestrictedSensitiveHeadersOnRedirect(false)
        , autoDecompressionEnabled(false)
        , acceptedEncodings(QStringList())
        , maxDownloadBytesPerSec(std::nullopt)
        , maxUploadBytesPerSec(std::nullopt)
        , backpressureLimitBytes(0)
        , backpressureResumeBytes(0)
        , uploadDevice(nullptr)
        , uploadFilePath(std::nullopt)
        , uploadBodySizeBytes(std::nullopt)
        , allowChunkedUploadForPost(false)
        , expect100ContinueTimeout(std::nullopt)
        , ipResolve(std::nullopt)
        , happyEyeballsTimeout(std::nullopt)
        , networkInterface(std::nullopt)
        , localPort(std::nullopt)
        , localPortRange(std::nullopt)
        , resolveOverride(std::nullopt)
        , connectTo(std::nullopt)
        , dnsServers(std::nullopt)
        , dohUrl(std::nullopt)
        , allowedProtocols(std::nullopt)
        , allowedRedirectProtocols(std::nullopt)
        , unsupportedSecurityOptionPolicy(QCUnsupportedSecurityOptionPolicy::Fail)
        , sslConfig(QCNetworkSslConfig::defaultConfig())
        , proxyConfig(std::nullopt)
        , timeoutConfig(QCNetworkTimeoutConfig::defaultConfig())
        , httpVersion(QCNetworkHttpVersion::Http1_1)
        , httpVersionExplicit(false)
        , retryPolicy(QCNetworkRetryPolicy::noRetry())
        , retryPolicyExplicit(false)
        , httpAuthConfig(std::nullopt)
        , requestPriority(QCNetworkRequestPriority::Normal)
        , cachePolicy(QCNetworkCachePolicy::PreferCache)
    {}

    ~QCNetworkRequestPrivate() {}

    // ========== 基础字段 ==========
    bool followLocation;
    int rangeStart;
    int rangeEnd;
    QMap<QByteArray, QByteArray> rawHeaderMap;
    QUrl reqUrl;

    // ========== 重定向策略（M1） ==========
    std::optional<int> maxRedirects;
    QCNetworkPostRedirectPolicy postRedirectPolicy;
    bool autoRefererEnabled;
    QString referer;
    bool allowUnrestrictedSensitiveHeadersOnRedirect;

    // ========== 自动解压（M1） ==========
    bool autoDecompressionEnabled;
    QStringList acceptedEncodings;

    // ========== 传输限速（M1） ==========
    std::optional<qint64> maxDownloadBytesPerSec;
    std::optional<qint64> maxUploadBytesPerSec;

    // ========== 下载 backpressure（P2） ==========
    qint64 backpressureLimitBytes;
    qint64 backpressureResumeBytes;

    // ========== 流式上传（M2） ==========
    QPointer<QIODevice> uploadDevice;
    std::optional<QString> uploadFilePath;
    std::optional<qint64> uploadBodySizeBytes;
    bool allowChunkedUploadForPost;

    // ========== Expect: 100-continue（P1） ==========
    std::optional<std::chrono::milliseconds> expect100ContinueTimeout;

    // ========== 网络路径与 DNS 控制（M4） ==========
    std::optional<QCNetworkIpResolve> ipResolve;
    std::optional<std::chrono::milliseconds> happyEyeballsTimeout;
    std::optional<QString> networkInterface;
    std::optional<int> localPort;
    std::optional<int> localPortRange;
    std::optional<QStringList> resolveOverride;
    std::optional<QStringList> connectTo;
    std::optional<QStringList> dnsServers;
    std::optional<QUrl> dohUrl;

    // ========== 协议白名单（M5，安全） ==========
    std::optional<QStringList> allowedProtocols;
    std::optional<QStringList> allowedRedirectProtocols;
    QCUnsupportedSecurityOptionPolicy unsupportedSecurityOptionPolicy;

    // ========== 高级配置字段 ==========
    QCNetworkSslConfig sslConfig;
    std::optional<QCNetworkProxyConfig> proxyConfig;
    QCNetworkTimeoutConfig timeoutConfig;
    QCNetworkHttpVersion httpVersion;
    bool httpVersionExplicit;

    // ========== 重试策略字段 ==========
    QCNetworkRetryPolicy retryPolicy;
    bool retryPolicyExplicit;

    // ========== HTTP 认证（请求级） ==========
    std::optional<QCNetworkHttpAuthConfig> httpAuthConfig;

    // ========== 请求优先级字段 ==========
    QCNetworkRequestPriority requestPriority;

    // ========== 缓存策略字段 ==========
    QCNetworkCachePolicy cachePolicy;
};

// ========== 构造和析构 ==========

QCNetworkRequest::QCNetworkRequest()
    : d(new QCNetworkRequestPrivate())
{}

QCNetworkRequest::QCNetworkRequest(const QUrl &url)
    : d(new QCNetworkRequestPrivate())
{
    d.data()->reqUrl = url;
}

QCNetworkRequest::QCNetworkRequest(const QCNetworkRequest &other)
    : d(other.d)
{}

QCNetworkRequest::~QCNetworkRequest() {}

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

    return lhs->followLocation == rhs->followLocation && lhs->reqUrl == rhs->reqUrl
           && lhs->rawHeaderMap == rhs->rawHeaderMap && lhs->rangeStart == rhs->rangeStart
           && lhs->rangeEnd == rhs->rangeEnd && lhs->httpVersion == rhs->httpVersion;
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

QCNetworkRequest &QCNetworkRequest::setUrl(const QUrl &url)
{
    d.data()->reqUrl = url;
    return *this;
}

QCNetworkRequest &QCNetworkRequest::setFollowLocation(bool followLocation)
{
    d.data()->followLocation = followLocation;
    return *this;
}

bool QCNetworkRequest::followLocation() const
{
    return d.data()->followLocation;
}

QCNetworkRequest &QCNetworkRequest::setMaxRedirects(int maxRedirects)
{
    if (maxRedirects < 0) {
        qWarning() << "QCNetworkRequest: maxRedirects must be >= 0, got" << maxRedirects
                   << "(ignored)";
        d.data()->maxRedirects.reset();
        return *this;
    }

    d.data()->maxRedirects = maxRedirects;
    return *this;
}

std::optional<int> QCNetworkRequest::maxRedirects() const
{
    return d.data()->maxRedirects;
}

QCNetworkRequest &QCNetworkRequest::setPostRedirectPolicy(QCNetworkPostRedirectPolicy policy)
{
    d.data()->postRedirectPolicy = policy;
    return *this;
}

QCNetworkPostRedirectPolicy QCNetworkRequest::postRedirectPolicy() const
{
    return d.data()->postRedirectPolicy;
}

QCNetworkRequest &QCNetworkRequest::setAutoRefererEnabled(bool enabled)
{
    d.data()->autoRefererEnabled = enabled;
    return *this;
}

bool QCNetworkRequest::autoRefererEnabled() const
{
    return d.data()->autoRefererEnabled;
}

QCNetworkRequest &QCNetworkRequest::setReferer(const QString &referer)
{
    d.data()->referer = referer;
    return *this;
}

QString QCNetworkRequest::referer() const
{
    return d.data()->referer;
}

QCNetworkRequest &QCNetworkRequest::setAllowUnrestrictedSensitiveHeadersOnRedirect(bool enabled)
{
    d.data()->allowUnrestrictedSensitiveHeadersOnRedirect = enabled;
    return *this;
}

bool QCNetworkRequest::allowUnrestrictedSensitiveHeadersOnRedirect() const
{
    return d.data()->allowUnrestrictedSensitiveHeadersOnRedirect;
}

QCNetworkRequest &QCNetworkRequest::setRawHeader(const QByteArray &headerName,
                                                 const QByteArray &headerValue)
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

QCNetworkRequest &QCNetworkRequest::setRange(int start, int end)
{
    d.data()->rangeStart = start;
    d.data()->rangeEnd   = end;
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

QCNetworkRequest &QCNetworkRequest::setSslConfig(const QCNetworkSslConfig &config)
{
    d.data()->sslConfig = config;
    return *this;
}

QCNetworkSslConfig QCNetworkRequest::sslConfig() const
{
    return d.data()->sslConfig;
}

QCNetworkRequest &QCNetworkRequest::setProxyConfig(const QCNetworkProxyConfig &config)
{
    d.data()->proxyConfig = config;
    return *this;
}

std::optional<QCNetworkProxyConfig> QCNetworkRequest::proxyConfig() const
{
    return d.data()->proxyConfig;
}

QCNetworkRequest &QCNetworkRequest::setTimeoutConfig(const QCNetworkTimeoutConfig &config)
{
    d.data()->timeoutConfig = config;
    return *this;
}

QCNetworkTimeoutConfig QCNetworkRequest::timeoutConfig() const
{
    return d.data()->timeoutConfig;
}

QCNetworkRequest &QCNetworkRequest::setHttpVersion(QCNetworkHttpVersion version)
{
    d.data()->httpVersion         = version;
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

QCNetworkRequest &QCNetworkRequest::setRetryPolicy(const QCNetworkRetryPolicy &policy)
{
    d.data()->retryPolicy         = policy;
    d.data()->retryPolicyExplicit = true;
    return *this;
}

QCNetworkRetryPolicy QCNetworkRequest::retryPolicy() const
{
    return d.data()->retryPolicy;
}

bool QCNetworkRequest::isRetryPolicyExplicit() const noexcept
{
    return d.data()->retryPolicyExplicit;
}

// ========== HTTP 认证 ==========

QCNetworkRequest &QCNetworkRequest::setHttpAuth(const QCNetworkHttpAuthConfig &config)
{
    d.data()->httpAuthConfig = config;
    return *this;
}

std::optional<QCNetworkHttpAuthConfig> QCNetworkRequest::httpAuth() const
{
    return d.data()->httpAuthConfig;
}

QCNetworkRequest &QCNetworkRequest::clearHttpAuth()
{
    d.data()->httpAuthConfig.reset();
    return *this;
}

// ========== 便捷方法 ==========

QCNetworkRequest &QCNetworkRequest::setTimeout(std::chrono::milliseconds timeout)
{
    d.data()->timeoutConfig.totalTimeout = timeout;
    return *this;
}

QCNetworkRequest &QCNetworkRequest::setConnectTimeout(std::chrono::milliseconds timeout)
{
    d.data()->timeoutConfig.connectTimeout = timeout;
    return *this;
}

QCNetworkRequest &QCNetworkRequest::setAutoDecompressionEnabled(bool enabled)
{
    d.data()->autoDecompressionEnabled = enabled;
    return *this;
}

bool QCNetworkRequest::autoDecompressionEnabled() const
{
    return d.data()->autoDecompressionEnabled;
}

QCNetworkRequest &QCNetworkRequest::setAcceptedEncodings(const QStringList &encodings)
{
    d.data()->acceptedEncodings        = encodings;
    d.data()->autoDecompressionEnabled = !encodings.isEmpty();
    return *this;
}

QStringList QCNetworkRequest::acceptedEncodings() const
{
    return d.data()->acceptedEncodings;
}

QCNetworkRequest &QCNetworkRequest::setMaxDownloadBytesPerSec(qint64 bytesPerSec)
{
    if (bytesPerSec < 0) {
        qWarning() << "QCNetworkRequest: maxDownloadBytesPerSec must be >= 0, got" << bytesPerSec
                   << "(ignored)";
        d.data()->maxDownloadBytesPerSec.reset();
        return *this;
    }

    if (bytesPerSec == 0) {
        d.data()->maxDownloadBytesPerSec.reset();
        return *this;
    }

    d.data()->maxDownloadBytesPerSec = bytesPerSec;
    return *this;
}

std::optional<qint64> QCNetworkRequest::maxDownloadBytesPerSec() const
{
    return d.data()->maxDownloadBytesPerSec;
}

QCNetworkRequest &QCNetworkRequest::setMaxUploadBytesPerSec(qint64 bytesPerSec)
{
    if (bytesPerSec < 0) {
        qWarning() << "QCNetworkRequest: maxUploadBytesPerSec must be >= 0, got" << bytesPerSec
                   << "(ignored)";
        d.data()->maxUploadBytesPerSec.reset();
        return *this;
    }

    if (bytesPerSec == 0) {
        d.data()->maxUploadBytesPerSec.reset();
        return *this;
    }

    d.data()->maxUploadBytesPerSec = bytesPerSec;
    return *this;
}

std::optional<qint64> QCNetworkRequest::maxUploadBytesPerSec() const
{
    return d.data()->maxUploadBytesPerSec;
}

QCNetworkRequest &QCNetworkRequest::setBackpressureLimitBytes(qint64 bytes)
{
    if (bytes < 0) {
        qWarning() << "QCNetworkRequest: backpressureLimitBytes must be >= 0, got" << bytes
                   << "(ignored)";
        bytes = 0;
    }

    // soft limit 推荐下限：
    // - libcurl write callback 通常不会超过 CURL_MAX_WRITE_SIZE（16KiB）
    // - 过小的 limit 会导致频繁 pause/resume 或产生非预期的“看起来卡住”的体验（即使最终可恢复）
    if (bytes > 0) {
        constexpr qint64 kRecommendedMinBytes = 16 * 1024;
        if (bytes < kRecommendedMinBytes) {
            qWarning()
                << "QCNetworkRequest: backpressureLimitBytes is very small:" << bytes
                << "bytes; backpressure is a soft limit (high watermark) and bytesAvailable() may"
                   " temporarily exceed it (bounded). Recommend >="
                << kRecommendedMinBytes << "bytes";
        }
    }

    d.data()->backpressureLimitBytes = bytes;
    if (bytes <= 0) {
        d.data()->backpressureResumeBytes = 0;
    }
    return *this;
}

qint64 QCNetworkRequest::backpressureLimitBytes() const noexcept
{
    return d.data()->backpressureLimitBytes;
}

QCNetworkRequest &QCNetworkRequest::setBackpressureResumeBytes(qint64 bytes)
{
    if (bytes < 0) {
        qWarning() << "QCNetworkRequest: backpressureResumeBytes must be >= 0, got" << bytes
                   << "(ignored)";
        bytes = 0;
    }

    const qint64 limit = d.data()->backpressureLimitBytes;
    if (limit > 0 && bytes > 0 && bytes >= limit) {
        qWarning()
            << "QCNetworkRequest: backpressureResumeBytes must be < backpressureLimitBytes, got"
            << bytes << "(limit=" << limit << "; use default limit/2)";
        bytes = 0;
    }

    d.data()->backpressureResumeBytes = bytes;
    return *this;
}

qint64 QCNetworkRequest::backpressureResumeBytes() const noexcept
{
    return d.data()->backpressureResumeBytes;
}

QCNetworkRequest &QCNetworkRequest::setUploadDevice(QIODevice *device,
                                                    std::optional<qint64> sizeBytes)
{
    if (!device) {
        return clearUploadBody();
    }

    if (!device->isReadable()) {
        qWarning() << "QCNetworkRequest: uploadDevice must be readable (ignored)";
        return clearUploadBody();
    }

    d.data()->uploadDevice = device;
    d.data()->uploadFilePath.reset();

    if (sizeBytes.has_value() && sizeBytes.value() < 0) {
        qWarning() << "QCNetworkRequest: upload sizeBytes must be >= 0, got" << sizeBytes.value()
                   << "(ignored)";
        d.data()->uploadBodySizeBytes.reset();
    } else {
        d.data()->uploadBodySizeBytes = sizeBytes;
    }
    return *this;
}

QIODevice *QCNetworkRequest::uploadDevice() const
{
    return d.data()->uploadDevice.data();
}

QCNetworkRequest &QCNetworkRequest::setUploadFile(const QString &filePath,
                                                  std::optional<qint64> sizeBytes)
{
    if (filePath.trimmed().isEmpty()) {
        return clearUploadBody();
    }

    d.data()->uploadDevice   = nullptr;
    d.data()->uploadFilePath = filePath;

    if (sizeBytes.has_value() && sizeBytes.value() < 0) {
        qWarning() << "QCNetworkRequest: upload sizeBytes must be >= 0, got" << sizeBytes.value()
                   << "(ignored)";
        d.data()->uploadBodySizeBytes.reset();
    } else {
        d.data()->uploadBodySizeBytes = sizeBytes;
    }
    return *this;
}

std::optional<QString> QCNetworkRequest::uploadFilePath() const
{
    return d.data()->uploadFilePath;
}

std::optional<qint64> QCNetworkRequest::uploadBodySizeBytes() const
{
    return d.data()->uploadBodySizeBytes;
}

QCNetworkRequest &QCNetworkRequest::setAllowChunkedUploadForPost(bool enabled)
{
    d.data()->allowChunkedUploadForPost = enabled;
    return *this;
}

bool QCNetworkRequest::allowChunkedUploadForPost() const
{
    return d.data()->allowChunkedUploadForPost;
}

QCNetworkRequest &QCNetworkRequest::clearUploadBody()
{
    d.data()->uploadDevice = nullptr;
    d.data()->uploadFilePath.reset();
    d.data()->uploadBodySizeBytes.reset();
    return *this;
}

QCNetworkRequest &QCNetworkRequest::setExpect100ContinueTimeout(std::chrono::milliseconds timeout)
{
    if (timeout.count() < 0) {
        qWarning() << "QCNetworkRequest: expect100ContinueTimeout must be >= 0, got"
                   << timeout.count() << "(ignored)";
        d.data()->expect100ContinueTimeout.reset();
        return *this;
    }

    d.data()->expect100ContinueTimeout = timeout;
    return *this;
}

std::optional<std::chrono::milliseconds> QCNetworkRequest::expect100ContinueTimeout() const
{
    return d.data()->expect100ContinueTimeout;
}

QCNetworkRequest &QCNetworkRequest::setIpResolve(QCNetworkIpResolve resolve)
{
    if (resolve == QCNetworkIpResolve::Any) {
        d.data()->ipResolve.reset();
        return *this;
    }

    d.data()->ipResolve = resolve;
    return *this;
}

std::optional<QCNetworkIpResolve> QCNetworkRequest::ipResolve() const
{
    return d.data()->ipResolve;
}

QCNetworkRequest &QCNetworkRequest::setHappyEyeballsTimeout(std::chrono::milliseconds timeout)
{
    if (timeout.count() < 0) {
        qWarning() << "QCNetworkRequest: happyEyeballsTimeout must be >= 0, got" << timeout.count()
                   << "(ignored)";
        d.data()->happyEyeballsTimeout.reset();
        return *this;
    }

    d.data()->happyEyeballsTimeout = timeout;
    return *this;
}

std::optional<std::chrono::milliseconds> QCNetworkRequest::happyEyeballsTimeout() const
{
    return d.data()->happyEyeballsTimeout;
}

QCNetworkRequest &QCNetworkRequest::setNetworkInterface(const QString &interfaceName)
{
    const QString trimmed = interfaceName.trimmed();
    if (trimmed.isEmpty()) {
        d.data()->networkInterface.reset();
        return *this;
    }

    d.data()->networkInterface = trimmed;
    return *this;
}

std::optional<QString> QCNetworkRequest::networkInterface() const
{
    return d.data()->networkInterface;
}

QCNetworkRequest &QCNetworkRequest::setLocalPortRange(int port, int range)
{
    if (port <= 0 || port > 65535) {
        qWarning() << "QCNetworkRequest: localPort must be in [1, 65535], got" << port
                   << "(ignored)";
        d.data()->localPort.reset();
        d.data()->localPortRange.reset();
        return *this;
    }

    if (range < 0 || range > 65535) {
        qWarning() << "QCNetworkRequest: localPortRange must be in [0, 65535], got" << range
                   << "(ignored)";
        d.data()->localPort.reset();
        d.data()->localPortRange.reset();
        return *this;
    }

    d.data()->localPort      = port;
    d.data()->localPortRange = range;
    return *this;
}

std::optional<int> QCNetworkRequest::localPort() const
{
    return d.data()->localPort;
}

std::optional<int> QCNetworkRequest::localPortRange() const
{
    return d.data()->localPortRange;
}

QCNetworkRequest &QCNetworkRequest::setResolveOverride(const QStringList &entries)
{
    QStringList cleaned;
    cleaned.reserve(entries.size());
    for (const QString &e : entries) {
        const QString v = e.trimmed();
        if (!v.isEmpty()) {
            cleaned.append(v);
        }
    }

    if (cleaned.isEmpty()) {
        d.data()->resolveOverride.reset();
        return *this;
    }

    d.data()->resolveOverride = cleaned;
    return *this;
}

std::optional<QStringList> QCNetworkRequest::resolveOverride() const
{
    return d.data()->resolveOverride;
}

QCNetworkRequest &QCNetworkRequest::setConnectTo(const QStringList &entries)
{
    QStringList cleaned;
    cleaned.reserve(entries.size());
    for (const QString &e : entries) {
        const QString v = e.trimmed();
        if (!v.isEmpty()) {
            cleaned.append(v);
        }
    }

    if (cleaned.isEmpty()) {
        d.data()->connectTo.reset();
        return *this;
    }

    d.data()->connectTo = cleaned;
    return *this;
}

std::optional<QStringList> QCNetworkRequest::connectTo() const
{
    return d.data()->connectTo;
}

QCNetworkRequest &QCNetworkRequest::setDnsServers(const QStringList &servers)
{
    QStringList cleaned;
    cleaned.reserve(servers.size());
    for (const QString &s : servers) {
        const QString v = s.trimmed();
        if (!v.isEmpty()) {
            cleaned.append(v);
        }
    }

    if (cleaned.isEmpty()) {
        d.data()->dnsServers.reset();
        return *this;
    }

    d.data()->dnsServers = cleaned;
    return *this;
}

std::optional<QStringList> QCNetworkRequest::dnsServers() const
{
    return d.data()->dnsServers;
}

QCNetworkRequest &QCNetworkRequest::setDohUrl(const QUrl &url)
{
    if (url.isEmpty()) {
        d.data()->dohUrl.reset();
        return *this;
    }

    d.data()->dohUrl = url;
    return *this;
}

std::optional<QUrl> QCNetworkRequest::dohUrl() const
{
    return d.data()->dohUrl;
}

QCNetworkRequest &QCNetworkRequest::setAllowedProtocols(const QStringList &protocols)
{
    QStringList cleaned;
    cleaned.reserve(protocols.size());
    for (const QString &p : protocols) {
        const QString v = p.trimmed();
        if (!v.isEmpty()) {
            cleaned.append(v);
        }
    }

    if (cleaned.isEmpty()) {
        d.data()->allowedProtocols.reset();
        return *this;
    }

    d.data()->allowedProtocols = cleaned;
    return *this;
}

std::optional<QStringList> QCNetworkRequest::allowedProtocols() const
{
    return d.data()->allowedProtocols;
}

QCNetworkRequest &QCNetworkRequest::setAllowedRedirectProtocols(const QStringList &protocols)
{
    QStringList cleaned;
    cleaned.reserve(protocols.size());
    for (const QString &p : protocols) {
        const QString v = p.trimmed();
        if (!v.isEmpty()) {
            cleaned.append(v);
        }
    }

    if (cleaned.isEmpty()) {
        d.data()->allowedRedirectProtocols.reset();
        return *this;
    }

    d.data()->allowedRedirectProtocols = cleaned;
    return *this;
}

std::optional<QStringList> QCNetworkRequest::allowedRedirectProtocols() const
{
    return d.data()->allowedRedirectProtocols;
}

QCNetworkRequest &QCNetworkRequest::setUnsupportedSecurityOptionPolicy(
    QCUnsupportedSecurityOptionPolicy policy)
{
    d.data()->unsupportedSecurityOptionPolicy = policy;
    return *this;
}

QCUnsupportedSecurityOptionPolicy QCNetworkRequest::unsupportedSecurityOptionPolicy() const
{
    return d.data()->unsupportedSecurityOptionPolicy;
}

// ========== 请求优先级 ==========

QCNetworkRequest &QCNetworkRequest::setPriority(QCNetworkRequestPriority priority)
{
    d.data()->requestPriority = priority;
    return *this;
}

QCNetworkRequestPriority QCNetworkRequest::priority() const
{
    return d.data()->requestPriority;
}

// ========== 缓存策略 ==========

QCNetworkRequest &QCNetworkRequest::setCachePolicy(QCNetworkCachePolicy policy)
{
    d.data()->cachePolicy = policy;
    return *this;
}

QCNetworkCachePolicy QCNetworkRequest::cachePolicy() const
{
    return d.data()->cachePolicy;
}

// ========== 调试输出 ==========

QDebug operator<<(QDebug dbg, const QCNetworkRequest &req)
{
    dbg.nospace() << "QCNetworkRequest("
                  << "url=" << req.url().toString() << ", followLocation=" << req.followLocation()
                  << ", range=" << req.rangeStart() << "-" << req.rangeEnd()
                  << ", httpVersion=" << static_cast<int>(req.httpVersion()) << ")";
    return dbg.space();
}

} // namespace QCurl
