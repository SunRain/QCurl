/**
 * @file
 * @brief 声明网络请求配置对象。
 */

#ifndef QCNETWORKREQUEST_H
#define QCNETWORKREQUEST_H

#include "QCGlobal.h"
#include "QCNetworkRequestConfig.h"

#include <QByteArray>
#include <QList>
#include <QSharedDataPointer>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <chrono>
#include <optional>

class QDebug;
namespace QCurl {
class QCNetworkRequestPrivate;
class QCNetworkSslConfig;
class QCNetworkProxyConfig;
class QCNetworkTimeoutConfig;
class QCNetworkRetryPolicy;
enum class QCNetworkHttpVersion;
enum class QCNetworkRequestPriority;
enum class QCNetworkCachePolicy;

/** HTTP 请求值语义配置对象。 */
class QCURL_EXPORT QCNetworkRequest
{
public:
    QCNetworkRequest();
    QCNetworkRequest(const QUrl &url);
    QCNetworkRequest(const QCNetworkRequest &other);
    ~QCNetworkRequest();

    QCNetworkRequest &operator=(const QCNetworkRequest &other);
    /// 比较请求身份字段；执行配置族不参与相等性判定。
    bool operator==(const QCNetworkRequest &other) const;
    bool operator!=(const QCNetworkRequest &other) const;

    /// 返回请求 URL；默认构造时为空 URL。
    QUrl url() const;
    /// 设置请求 URL；不做网络访问或合法性探测。
    QCNetworkRequest &setUrl(const QUrl &url);

    /// 设置是否跟随重定向；默认开启。
    QCNetworkRequest &setFollowLocation(bool followLocation = true);
    /// 返回是否跟随重定向。
    bool followLocation() const;

    /// 设置最大重定向次数；负值会清空显式上限。
    QCNetworkRequest &setMaxRedirects(int maxRedirects);
    /// 返回最大重定向次数；`std::nullopt` 表示使用底层默认。
    [[nodiscard]] std::optional<int> maxRedirects() const;

    QCNetworkRequest &setPostRedirectPolicy(QCNetworkPostRedirectPolicy policy);
    [[nodiscard]] QCNetworkPostRedirectPolicy postRedirectPolicy() const;

    QCNetworkRequest &setAutoRefererEnabled(bool enabled = true);
    [[nodiscard]] bool autoRefererEnabled() const;

    QCNetworkRequest &setReferer(const QString &referer);
    [[nodiscard]] QString referer() const;

    QCNetworkRequest &setAllowUnrestrictedSensitiveHeadersOnRedirect(bool enabled = true);
    [[nodiscard]] bool allowUnrestrictedSensitiveHeadersOnRedirect() const;

    QCNetworkRequest &setRawHeader(const QByteArray &headerName, const QByteArray &headerValue);
    QList<QByteArray> rawHeaderList() const;
    QByteArray rawHeader(const QByteArray &headerName) const;

    QCNetworkRequest &setRange(int start, int end);
    int rangeStart() const;
    int rangeEnd() const;

    QCNetworkRequest &setSslConfig(const QCNetworkSslConfig &config);
    [[nodiscard]] QCNetworkSslConfig sslConfig() const;

    QCNetworkRequest &setProxyConfig(const QCNetworkProxyConfig &config);
    [[nodiscard]] std::optional<QCNetworkProxyConfig> proxyConfig() const;

    QCNetworkRequest &setTimeoutConfig(const QCNetworkTimeoutConfig &config);
    [[nodiscard]] QCNetworkTimeoutConfig timeoutConfig() const;

    QCNetworkRequest &setHttpVersion(QCNetworkHttpVersion version);
    [[nodiscard]] QCNetworkHttpVersion httpVersion() const;
    [[nodiscard]] bool isHttpVersionExplicit() const noexcept;

    QCNetworkRequest &setRetryPolicy(const QCNetworkRetryPolicy &policy);
    [[nodiscard]] QCNetworkRetryPolicy retryPolicy() const;
    [[nodiscard]] bool isRetryPolicyExplicit() const noexcept;

    QCNetworkRequest &setHttpAuth(const QCNetworkHttpAuthConfig &config);
    [[nodiscard]] std::optional<QCNetworkHttpAuthConfig> httpAuth() const;
    /// 清空请求级 HTTP 认证配置；不会清理已设置的 raw Authorization header。
    QCNetworkRequest &clearHttpAuth();

    QCNetworkRequest &setRedirectConfig(const QCNetworkRedirectConfig &config);
    [[nodiscard]] QCNetworkRedirectConfig redirectConfig() const;

    QCNetworkRequest &setTransferConfig(const QCNetworkTransferConfig &config);
    [[nodiscard]] QCNetworkTransferConfig transferConfig() const;

    QCNetworkRequest &setTimeout(std::chrono::milliseconds timeout);
    QCNetworkRequest &setConnectTimeout(std::chrono::milliseconds timeout);

    QCNetworkRequest &setAutoDecompressionEnabled(bool enabled = true);
    [[nodiscard]] bool autoDecompressionEnabled() const;

    QCNetworkRequest &setAcceptedEncodings(const QStringList &encodings);
    [[nodiscard]] QStringList acceptedEncodings() const;

    QCNetworkRequest &setMaxDownloadBytesPerSec(qint64 bytesPerSec);
    [[nodiscard]] std::optional<qint64> maxDownloadBytesPerSec() const;

    QCNetworkRequest &setMaxUploadBytesPerSec(qint64 bytesPerSec);
    [[nodiscard]] std::optional<qint64> maxUploadBytesPerSec() const;

    QCNetworkRequest &setBackpressureLimitBytes(qint64 bytes);
    [[nodiscard]] qint64 backpressureLimitBytes() const noexcept;

    QCNetworkRequest &setBackpressureResumeBytes(qint64 bytes);
    [[nodiscard]] qint64 backpressureResumeBytes() const noexcept;

    QCNetworkRequest &setExpect100ContinueTimeout(std::chrono::milliseconds timeout);
    [[nodiscard]] std::optional<std::chrono::milliseconds> expect100ContinueTimeout() const;

    QCNetworkRequest &setIpResolve(QCNetworkIpResolve resolve);
    [[nodiscard]] std::optional<QCNetworkIpResolve> ipResolve() const;

#ifdef QCURL_ENABLE_ADVANCED_REQUEST_NETWORK_PATH_API
    QCNetworkRequest &setHappyEyeballsTimeout(std::chrono::milliseconds timeout);
    [[nodiscard]] std::optional<std::chrono::milliseconds> happyEyeballsTimeout() const;

    QCNetworkRequest &setNetworkInterface(const QString &interfaceName);
    [[nodiscard]] std::optional<QString> networkInterface() const;

    QCNetworkRequest &setLocalPortRange(int port, int range = 0);
    [[nodiscard]] std::optional<int> localPort() const;
    [[nodiscard]] std::optional<int> localPortRange() const;

    QCNetworkRequest &setResolveOverride(const QStringList &entries);
    [[nodiscard]] std::optional<QStringList> resolveOverride() const;

    QCNetworkRequest &setConnectTo(const QStringList &entries);
    [[nodiscard]] std::optional<QStringList> connectTo() const;

    QCNetworkRequest &setDnsServers(const QStringList &servers);
    [[nodiscard]] std::optional<QStringList> dnsServers() const;

    QCNetworkRequest &setDohUrl(const QUrl &url);
    [[nodiscard]] std::optional<QUrl> dohUrl() const;
#endif

    QCNetworkRequest &setAllowedProtocols(const QStringList &protocols);
    [[nodiscard]] std::optional<QStringList> allowedProtocols() const;

    QCNetworkRequest &setAllowedRedirectProtocols(const QStringList &protocols);
    [[nodiscard]] std::optional<QStringList> allowedRedirectProtocols() const;

    QCNetworkRequest &setUnsupportedSecurityOptionPolicy(QCUnsupportedSecurityOptionPolicy policy);
    [[nodiscard]] QCUnsupportedSecurityOptionPolicy unsupportedSecurityOptionPolicy() const;

    QCNetworkRequest &setLane(const QString &lane);
    [[nodiscard]] QString lane() const;

    QCNetworkRequest &setPriority(QCNetworkRequestPriority priority);
    [[nodiscard]] QCNetworkRequestPriority priority() const;

    QCNetworkRequest &setCachePolicy(QCNetworkCachePolicy policy);
    [[nodiscard]] QCNetworkCachePolicy cachePolicy() const;

private:
    QSharedDataPointer<QCurl::QCNetworkRequestPrivate> d;
};

QCURL_EXPORT QDebug operator<<(QDebug dbg, const QCNetworkRequest &req);

} // namespace QCurl
#endif // QCNETWORKREQUEST_H
