#include "QCNetworkAccessManager.h"

#include <curl/multi.h>
#include <QSet>
#include <QTimer>
#include <QDebug>
#include <QSocketNotifier>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QUrl>
#include <QFile>
#include <QIODevice>
#include <QSharedPointer>
#include <QDir>

#include "QCMultipartFormData.h"
#include "CurlGlobalConstructor.h"
#include "QCNetworkAccessManager_p.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "QCUtility.h"
#include "QCNetworkRequestScheduler.h"
#include "QCNetworkCache.h"
#include "QCNetworkDiskCache.h"
#include "QCNetworkLogger.h"
#include "QCNetworkMiddleware.h"
#include "QCNetworkRequestBuilder.h"
#include "QCNetworkMockHandler.h"

namespace QCurl {

QCNetworkAccessManager::QCNetworkAccessManager(QObject *parent)
    : QObject(parent),
      d_ptr(new QCNetworkAccessManagerPrivate(this)),
      m_cookieModeFlag(NotOpen),
      m_schedulerEnabled(false),
      m_cache(nullptr)
{
    CurlGlobalConstructor::instance();
}

QCNetworkAccessManager::~QCNetworkAccessManager()
{
    delete d_ptr;
}

QString QCNetworkAccessManager::cookieFilePath() const
{
    return m_cookieFilePath;
}

QCNetworkAccessManager::CookieFileModeFlag QCNetworkAccessManager::cookieFileMode() const
{
    return m_cookieModeFlag;
}

void QCNetworkAccessManager::setCookieFilePath(const QString &cookieFilePath, CookieFileModeFlag flag)
{
    m_cookieFilePath = cookieFilePath;
    m_cookieModeFlag = flag;
}

// ============================================================================
// 核心 API 实现
// ============================================================================

QCNetworkReply* QCNetworkAccessManager::sendHead(const QCNetworkRequest &request)
{
    auto *reply = new QCNetworkReply(request,
                                      HttpMethod::Head,
                                      ExecutionMode::Async,
                                      QByteArray(),
                                      this);

    // Cookie 配置传递
    if (m_cookieModeFlag != NotOpen && !m_cookieFilePath.isEmpty()) {
        reply->d_func()->cookieFilePath = m_cookieFilePath;
        reply->d_func()->cookieMode = m_cookieModeFlag;
    }

    reply->execute();  // 自动启动请求
    return reply;
}

QCNetworkReply* QCNetworkAccessManager::sendGet(const QCNetworkRequest &request)
{
    auto *reply = new QCNetworkReply(request,
                                      HttpMethod::Get,
                                      ExecutionMode::Async,
                                      QByteArray(),
                                      this);

    // Cookie 配置传递
    if (m_cookieModeFlag != NotOpen && !m_cookieFilePath.isEmpty()) {
        reply->d_func()->cookieFilePath = m_cookieFilePath;
        reply->d_func()->cookieMode = m_cookieModeFlag;
    }

    reply->execute();  // 自动启动请求
    return reply;
}

QCNetworkReply* QCNetworkAccessManager::sendPost(const QCNetworkRequest &request, const QByteArray &data)
{
    auto *reply = new QCNetworkReply(request,
                                      HttpMethod::Post,
                                      ExecutionMode::Async,
                                      data,
                                      this);

    // Cookie 配置传递
    if (m_cookieModeFlag != NotOpen && !m_cookieFilePath.isEmpty()) {
        reply->d_func()->cookieFilePath = m_cookieFilePath;
        reply->d_func()->cookieMode = m_cookieModeFlag;
    }

    reply->execute();  // 自动启动请求
    return reply;
}

QCNetworkReply* QCNetworkAccessManager::sendPut(const QCNetworkRequest &request, const QByteArray &data)
{
    auto *reply = new QCNetworkReply(request,
                                      HttpMethod::Put,
                                      ExecutionMode::Async,
                                      data,
                                      this);

    // Cookie 配置传递
    if (m_cookieModeFlag != NotOpen && !m_cookieFilePath.isEmpty()) {
        reply->d_func()->cookieFilePath = m_cookieFilePath;
        reply->d_func()->cookieMode = m_cookieModeFlag;
    }

    reply->execute();  // 自动启动请求
    return reply;
}

QCNetworkReply* QCNetworkAccessManager::sendDelete(const QCNetworkRequest &request)
{
    return sendDelete(request, QByteArray());
}

QCNetworkReply* QCNetworkAccessManager::sendDelete(const QCNetworkRequest &request, const QByteArray &data)
{
    auto *reply = new QCNetworkReply(request,
                                      HttpMethod::Delete,
                                      ExecutionMode::Async,
                                      data,
                                      this);

    // Cookie 配置传递
    if (m_cookieModeFlag != NotOpen && !m_cookieFilePath.isEmpty()) {
        reply->d_func()->cookieFilePath = m_cookieFilePath;
        reply->d_func()->cookieMode = m_cookieModeFlag;
    }

    reply->execute();  // 自动启动请求
    return reply;
}

QCNetworkReply* QCNetworkAccessManager::sendPatch(const QCNetworkRequest &request, const QByteArray &data)
{
    auto *reply = new QCNetworkReply(request,
                                      HttpMethod::Patch,
                                      ExecutionMode::Async,
                                      data,
                                      this);

    // Cookie 配置传递
    if (m_cookieModeFlag != NotOpen && !m_cookieFilePath.isEmpty()) {
        reply->d_func()->cookieFilePath = m_cookieFilePath;
        reply->d_func()->cookieMode = m_cookieModeFlag;
    }

    reply->execute();  // 自动启动请求
    return reply;
}

QCNetworkReply* QCNetworkAccessManager::sendGetSync(const QCNetworkRequest &request)
{
    auto *reply = new QCNetworkReply(request,
                                      HttpMethod::Get,
                                      ExecutionMode::Sync,
                                      QByteArray(),
                                      this);

    // Cookie 配置传递
    if (m_cookieModeFlag != NotOpen && !m_cookieFilePath.isEmpty()) {
        reply->d_func()->cookieFilePath = m_cookieFilePath;
        reply->d_func()->cookieMode = m_cookieModeFlag;
    }

    reply->execute();  // 同步执行（会阻塞）
    return reply;
}

QCNetworkReply* QCNetworkAccessManager::sendPostSync(const QCNetworkRequest &request, const QByteArray &data)
{
    auto *reply = new QCNetworkReply(request,
                                      HttpMethod::Post,
                                      ExecutionMode::Sync,
                                      data,
                                      this);

    // Cookie 配置传递
    if (m_cookieModeFlag != NotOpen && !m_cookieFilePath.isEmpty()) {
        reply->d_func()->cookieFilePath = m_cookieFilePath;
        reply->d_func()->cookieMode = m_cookieModeFlag;
    }

    reply->execute();  // 同步执行（会阻塞）
    return reply;
}

// ============================================================================
// 请求优先级调度
// ============================================================================

void QCNetworkAccessManager::enableRequestScheduler(bool enable)
{
    m_schedulerEnabled = enable;
}

bool QCNetworkAccessManager::isSchedulerEnabled() const
{
    return m_schedulerEnabled;
}

QCNetworkRequestScheduler* QCNetworkAccessManager::scheduler() const
{
    return QCNetworkRequestScheduler::instance();
}

QCNetworkReply* QCNetworkAccessManager::scheduleGet(const QCNetworkRequest &request)
{
    if (m_schedulerEnabled) {
        return scheduler()->scheduleRequest(
            request,
            HttpMethod::Get,
            request.priority()
        );
    } else {
        // 回退到直接执行
        return sendGet(request);
    }
}

QCNetworkReply* QCNetworkAccessManager::schedulePost(const QCNetworkRequest &request, const QByteArray &data)
{
    if (m_schedulerEnabled) {
        return scheduler()->scheduleRequest(
            request,
            HttpMethod::Post,
            request.priority(),
            data
        );
    } else {
        // 回退到直接执行
        return sendPost(request, data);
    }
}

QCNetworkReply* QCNetworkAccessManager::schedulePut(const QCNetworkRequest &request, const QByteArray &data)
{
    if (m_schedulerEnabled) {
        return scheduler()->scheduleRequest(
            request,
            HttpMethod::Put,
            request.priority(),
            data
        );
    } else {
        // 回退到直接执行
        return sendPut(request, data);
    }
}

// ============================================================================
// JSON 快捷方法实现
// ============================================================================

QCNetworkReply* QCNetworkAccessManager::postJson(const QUrl &url, const QJsonObject &json)
{
    // 序列化 JSON 对象
    QJsonDocument doc(json);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

    // 创建请求并设置 Content-Type
    QCNetworkRequest request(url);
    request.setRawHeader("Content-Type", "application/json");

    // 发送 POST 请求
    return sendPost(request, jsonData);
}

QCNetworkReply* QCNetworkAccessManager::postForm(const QUrl &url, const QMap<QString, QString> &formData)
{
    // URL编码表单数据
    QStringList pairs;
    for (auto it = formData.constBegin(); it != formData.constEnd(); ++it) {
        QString key = QUrl::toPercentEncoding(it.key());
        QString value = QUrl::toPercentEncoding(it.value());
        pairs.append(QString("%1=%2").arg(key, value));
    }
    QByteArray encodedData = pairs.join("&").toUtf8();

    // 创建请求并设置 Content-Type
    QCNetworkRequest request(url);
    request.setRawHeader("Content-Type", "application/x-www-form-urlencoded");

    // 发送 POST 请求
    return sendPost(request, encodedData);
}

// ========================================================================
// Multipart/form-data 支持
// ========================================================================

QCNetworkReply* QCNetworkAccessManager::postMultipart(const QUrl &url, const QCMultipartFormData &formData)
{
    // 编码 Multipart 表单数据
    QByteArray multipartData = formData.toByteArray();

    // 创建请求并设置 Content-Type
    QCNetworkRequest request(url);
    request.setRawHeader("Content-Type", formData.contentType().toUtf8());

    // 发送 POST 请求
    return sendPost(request, multipartData);
}

QCNetworkReply* QCNetworkAccessManager::postMultipart(const QCNetworkRequest &request,
                                                       const QCMultipartFormData &formData)
{
    // 创建请求副本并设置 Content-Type
    QCNetworkRequest modifiedRequest = request;
    modifiedRequest.setRawHeader("Content-Type", formData.contentType().toUtf8());

    // 编码 Multipart 表单数据
    QByteArray multipartData = formData.toByteArray();

    // 发送 POST 请求
    return sendPost(modifiedRequest, multipartData);
}

// ========================================================================
// 文件操作便捷 API
// ========================================================================

QCNetworkReply* QCNetworkAccessManager::downloadFile(const QUrl &url, const QString &savePath)
{
    // 创建请求
    QCNetworkRequest request(url);

    // 发送 GET 请求
    QCNetworkReply *reply = sendGet(request);

    // 连接信号以保存文件
    QObject::connect(reply, &QCNetworkReply::finished, reply, [reply, savePath]() {
        if (reply->error() == NetworkError::NoError) {
            // 读取响应数据
            auto responseData = reply->readAll();
            if (responseData.has_value()) {
                // 保存到文件
                QFile file(savePath);
                if (file.open(QIODevice::WriteOnly)) {
                    file.write(responseData.value());
                    file.close();
                    qDebug() << "File saved to:" << savePath;
                } else {
                    qWarning() << "Cannot open file for writing:" << savePath;
                }
            }
        }
    });

    return reply;
}

QCNetworkReply* QCNetworkAccessManager::uploadFile(const QUrl &url, const QString &filePath)
{
    // 使用默认字段名 "file"
    return uploadFile(url, "file", filePath);
}

QCNetworkReply* QCNetworkAccessManager::uploadFile(const QUrl &url, const QString &fieldName,
                                                     const QString &filePath)
{
    // 创建 Multipart 表单数据
    QCMultipartFormData formData;

    // 添加文件字段
    if (!formData.addFileField(fieldName, filePath)) {
        qWarning() << "Cannot add file to form:" << filePath;
        QCNetworkRequest request(url);
        return sendGet(request);
    }

    // 使用 postMultipart 发送
    return postMultipart(url, formData);
}

// ========================================================================
// 流式下载/上传 API
// ========================================================================

QCNetworkReply* QCNetworkAccessManager::downloadToDevice(const QUrl &url, QIODevice *device)
{
    if (!device || !device->isWritable()) {
        qWarning() << "downloadToDevice: device not writable";
        QCNetworkRequest request(url);
        return sendGet(request);
    }

    // 创建请求
    QCNetworkRequest request(url);

    // 发送 GET 请求
    QCNetworkReply *reply = sendGet(request);

    // 连接 readyRead 信号实现流式写入
    QObject::connect(reply, &QCNetworkReply::readyRead, reply, [reply, device]() {
        auto data = reply->readAll();
        if (data.has_value()) {
            device->write(data.value());
        }
    });

    return reply;
}

QCNetworkReply* QCNetworkAccessManager::uploadFromDevice(const QUrl &url, const QString &fieldName,
                                                          QIODevice *device, const QString &fileName,
                                                          const QString &mimeType)
{
    if (!device || !device->isReadable()) {
        qWarning() << "uploadFromDevice: device not readable";
        QCNetworkRequest request(url);
        return sendGet(request);
    }

    QCMultipartFormData formData;

    if (!formData.addFileFieldStream(fieldName, device, fileName, mimeType)) {
        qWarning() << "uploadFromDevice: cannot add stream field to form";
        QCNetworkRequest request(url);
        return sendGet(request);
    }

    // 使用 postMultipart 发送
    return postMultipart(url, formData);
}

QCNetworkReply* QCNetworkAccessManager::downloadFileResumable(const QUrl &url,
                                                                const QString &savePath,
                                                                bool overwrite)
{
    // 检查文件是否存在，用于断点续传
    QFile file(savePath);
    qint64 existingSize = 0;

    if (!overwrite && file.exists()) {
        existingSize = file.size();
        qDebug() << "File exists, resuming download from byte:" << existingSize;
    }

    QCNetworkRequest request(url);

    // 断点续传：设置 Range 头（RFC 7233）
    if (existingSize > 0) {
        QString rangeHeader = QString("bytes=%1-").arg(existingSize);
        request.setRawHeader("Range", rangeHeader.toUtf8());
    }

    QCNetworkReply *reply = sendGet(request);

    // 使用 QSharedPointer 管理文件句柄生命周期
    auto filePtr = QSharedPointer<QFile>::create(savePath);

    QObject::connect(reply, &QCNetworkReply::readyRead, reply,
                     [reply, filePtr, existingSize]() {
        if (!filePtr->isOpen()) {
            QIODevice::OpenMode mode = existingSize > 0
                ? QIODevice::Append
                : QIODevice::WriteOnly;

            if (!filePtr->open(mode)) {
                qWarning() << "Cannot open file:" << filePtr->fileName();
                reply->cancel();
                return;
            }
        }

        auto data = reply->readAll();
        if (data.has_value()) {
            filePtr->write(data.value());
            filePtr->flush();
        }
    });

    QObject::connect(reply, &QCNetworkReply::finished, reply,
                     [reply, filePtr, savePath]() {
        if (filePtr->isOpen()) {
            filePtr->close();
        }

        if (reply->error() == NetworkError::NoError) {
            qDebug() << "Download completed:" << savePath;
        } else {
            qWarning() << "Download failed:" << reply->errorString();
        }
    });

    return reply;
}

// ============================================================================
// 缓存管理实现
// ============================================================================

void QCNetworkAccessManager::setCache(QCNetworkCache *cache)
{
    m_cache = cache;
}

QCNetworkCache* QCNetworkAccessManager::cache() const
{
    return m_cache;
}

void QCNetworkAccessManager::setCachePath(const QString &path, qint64 maxSize)
{
    auto *diskCache = new QCNetworkDiskCache(this);
    diskCache->setCacheDirectory(path);
    diskCache->setMaxCacheSize(maxSize);
    m_cache = diskCache;
}

// ============================================================================
// 日志系统
// ============================================================================

void QCNetworkAccessManager::setLogger(QCNetworkLogger *logger)
{
    Q_D(QCNetworkAccessManager);
    d->logger = logger;
}

QCNetworkLogger* QCNetworkAccessManager::logger() const
{
    Q_D(const QCNetworkAccessManager);
    return d->logger;
}

// ============================================================================
// 中间件系统
// ============================================================================

void QCNetworkAccessManager::addMiddleware(QCNetworkMiddleware *middleware)
{
    Q_D(QCNetworkAccessManager);
    if (middleware && !d->middlewares.contains(middleware)) {
        d->middlewares.append(middleware);
    }
}

void QCNetworkAccessManager::removeMiddleware(QCNetworkMiddleware *middleware)
{
    Q_D(QCNetworkAccessManager);
    d->middlewares.removeAll(middleware);
}

void QCNetworkAccessManager::clearMiddlewares()
{
    Q_D(QCNetworkAccessManager);
    d->middlewares.clear();
}

QList<QCNetworkMiddleware*> QCNetworkAccessManager::middlewares() const
{
    Q_D(const QCNetworkAccessManager);
    return d->middlewares;
}

// ============================================================================
// 流式构建器
// ============================================================================

QCNetworkRequestBuilder QCNetworkAccessManager::newRequest(const QUrl &url)
{
    return QCNetworkRequestBuilder(this, url);
}


// ============================================================================
// Mock 工具
// ============================================================================

void QCNetworkAccessManager::setMockHandler(QCNetworkMockHandler *handler)
{
    Q_D(QCNetworkAccessManager);
    d->mockHandler = handler;
}

QCNetworkMockHandler* QCNetworkAccessManager::mockHandler() const
{
    Q_D(const QCNetworkAccessManager);
    return d->mockHandler;
}

} // namespace QCurl
