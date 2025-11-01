#ifndef BATCHREQUEST_H
#define BATCHREQUEST_H

#include <QObject>
#include <QString>
#include <QUrl>
#include <QMap>
#include <QVariant>
#include <QCNetworkAccessManager.h>
#include <QCNetworkRequest.h>
#include <QCNetworkReply.h>

/**
 * @brief 批量请求管理器
 *
 * 功能特性:
 * - 批量 HTTP 请求管理（GET/POST/HEAD）
 * - 请求优先级控制（高/中/低）
 * - 并发数限制
 * - 自动重试机制
 * - 进度统计和回调
 */
class BatchRequest : public QObject
{
    Q_OBJECT

public:
    enum class Priority {
        High = 0,    ///< 高优先级
        Normal = 1,  ///< 普通优先级
        Low = 2      ///< 低优先级
    };

    enum class RequestMethod {
        GET,
        POST,
        HEAD
    };

    struct RequestInfo {
        QString id;                 ///< 请求唯一标识
        QUrl url;                   ///< 请求 URL
        RequestMethod method;       ///< 请求方法
        Priority priority;          ///< 优先级
        QByteArray postData;        ///< POST 数据（可选）
        int retryCount;             ///< 重试次数
        int maxRetries;             ///< 最大重试次数
        QString errorString;        ///< 错误信息
        QByteArray responseData;    ///< 响应数据
        bool completed;             ///< 是否完成
        bool success;               ///< 是否成功
    };

    explicit BatchRequest(QObject *parent = nullptr);
    ~BatchRequest();

    // 配置
    void setMaxConcurrent(int max);  ///< 设置最大并发数（默认5）
    int maxConcurrent() const { return m_maxConcurrent; }

    void setMaxRetries(int retries);  ///< 设置最大重试次数（默认3）
    int maxRetries() const { return m_maxRetries; }

    void setTimeout(int seconds);  ///< 设置超时时间（默认30秒）
    int timeout() const { return m_timeout; }

    // 添加请求
    QString addRequest(const QUrl &url,
                      RequestMethod method = RequestMethod::GET,
                      Priority priority = Priority::Normal,
                      const QByteArray &postData = QByteArray());

    // 批量操作
    void start();           ///< 开始执行所有请求
    void pause();           ///< 暂停执行
    void resume();          ///< 恢复执行
    void cancel();          ///< 取消所有请求
    void clear();           ///< 清除所有请求

    // 统计信息
    int totalCount() const { return m_requests.size(); }
    int completedCount() const;
    int successCount() const;
    int failedCount() const;
    int runningCount() const { return m_runningRequests.size(); }
    int pendingCount() const { return m_pendingRequests.size(); }

    // 获取请求信息
    RequestInfo getRequestInfo(const QString &id) const;
    QList<RequestInfo> allRequests() const { return m_requests; }

signals:
    void started();
    void paused();
    void resumed();
    void cancelled();

    void requestStarted(const QString &id);
    void requestCompleted(const QString &id, bool success);
    void requestProgress(int completed, int total, double percentage);

    void allCompleted();
    void error(const QString &id, const QString &errorString);

private slots:
    void onRequestFinished();
    void onRequestError(QCurl::NetworkError errorCode);

private:
    void processQueue();
    void startRequest(const QString &id);
    void retryRequest(const QString &id);
    QString generateRequestId();

private:
    QCurl::QCNetworkAccessManager *m_manager;

    QList<RequestInfo> m_requests;              ///< 所有请求
    QList<QString> m_pendingRequests;           ///< 待处理队列（按优先级排序）
    QMap<QString, QCurl::QCNetworkReply*> m_runningRequests;  ///< 运行中的请求

    int m_maxConcurrent;    ///< 最大并发数
    int m_maxRetries;       ///< 最大重试次数
    int m_timeout;          ///< 超时时间（秒）
    bool m_paused;          ///< 是否暂停
    int m_requestCounter;   ///< 请求计数器（用于生成ID）
};

#endif // BATCHREQUEST_H
