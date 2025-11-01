#include "QCRequest.h"
#include "QCNetworkSslConfig.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkRequestPriority.h"
#include "QCNetworkRetryPolicy.h"
#include <QUrlQuery>
#include <QJsonDocument>
#include <QDebug>

namespace QCurl {

// ========== 静态工厂方法实现 ==========

QCRequest QCRequest::get(const QString &url)
{
    return QCRequest(QUrl(url), "GET");
}

QCRequest QCRequest::get(const QUrl &url)
{
    return QCRequest(url, "GET");
}

QCRequest QCRequest::post(const QString &url)
{
    return QCRequest(QUrl(url), "POST");
}

QCRequest QCRequest::post(const QUrl &url)
{
    return QCRequest(url, "POST");
}

QCRequest QCRequest::put(const QString &url)
{
    return QCRequest(QUrl(url), "PUT");
}

QCRequest QCRequest::put(const QUrl &url)
{
    return QCRequest(url, "PUT");
}

QCRequest QCRequest::del(const QString &url)
{
    return QCRequest(QUrl(url), "DELETE");
}

QCRequest QCRequest::del(const QUrl &url)
{
    return QCRequest(url, "DELETE");
}

QCRequest QCRequest::patch(const QString &url)
{
    return QCRequest(QUrl(url), "PATCH");
}

QCRequest QCRequest::patch(const QUrl &url)
{
    return QCRequest(url, "PATCH");
}

QCRequest QCRequest::head(const QString &url)
{
    return QCRequest(QUrl(url), "HEAD");
}

QCRequest QCRequest::head(const QUrl &url)
{
    return QCRequest(url, "HEAD");
}

// ========== 私有构造函数 ==========

QCRequest::QCRequest(const QUrl &url, const QString &method)
    : m_request(url)
    , m_method(method)
{
}

// ========== 流式配置方法实现 ==========

QCRequest& QCRequest::withHeader(const QString &key, const QString &value)
{
    m_request.setRawHeader(key.toUtf8(), value.toUtf8());
    return *this;
}

QCRequest& QCRequest::withQueryParam(const QString &key, const QString &value)
{
    QUrl url = m_request.url();
    QUrlQuery query(url);
    query.addQueryItem(key, value);
    url.setQuery(query);

    // 需要创建新的 request 对象以更新 URL
    m_request = QCNetworkRequest(url);

    return *this;
}

QCRequest& QCRequest::withTimeout(std::chrono::seconds timeout)
{
    m_request.setTimeout(std::chrono::duration_cast<std::chrono::milliseconds>(timeout));
    return *this;
}

QCRequest& QCRequest::withTimeout(std::chrono::milliseconds timeout)
{
    m_request.setTimeout(timeout);
    return *this;
}

QCRequest& QCRequest::withJson(const QJsonObject &json)
{
    // 序列化 JSON 对象
    QJsonDocument doc(json);
    m_postData = doc.toJson(QJsonDocument::Compact);

    // 自动设置 Content-Type
    m_request.setRawHeader("Content-Type", "application/json");

    return *this;
}

QCRequest& QCRequest::withBody(const QByteArray &data, const QString &contentType)
{
    m_postData = data;

    if (!contentType.isEmpty()) {
        m_request.setRawHeader("Content-Type", contentType.toUtf8());
    } else {
        m_request.setRawHeader("Content-Type", "application/octet-stream");
    }

    return *this;
}

QCRequest& QCRequest::withSslConfig(const QCNetworkSslConfig &config)
{
    m_request.setSslConfig(config);
    return *this;
}

QCRequest& QCRequest::withProxyConfig(const QCNetworkProxyConfig &config)
{
    m_request.setProxyConfig(config);
    return *this;
}

QCRequest& QCRequest::withHttpVersion(QCNetworkHttpVersion version)
{
    m_request.setHttpVersion(version);
    return *this;
}

QCRequest& QCRequest::withPriority(QCNetworkRequestPriority priority)
{
    m_request.setPriority(priority);
    return *this;
}

QCRequest& QCRequest::withRetryPolicy(const QCNetworkRetryPolicy &policy)
{
    m_request.setRetryPolicy(policy);
    return *this;
}

QCRequest& QCRequest::withFollowRedirects(bool follow)
{
    m_request.setFollowLocation(follow);
    return *this;
}

// ========== 发送请求方法实现 ==========

QCNetworkReply* QCRequest::send(QCNetworkAccessManager *manager)
{
    if (!manager) {
        manager = defaultManager();
    }

    return sendInternal(manager);
}

QCNetworkReply* QCRequest::send(std::function<void(QCNetworkReply*)> callback)
{
    QCNetworkAccessManager *manager = defaultManager();
    QCNetworkReply *reply = sendInternal(manager);

    if (callback && reply) {
        QObject::connect(reply, &QCNetworkReply::finished, [reply, callback]() {
            callback(reply);
        });
    }

    return reply;
}

// ========== 内部辅助方法 ==========

QCNetworkAccessManager* QCRequest::defaultManager()
{
    // 使用函数级静态变量确保全局唯一实例
    static QCNetworkAccessManager *s_manager = new QCNetworkAccessManager();
    return s_manager;
}

QCNetworkReply* QCRequest::sendInternal(QCNetworkAccessManager *manager)
{
    QCNetworkReply *reply = nullptr;

    if (m_method == "GET") {
        reply = manager->sendGet(m_request);
    } else if (m_method == "POST") {
        reply = manager->sendPost(m_request, m_postData);
    } else if (m_method == "PUT") {
        reply = manager->sendPut(m_request, m_postData);
    } else if (m_method == "DELETE") {
        reply = manager->sendDelete(m_request);
    } else if (m_method == "PATCH") {
        reply = manager->sendPatch(m_request, m_postData);
    } else if (m_method == "HEAD") {
        reply = manager->sendHead(m_request);
    } else {
        qWarning() << "[QCRequest] Unsupported HTTP method:" << m_method;
        return nullptr;
    }

    if (reply) {
        // 自动调用 execute() 开始请求
        reply->execute();
    }

    return reply;
}

} // namespace QCurl
