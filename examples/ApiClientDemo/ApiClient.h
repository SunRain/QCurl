#ifndef APICLIENT_H
#define APICLIENT_H

#include <QObject>
#include <QString>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QVariant>
#include <functional>
#include <QCNetworkAccessManager.h>
#include <QCNetworkRequest.h>
#include <QCNetworkReply.h>

/**
 * @brief RESTful API 客户端封装
 *
 * 功能特性:
 * - 简洁的 REST API 调用接口（GET/POST/PUT/DELETE）
 * - 自动 JSON 序列化/反序列化
 * - 请求头管理（Authorization、Content-Type 等）
 * - 错误处理和回调
 * - 超时配置
 */
class ApiClient : public QObject
{
    Q_OBJECT

public:
    using SuccessCallback = std::function<void(const QJsonDocument &response)>;
    using ErrorCallback = std::function<void(int httpCode, const QString &errorMessage)>;

    explicit ApiClient(const QString &baseUrl, QObject *parent = nullptr);
    ~ApiClient();

    // 配置
    void setBaseUrl(const QString &url);
    QString baseUrl() const { return m_baseUrl; }

    void setTimeout(int seconds);  ///< 设置超时时间（默认30秒）
    int timeout() const { return m_timeout; }

    void setDefaultHeader(const QString &key, const QString &value);  ///< 设置默认请求头
    void removeDefaultHeader(const QString &key);
    void clearDefaultHeaders();

    void setBearerToken(const QString &token);  ///< 设置 Bearer Token
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

    void del(const QString &endpoint,
             SuccessCallback onSuccess,
             ErrorCallback onError);

    // 取消所有请求
    void cancelAll();

signals:
    void requestStarted(const QString &endpoint);
    void requestCompleted(const QString &endpoint, bool success);

private:
    struct RequestContext {
        QString endpoint;
        SuccessCallback onSuccess;
        ErrorCallback onError;
    };

    void sendRequest(QCurl::QCNetworkRequest &request,
                    const QString &method,
                    const RequestContext &context,
                    const QByteArray &postData = QByteArray());

    void handleResponse(QCurl::QCNetworkReply *reply,
                       const RequestContext &context);

private:
    QCurl::QCNetworkAccessManager *m_manager;

    QString m_baseUrl;
    int m_timeout;
    QMap<QString, QString> m_defaultHeaders;
    QString m_bearerToken;

    QList<QCurl::QCNetworkReply*> m_activeRequests;
};

#endif // APICLIENT_H
