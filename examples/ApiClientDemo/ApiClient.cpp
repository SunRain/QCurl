#include "ApiClient.h"
#include <QDebug>
#include <QUrlQuery>
#include <QJsonParseError>
#include <chrono>

ApiClient::ApiClient(const QString &baseUrl, QObject *parent)
    : QObject(parent)
    , m_manager(new QCurl::QCNetworkAccessManager(this))
    , m_baseUrl(baseUrl)
    , m_timeout(30)
{
    // 设置默认 Content-Type
    m_defaultHeaders["Content-Type"] = "application/json";
}

ApiClient::~ApiClient()
{
    cancelAll();
}

void ApiClient::setBaseUrl(const QString &url)
{
    m_baseUrl = url;
    if (m_baseUrl.endsWith('/')) {
        m_baseUrl.chop(1);  // 移除末尾的斜杠
    }
}

void ApiClient::setTimeout(int seconds)
{
    if (seconds > 0 && seconds <= 300) {
        m_timeout = seconds;
    }
}

void ApiClient::setDefaultHeader(const QString &key, const QString &value)
{
    m_defaultHeaders[key] = value;
}

void ApiClient::removeDefaultHeader(const QString &key)
{
    m_defaultHeaders.remove(key);
}

void ApiClient::clearDefaultHeaders()
{
    m_defaultHeaders.clear();
    // 重新设置默认 Content-Type
    m_defaultHeaders["Content-Type"] = "application/json";
}

void ApiClient::setBearerToken(const QString &token)
{
    m_bearerToken = token;
    m_defaultHeaders["Authorization"] = "Bearer " + token;
}

void ApiClient::clearBearerToken()
{
    m_bearerToken.clear();
    m_defaultHeaders.remove("Authorization");
}

void ApiClient::get(const QString &endpoint,
                   SuccessCallback onSuccess,
                   ErrorCallback onError,
                   const QMap<QString, QString> &queryParams)
{
    // 构建 URL
    QString urlStr = m_baseUrl + "/" + endpoint;
    QUrl url(urlStr);

    // 添加查询参数
    if (!queryParams.isEmpty()) {
        QUrlQuery query;
        for (auto it = queryParams.constBegin(); it != queryParams.constEnd(); ++it) {
            query.addQueryItem(it.key(), it.value());
        }
        url.setQuery(query);
    }

    QCurl::QCNetworkRequest request(url);
    request.setTimeout(std::chrono::seconds(m_timeout));

    // 设置请求头
    for (auto it = m_defaultHeaders.constBegin(); it != m_defaultHeaders.constEnd(); ++it) {
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }

    RequestContext context;
    context.endpoint = endpoint;
    context.onSuccess = onSuccess;
    context.onError = onError;

    sendRequest(request, "GET", context);
}

void ApiClient::post(const QString &endpoint,
                    const QJsonObject &data,
                    SuccessCallback onSuccess,
                    ErrorCallback onError)
{
    QString urlStr = m_baseUrl + "/" + endpoint;
    QUrl url(urlStr);
    QCurl::QCNetworkRequest request{url};  // 使用 {} 初始化
    request.setTimeout(std::chrono::seconds(m_timeout));

    // 设置请求头
    for (auto it = m_defaultHeaders.constBegin(); it != m_defaultHeaders.constEnd(); ++it) {
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }

    // 序列化 JSON
    QJsonDocument doc(data);
    QByteArray postData = doc.toJson(QJsonDocument::Compact);

    RequestContext context;
    context.endpoint = endpoint;
    context.onSuccess = onSuccess;
    context.onError = onError;

    sendRequest(request, "POST", context, postData);
}

void ApiClient::put(const QString &endpoint,
                   const QJsonObject &data,
                   SuccessCallback onSuccess,
                   ErrorCallback onError)
{
    QString urlStr = m_baseUrl + "/" + endpoint;
    QUrl url(urlStr);
    QCurl::QCNetworkRequest request{url};  // 使用 {} 初始化
    request.setTimeout(std::chrono::seconds(m_timeout));

    // 设置请求头
    for (auto it = m_defaultHeaders.constBegin(); it != m_defaultHeaders.constEnd(); ++it) {
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }

    // 序列化 JSON
    QJsonDocument doc(data);
    QByteArray putData = doc.toJson(QJsonDocument::Compact);

    RequestContext context;
    context.endpoint = endpoint;
    context.onSuccess = onSuccess;
    context.onError = onError;

    // 设置 PUT 方法
    request.setRawHeader("X-HTTP-Method-Override", "PUT");

    sendRequest(request, "PUT", context, putData);
}

void ApiClient::del(const QString &endpoint,
                   SuccessCallback onSuccess,
                   ErrorCallback onError)
{
    QString urlStr = m_baseUrl + "/" + endpoint;
    QUrl url(urlStr);
    QCurl::QCNetworkRequest request{url};  // 使用 {} 初始化
    request.setTimeout(std::chrono::seconds(m_timeout));

    // 设置请求头
    for (auto it = m_defaultHeaders.constBegin(); it != m_defaultHeaders.constEnd(); ++it) {
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }

    // 设置 DELETE 方法
    request.setRawHeader("X-HTTP-Method-Override", "DELETE");

    RequestContext context;
    context.endpoint = endpoint;
    context.onSuccess = onSuccess;
    context.onError = onError;

    sendRequest(request, "DELETE", context);
}

void ApiClient::cancelAll()
{
    for (QCurl::QCNetworkReply *reply : m_activeRequests) {
        if (reply) {
            reply->cancel();
            reply->deleteLater();
        }
    }
    m_activeRequests.clear();
}

void ApiClient::sendRequest(QCurl::QCNetworkRequest &request,
                           const QString &method,
                           const RequestContext &context,
                           const QByteArray &postData)
{
    emit requestStarted(context.endpoint);

    QCurl::QCNetworkReply *reply = nullptr;

    if (method == "GET") {
        reply = m_manager->sendGet(request);
    } else if (method == "POST") {
        reply = m_manager->sendPost(request, postData);
    } else if (method == "PUT") {
        // QCurl 可能没有直接的 PUT 方法，使用 POST 模拟
        reply = m_manager->sendPost(request, postData);
    } else if (method == "DELETE") {
        // QCurl 可能没有直接的 DELETE 方法，使用 HEAD 或自定义
        reply = m_manager->sendHead(request);
    }

    if (!reply) {
        qWarning() << "[ApiClient] 创建请求失败:" << context.endpoint;
        if (context.onError) {
            context.onError(0, "Failed to create request");
        }
        return;
    }

    // 保存上下文
    reply->setProperty("context_endpoint", QVariant(context.endpoint));

    // 连接信号
    connect(reply, &QCurl::QCNetworkReply::finished,
            this, [this, reply, context]() {
        handleResponse(reply, context);
    });

    connect(reply, qOverload<QCurl::NetworkError>(&QCurl::QCNetworkReply::error),
            this, [this, reply, context](QCurl::NetworkError errorCode) {
        QString errorMsg = reply->errorString();
        qWarning() << "[ApiClient] 请求错误:" << context.endpoint << errorMsg;

        if (context.onError) {
            context.onError(static_cast<int>(errorCode), errorMsg);
        }

        m_activeRequests.removeOne(reply);
        reply->deleteLater();

        emit requestCompleted(context.endpoint, false);
    });

    m_activeRequests.append(reply);

    // 执行请求
    reply->execute();
}

void ApiClient::handleResponse(QCurl::QCNetworkReply *reply,
                               const RequestContext &context)
{
    // 读取响应数据
    auto dataOpt = reply->readAll();
    QByteArray data;

    if (dataOpt.has_value()) {
        data = dataOpt.value();
    }

    // 解析 JSON
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "[ApiClient] JSON 解析错误:" << parseError.errorString();
        if (context.onError) {
            context.onError(0, "JSON parse error: " + parseError.errorString());
        }
    } else {
        if (context.onSuccess) {
            context.onSuccess(doc);
        }
    }

    m_activeRequests.removeOne(reply);
    reply->deleteLater();

    emit requestCompleted(context.endpoint, true);
}
