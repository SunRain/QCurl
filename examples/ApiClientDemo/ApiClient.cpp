#include "ApiClient.h"

#include <QCNetworkHttpHeaders.h>
#include <QDebug>
#include <QJsonParseError>
#include <QUrlQuery>

#include <chrono>
#include <utility>

ApiClient::ApiClient(const QString &baseUrl, QObject *parent)
    : QObject(parent)
    , m_manager(new QCurl::QCNetworkAccessManager(this))
    , m_baseUrl(baseUrl)
    , m_timeout(30)
{
    // 设置默认 Content-Type
    m_defaultHeaders[QString::fromLatin1(QCurl::httpheaders::kContentType)] = QStringLiteral(
        "application/json");
}

ApiClient::~ApiClient() = default;

void ApiClient::setBaseUrl(const QString &url)
{
    m_baseUrl = url;
    if (m_baseUrl.endsWith('/')) {
        m_baseUrl.chop(1); // 移除末尾的斜杠
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
    m_defaultHeaders[QString::fromLatin1(QCurl::httpheaders::kContentType)] = QStringLiteral(
        "application/json");
}

void ApiClient::setBearerToken(const QString &token)
{
    m_bearerToken                                                             = token;
    m_defaultHeaders[QString::fromLatin1(QCurl::httpheaders::kAuthorization)] = QStringLiteral(
                                                                                    "Bearer ")
                                                                                + token;
}

void ApiClient::clearBearerToken()
{
    m_bearerToken.clear();
    m_defaultHeaders.remove(QString::fromLatin1(QCurl::httpheaders::kAuthorization));
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
    context.endpoint  = endpoint;
    context.onSuccess = onSuccess;
    context.onError   = onError;

    sendRequest(request, QCurl::HttpMethod::Get, context);
}

void ApiClient::post(const QString &endpoint,
                     const QJsonObject &data,
                     SuccessCallback onSuccess,
                     ErrorCallback onError)
{
    QString urlStr = m_baseUrl + "/" + endpoint;
    QUrl url(urlStr);
    QCurl::QCNetworkRequest request{url}; // 使用 {} 初始化
    request.setTimeout(std::chrono::seconds(m_timeout));

    // 设置请求头
    for (auto it = m_defaultHeaders.constBegin(); it != m_defaultHeaders.constEnd(); ++it) {
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }

    // 序列化 JSON
    QJsonDocument doc(data);
    QByteArray postData = doc.toJson(QJsonDocument::Compact);

    RequestContext context;
    context.endpoint  = endpoint;
    context.onSuccess = onSuccess;
    context.onError   = onError;

    sendRequest(request, QCurl::HttpMethod::Post, context, postData);
}

void ApiClient::put(const QString &endpoint,
                    const QJsonObject &data,
                    SuccessCallback onSuccess,
                    ErrorCallback onError)
{
    QString urlStr = m_baseUrl + "/" + endpoint;
    QUrl url(urlStr);
    QCurl::QCNetworkRequest request{url}; // 使用 {} 初始化
    request.setTimeout(std::chrono::seconds(m_timeout));

    // 设置请求头
    for (auto it = m_defaultHeaders.constBegin(); it != m_defaultHeaders.constEnd(); ++it) {
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }

    // 序列化 JSON
    QJsonDocument doc(data);
    QByteArray putData = doc.toJson(QJsonDocument::Compact);

    RequestContext context;
    context.endpoint  = endpoint;
    context.onSuccess = onSuccess;
    context.onError   = onError;

    // 保留既有 override 头；实际 HTTP 方法由 sendRequest 分发。
    request.setRawHeader("X-HTTP-Method-Override", "PUT");

    sendRequest(request, QCurl::HttpMethod::Put, context, putData);
}

void ApiClient::del(const QString &endpoint, SuccessCallback onSuccess, ErrorCallback onError)
{
    QString urlStr = m_baseUrl + "/" + endpoint;
    QUrl url(urlStr);
    QCurl::QCNetworkRequest request{url}; // 使用 {} 初始化
    request.setTimeout(std::chrono::seconds(m_timeout));

    // 设置请求头
    for (auto it = m_defaultHeaders.constBegin(); it != m_defaultHeaders.constEnd(); ++it) {
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }

    // 保留既有 override 头；实际 HTTP 方法由 sendRequest 分发。
    request.setRawHeader("X-HTTP-Method-Override", "DELETE");

    RequestContext context;
    context.endpoint  = endpoint;
    context.onSuccess = onSuccess;
    context.onError   = onError;

    sendRequest(request, QCurl::HttpMethod::Delete, context);
}

void ApiClient::cancelAll()
{
    // cancel() 会同步进入完成回调；先移出快照，避免重入修改遍历中的列表。
    const auto replies = std::exchange(m_activeRequests, {});
    for (const auto &reply : replies) {
        if (reply) {
            reply->cancel();
        }
    }
}

void ApiClient::sendRequest(QCurl::QCNetworkRequest &request,
                            QCurl::HttpMethod method,
                            const RequestContext &context,
                            const QByteArray &postData)
{
    const QPointer<ApiClient> observer(this);
    Q_EMIT requestStarted(context.endpoint);
    if (!observer) {
        return;
    }

    QCurl::QCNetworkReply *reply = nullptr;

    switch (method) {
        case QCurl::HttpMethod::Get:
            reply = m_manager->get(request);
            break;
        case QCurl::HttpMethod::Post:
            reply = m_manager->post(request, postData);
            break;
        case QCurl::HttpMethod::Put:
            reply = m_manager->put(request, postData);
            break;
        case QCurl::HttpMethod::Delete:
            reply = m_manager->deleteResource(request);
            break;
        default:
            reportError(context,
                        static_cast<int>(QCurl::NetworkError::InvalidRequest),
                        QStringLiteral("Unsupported HTTP method"));
            return;
    }

    if (!reply) {
        reportError(context, 0, QStringLiteral("Failed to create request"));
        return;
    }

    trackReply(reply, context);
}

void ApiClient::trackReply(QCurl::QCNetworkReply *reply, const RequestContext &context)
{
    // 保存上下文
    reply->setProperty("context_endpoint", QVariant(context.endpoint));

    // finished 同时覆盖成功、失败和取消，业务通知与清理只从这里进入。
    const QPointer<QCurl::QCNetworkReply> observer(reply);
    connect(reply, &QCurl::QCNetworkReply::finished, this, [this, observer, context]() {
        if (observer) {
            handleResponse(observer, context);
        }
    });

    m_activeRequests.append(reply);

    // 执行请求
    reply->execute();
}

void ApiClient::handleResponse(QCurl::QCNetworkReply *reply, RequestContext context)
{
    const QCurl::NetworkError error = reply->error();
    const QString message           = reply->errorString();
    const QByteArray data           = error == QCurl::NetworkError::NoError
                                          ? reply->readAll().value_or(QByteArray())
                                          : QByteArray();

    // 用户回调可以取消其他请求或销毁客户端，先结束当前请求的内部生命周期。
    m_activeRequests.removeOne(reply);
    reply->deleteLater();
    if (error != QCurl::NetworkError::NoError) {
        reportError(std::move(context), static_cast<int>(error), message);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        reportError(std::move(context),
                    0,
                    QStringLiteral("JSON parse error: %1").arg(parseError.errorString()));
        return;
    }

    const QPointer<ApiClient> observer(this);
    if (context.onSuccess) {
        context.onSuccess(doc);
    }
    if (observer) {
        Q_EMIT requestCompleted(context.endpoint, true);
    }
}

void ApiClient::reportError(RequestContext context, int errorCode, const QString &message)
{
    const QPointer<ApiClient> observer(this);
    qWarning() << "[ApiClient] 请求错误:" << context.endpoint << message;
    if (context.onError) {
        context.onError(errorCode, message);
    }
    if (observer) {
        Q_EMIT requestCompleted(context.endpoint, false);
    }
}
