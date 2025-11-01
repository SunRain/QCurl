#include "BatchRequest.h"
#include <QDebug>
#include <QTimer>
#include <chrono>

BatchRequest::BatchRequest(QObject *parent)
    : QObject(parent)
    , m_manager(new QCurl::QCNetworkAccessManager(this))
    , m_maxConcurrent(5)
    , m_maxRetries(3)
    , m_timeout(30)
    , m_paused(false)
    , m_requestCounter(0)
{
}

BatchRequest::~BatchRequest()
{
    cancel();
}

void BatchRequest::setMaxConcurrent(int max)
{
    if (max > 0 && max <= 20) {
        m_maxConcurrent = max;
    }
}

void BatchRequest::setMaxRetries(int retries)
{
    if (retries >= 0 && retries <= 10) {
        m_maxRetries = retries;
    }
}

void BatchRequest::setTimeout(int seconds)
{
    if (seconds > 0 && seconds <= 300) {
        m_timeout = seconds;
    }
}

QString BatchRequest::addRequest(const QUrl &url,
                                 RequestMethod method,
                                 Priority priority,
                                 const QByteArray &postData)
{
    QString id = generateRequestId();

    RequestInfo info;
    info.id = id;
    info.url = url;
    info.method = method;
    info.priority = priority;
    info.postData = postData;
    info.retryCount = 0;
    info.maxRetries = m_maxRetries;
    info.completed = false;
    info.success = false;

    m_requests.append(info);

    // 插入到待处理队列，按优先级排序
    int insertPos = 0;
    for (int i = 0; i < m_pendingRequests.size(); ++i) {
        const RequestInfo &existing = getRequestInfo(m_pendingRequests[i]);
        if (static_cast<int>(priority) < static_cast<int>(existing.priority)) {
            insertPos = i;
            break;
        }
        insertPos = i + 1;
    }
    m_pendingRequests.insert(insertPos, id);

    return id;
}

void BatchRequest::start()
{
    if (m_pendingRequests.isEmpty() && m_runningRequests.isEmpty()) {
        qDebug() << "[BatchRequest] 没有待处理的请求";
        return;
    }

    m_paused = false;
    emit started();

    processQueue();
}

void BatchRequest::pause()
{
    m_paused = true;
    emit paused();
}

void BatchRequest::resume()
{
    if (!m_paused) {
        return;
    }

    m_paused = false;
    emit resumed();

    processQueue();
}

void BatchRequest::cancel()
{
    // 取消所有运行中的请求
    for (auto it = m_runningRequests.begin(); it != m_runningRequests.end(); ++it) {
        QCurl::QCNetworkReply *reply = it.value();
        if (reply) {
            reply->cancel();
            reply->deleteLater();
        }
    }

    m_runningRequests.clear();
    m_pendingRequests.clear();

    emit cancelled();
}

void BatchRequest::clear()
{
    cancel();
    m_requests.clear();
    m_requestCounter = 0;
}

int BatchRequest::completedCount() const
{
    int count = 0;
    for (const RequestInfo &info : m_requests) {
        if (info.completed) {
            ++count;
        }
    }
    return count;
}

int BatchRequest::successCount() const
{
    int count = 0;
    for (const RequestInfo &info : m_requests) {
        if (info.completed && info.success) {
            ++count;
        }
    }
    return count;
}

int BatchRequest::failedCount() const
{
    int count = 0;
    for (const RequestInfo &info : m_requests) {
        if (info.completed && !info.success) {
            ++count;
        }
    }
    return count;
}

BatchRequest::RequestInfo BatchRequest::getRequestInfo(const QString &id) const
{
    for (const RequestInfo &info : m_requests) {
        if (info.id == id) {
            return info;
        }
    }
    return RequestInfo();
}

void BatchRequest::processQueue()
{
    if (m_paused) {
        return;
    }

    // 启动待处理的请求，直到达到并发上限
    while (m_runningRequests.size() < m_maxConcurrent && !m_pendingRequests.isEmpty()) {
        QString id = m_pendingRequests.takeFirst();
        startRequest(id);
    }

    // 检查是否全部完成
    if (m_runningRequests.isEmpty() && m_pendingRequests.isEmpty()) {
        emit allCompleted();
    }
}

void BatchRequest::startRequest(const QString &id)
{
    // 查找请求信息
    RequestInfo *info = nullptr;
    for (int i = 0; i < m_requests.size(); ++i) {
        if (m_requests[i].id == id) {
            info = &m_requests[i];
            break;
        }
    }

    if (!info) {
        qWarning() << "[BatchRequest] 请求不存在:" << id;
        return;
    }

    // 创建请求
    QCurl::QCNetworkRequest request(info->url);
    request.setTimeout(std::chrono::seconds(m_timeout));

    // 发起请求
    QCurl::QCNetworkReply *reply = nullptr;

    switch (info->method) {
    case RequestMethod::GET:
        reply = m_manager->sendGet(request);
        break;
    case RequestMethod::POST:
        reply = m_manager->sendPost(request, info->postData);
        break;
    case RequestMethod::HEAD:
        reply = m_manager->sendHead(request);
        break;
    }

    if (!reply) {
        qWarning() << "[BatchRequest] 创建请求失败:" << id;
        return;
    }

    // 连接信号
    connect(reply, &QCurl::QCNetworkReply::finished,
            this, &BatchRequest::onRequestFinished);
    connect(reply, qOverload<QCurl::NetworkError>(&QCurl::QCNetworkReply::error),
            this, &BatchRequest::onRequestError);

    // 保存请求映射
    reply->setProperty("requestId", QVariant(id));
    m_runningRequests[id] = reply;

    // 执行请求
    reply->execute();

    emit requestStarted(id);

    // 更新进度
    emit requestProgress(completedCount(), totalCount(),
                        (totalCount() > 0) ? (completedCount() * 100.0 / totalCount()) : 0.0);
}

void BatchRequest::retryRequest(const QString &id)
{
    // 查找请求信息
    RequestInfo *info = nullptr;
    for (int i = 0; i < m_requests.size(); ++i) {
        if (m_requests[i].id == id) {
            info = &m_requests[i];
            break;
        }
    }

    if (!info) {
        return;
    }

    if (info->retryCount < info->maxRetries) {
        info->retryCount++;
        qDebug() << "[BatchRequest] 重试请求" << id << "(" << info->retryCount << "/" << info->maxRetries << ")";

        // 重新加入队列（保持原优先级）
        m_pendingRequests.prepend(id);
        processQueue();
    } else {
        qWarning() << "[BatchRequest] 请求失败，超过最大重试次数:" << id;
        info->completed = true;
        info->success = false;

        emit requestCompleted(id, false);
        emit requestProgress(completedCount(), totalCount(),
                            (totalCount() > 0) ? (completedCount() * 100.0 / totalCount()) : 0.0);

        processQueue();
    }
}

QString BatchRequest::generateRequestId()
{
    return QString("req_%1").arg(++m_requestCounter);
}

void BatchRequest::onRequestFinished()
{
    auto *reply = qobject_cast<QCurl::QCNetworkReply*>(sender());
    if (!reply) {
        return;
    }

    QString id = reply->property("requestId").toString();

    // 查找请求信息
    RequestInfo *info = nullptr;
    for (int i = 0; i < m_requests.size(); ++i) {
        if (m_requests[i].id == id) {
            info = &m_requests[i];
            break;
        }
    }

    if (info) {
        // 读取响应数据
        auto dataOpt = reply->readAll();
        if (dataOpt.has_value()) {
            info->responseData = dataOpt.value();
        }

        info->completed = true;
        info->success = true;

        emit requestCompleted(id, true);
    }

    // 从运行列表中移除
    m_runningRequests.remove(id);
    reply->deleteLater();

    // 更新进度
    emit requestProgress(completedCount(), totalCount(),
                        (totalCount() > 0) ? (completedCount() * 100.0 / totalCount()) : 0.0);

    // 处理下一个请求
    processQueue();
}

void BatchRequest::onRequestError(QCurl::NetworkError errorCode)
{
    auto *reply = qobject_cast<QCurl::QCNetworkReply*>(sender());
    if (!reply) {
        return;
    }

    QString id = reply->property("requestId").toString();
    QString errorString = reply->errorString();

    qWarning() << "[BatchRequest] 请求失败:" << id << errorString << "(" << static_cast<int>(errorCode) << ")";

    // 查找请求信息
    RequestInfo *info = nullptr;
    for (int i = 0; i < m_requests.size(); ++i) {
        if (m_requests[i].id == id) {
            info = &m_requests[i];
            break;
        }
    }

    if (info) {
        info->errorString = errorString;
    }

    // 从运行列表中移除
    m_runningRequests.remove(id);
    reply->deleteLater();

    emit error(id, errorString);

    // 尝试重试
    retryRequest(id);
}
