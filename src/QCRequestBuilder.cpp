#include "QCRequestBuilder.h"
#include "QCNetworkSslConfig.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkRequestPriority.h"
#include "QCNetworkRetryPolicy.h"
#include <QUrlQuery>
#include <QJsonDocument>
#include <QDebug>

namespace QCurl {

// ========== 构造函数和析构 ==========

QCRequestBuilder::QCRequestBuilder()
    : m_method(GET)
    , m_timeoutMs(-1)  // -1 表示使用默认超时
    , m_followRedirects(true)
    , m_hasSslConfig(false)
    , m_hasProxyConfig(false)
    , m_hasHttpVersion(false)
    , m_hasPriority(false)
    , m_hasRetryPolicy(false)
{
}

// ========== 基本配置方法实现 ==========

QCRequestBuilder& QCRequestBuilder::setUrl(const QString &url)
{
    m_url = QUrl(url);
    return *this;
}

QCRequestBuilder& QCRequestBuilder::setUrl(const QUrl &url)
{
    m_url = url;
    return *this;
}

QCRequestBuilder& QCRequestBuilder::setMethod(Method method)
{
    m_method = method;
    return *this;
}

// ========== Header 和查询参数实现 ==========

QCRequestBuilder& QCRequestBuilder::addHeader(const QString &key, const QString &value)
{
    m_headers[key] = value;
    return *this;
}

QCRequestBuilder& QCRequestBuilder::addQueryParam(const QString &key, const QString &value)
{
    m_queryParams.insert(key, value);
    return *this;
}

// ========== 超时配置实现 ==========

QCRequestBuilder& QCRequestBuilder::setTimeout(int seconds)
{
    m_timeoutMs = seconds * 1000;
    return *this;
}

QCRequestBuilder& QCRequestBuilder::setTimeoutMs(int milliseconds)
{
    m_timeoutMs = milliseconds;
    return *this;
}

// ========== 请求体配置实现 ==========

QCRequestBuilder& QCRequestBuilder::setBody(const QByteArray &data)
{
    m_body = data;
    return *this;
}

QCRequestBuilder& QCRequestBuilder::setJsonBody(const QJsonObject &json)
{
    // 序列化 JSON 对象
    QJsonDocument doc(json);
    m_body = doc.toJson(QJsonDocument::Compact);

    // 自动设置 Content-Type
    m_headers["Content-Type"] = "application/json";

    return *this;
}

QCRequestBuilder& QCRequestBuilder::setContentType(const QString &contentType)
{
    m_headers["Content-Type"] = contentType;
    return *this;
}

// ========== 高级配置实现 ==========

QCRequestBuilder& QCRequestBuilder::setSslConfig(const QCNetworkSslConfig &config)
{
    m_sslConfig = config;
    m_hasSslConfig = true;
    return *this;
}

QCRequestBuilder& QCRequestBuilder::setProxyConfig(const QCNetworkProxyConfig &config)
{
    m_proxyConfig = config;
    m_hasProxyConfig = true;
    return *this;
}

QCRequestBuilder& QCRequestBuilder::setHttpVersion(QCNetworkHttpVersion version)
{
    m_httpVersion = version;
    m_hasHttpVersion = true;
    return *this;
}

QCRequestBuilder& QCRequestBuilder::setPriority(QCNetworkRequestPriority priority)
{
    m_priority = priority;
    m_hasPriority = true;
    return *this;
}

QCRequestBuilder& QCRequestBuilder::setRetryPolicy(const QCNetworkRetryPolicy &policy)
{
    m_retryPolicy = policy;
    m_hasRetryPolicy = true;
    return *this;
}

QCRequestBuilder& QCRequestBuilder::setFollowRedirects(bool follow)
{
    m_followRedirects = follow;
    return *this;
}

// ========== 构建方法实现 ==========

QCNetworkRequest QCRequestBuilder::build() const
{
    // 验证必要的配置
    if (m_url.isEmpty()) {
        qWarning() << "[QCRequestBuilder] Cannot build request: URL is empty";
        return QCNetworkRequest();
    }

    // 构建带查询参数的 URL
    QUrl finalUrl = m_url;
    if (!m_queryParams.isEmpty()) {
        QUrlQuery query(finalUrl);
        for (auto it = m_queryParams.constBegin(); it != m_queryParams.constEnd(); ++it) {
            query.addQueryItem(it.key(), it.value());
        }
        finalUrl.setQuery(query);
    }

    // 创建基础 QCNetworkRequest
    QCNetworkRequest request(finalUrl);

    // 设置 Headers
    for (auto it = m_headers.constBegin(); it != m_headers.constEnd(); ++it) {
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }

    // 设置超时
    if (m_timeoutMs > 0) {
        request.setTimeout(std::chrono::milliseconds(m_timeoutMs));
    }

    // 设置重定向
    request.setFollowLocation(m_followRedirects);

    // 设置高级配置(如果有)
    if (m_hasSslConfig) {
        request.setSslConfig(m_sslConfig);
    }

    if (m_hasProxyConfig) {
        request.setProxyConfig(m_proxyConfig);
    }

    if (m_hasHttpVersion) {
        request.setHttpVersion(m_httpVersion);
    }

    if (m_hasPriority) {
        request.setPriority(m_priority);
    }

    if (m_hasRetryPolicy) {
        request.setRetryPolicy(m_retryPolicy);
    }

    return request;
}

void QCRequestBuilder::reset()
{
    m_url.clear();
    m_method = GET;
    m_headers.clear();
    m_queryParams.clear();
    m_timeoutMs = -1;
    m_body.clear();
    m_followRedirects = true;

    // 重置高级配置标志
    m_hasSslConfig = false;
    m_hasProxyConfig = false;
    m_hasHttpVersion = false;
    m_hasPriority = false;
    m_hasRetryPolicy = false;
}

} // namespace QCurl
