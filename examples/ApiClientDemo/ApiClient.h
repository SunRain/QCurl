#ifndef APICLIENT_H
#define APICLIENT_H

#include <QCNetworkAccessManager.h>
#include <QCNetworkHttpMethod.h>
#include <QCNetworkReply.h>
#include <QCNetworkRequest.h>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QVariant>

#include <functional>

/**
 * @brief RESTful API 客户端封装
 *
 * 功能特性:
 * - 简洁的 REST API 调用接口（GET/POST/PUT/DELETE）
 * - 自动 JSON 序列化/反序列化
 * - 请求头管理（Authorization、Content-Type 等）
 * - 错误处理和回调
 * - 超时配置
 *
 * 每个请求最多调用一次结果回调；请求失败或取消时不再解析 JSON。
 * 无请求错误且 JSON 解析成功才视为成功；空响应仍按 JSON 解析失败处理。
 * 结果回调之后，客户端仍存活时发射一次 requestCompleted()。
 * 销毁客户端会停止未完成请求，不再调用业务回调或发射完成信号。
 */
class ApiClient : public QObject
{
    Q_OBJECT

public:
    using SuccessCallback = std::function<void(const QJsonDocument &response)>;
    /// 请求错误码为 QCurl::NetworkError 的整数值；JSON 解析错误码为 0。
    using ErrorCallback = std::function<void(int errorCode, const QString &errorMessage)>;

    explicit ApiClient(const QString &baseUrl, QObject *parent = nullptr);
    ~ApiClient() override;

    // 配置
    void setBaseUrl(const QString &url);
    QString baseUrl() const { return m_baseUrl; }

    void setTimeout(int seconds); ///< 设置超时时间（默认30秒）
    int timeout() const { return m_timeout; }

    void setDefaultHeader(const QString &key, const QString &value); ///< 设置默认请求头
    void removeDefaultHeader(const QString &key);
    void clearDefaultHeaders();

    void setBearerToken(const QString &token); ///< 设置 Bearer Token
    void clearBearerToken();

    // REST API 方法
    void get(const QString &endpoint,
             SuccessCallback onSuccess,
             ErrorCallback onError,
             const QMap<QString, QString> &queryParams = QMap<QString, QString>());

    void post(const QString &endpoint,
              const QJsonObject &data,
              SuccessCallback onSuccess,
              ErrorCallback onError);

    void put(const QString &endpoint,
             const QJsonObject &data,
             SuccessCallback onSuccess,
             ErrorCallback onError);

    void del(const QString &endpoint, SuccessCallback onSuccess, ErrorCallback onError);

    /// 取消调用时尚未完成的请求；取消回调中新建的请求不属于本次取消范围。
    void cancelAll();

Q_SIGNALS:
    void requestStarted(const QString &endpoint);
    /// 结果回调结束后发射；失败、取消和 JSON 解析失败的 success 均为 false。
    void requestCompleted(const QString &endpoint, bool success);

private:
    Q_DISABLE_COPY_MOVE(ApiClient)
    friend class TestApiClient;

    struct RequestContext
    {
        QString endpoint;
        SuccessCallback onSuccess;
        ErrorCallback onError;
    };

    void sendRequest(QCurl::QCNetworkRequest &request,
                     QCurl::HttpMethod method,
                     const RequestContext &context,
                     const QByteArray &postData = QByteArray());

    void trackReply(QCurl::QCNetworkReply *reply, const RequestContext &context);
    void handleResponse(QCurl::QCNetworkReply *reply, RequestContext context);
    void reportError(RequestContext context, int errorCode, const QString &message);

private:
    QCurl::QCNetworkAccessManager *m_manager;

    QString m_baseUrl;
    int m_timeout;
    QMap<QString, QString> m_defaultHeaders;
    QString m_bearerToken;

    QList<QPointer<QCurl::QCNetworkReply>> m_activeRequests;
};

#endif // APICLIENT_H
