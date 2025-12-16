#ifndef QCNETWORKACCESSMANAGER_H
#define QCNETWORKACCESSMANAGER_H

#include <curl/curl.h>

#include <QObject>
#include <QSet>
#include <QJsonObject>
#include <QMap>

#include "QCNetworkRequestBuilder.h"

class QTimer;
class QSocketNotifier;

namespace QCurl {

class QCNetworkRequest;
class QCNetworkReply;
class QCNetworkAccessManagerPrivate;
class QCNetworkRequestScheduler;
class QCMultipartFormData;
class QCNetworkCache;
class QCNetworkLogger;
class QCNetworkMiddleware;
class QCNetworkMockHandler;
class QCNetworkAccessManager : public QObject
{
    Q_OBJECT
public:
    explicit QCNetworkAccessManager(QObject *parent = nullptr);
    virtual ~QCNetworkAccessManager();

    enum CookieFileModeFlag {
        NotOpen = 0x0,
        ReadOnly = 0x1,
        WriteOnly = 0x2,
        ReadWrite = ReadOnly | WriteOnly
    };

    QString cookieFilePath() const;

    CookieFileModeFlag cookieFileMode() const;

    void setCookieFilePath(const QString &cookieFilePath, CookieFileModeFlag flag = CookieFileModeFlag::ReadWrite);

    // ========================================================================
    // 核心 API：返回统一的 QCNetworkReply
    // ========================================================================

    /**
     * @brief 发送 HEAD 请求（异步）
     * @param request 请求配置
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     * @note 请求会自动启动（已调用 execute()）
     */
    QCNetworkReply* sendHead(const QCNetworkRequest &request);

    /**
     * @brief 发送 GET 请求（异步）
     * @param request 请求配置
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     * @note 请求会自动启动（已调用 execute()）
     */
    QCNetworkReply* sendGet(const QCNetworkRequest &request);

    /**
     * @brief 发送 POST 请求（异步）
     * @param request 请求配置
     * @param data 请求体数据
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     * @note 请求会自动启动（已调用 execute()）
     */
    QCNetworkReply* sendPost(const QCNetworkRequest &request, const QByteArray &data);

    /**
     * @brief 发送 PUT 请求（异步）
     * @param request 请求配置
     * @param data 请求体数据
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     * @note 请求会自动启动（已调用 execute()）
     */
    QCNetworkReply* sendPut(const QCNetworkRequest &request, const QByteArray &data);

    /**
     * @brief 发送 DELETE 请求（异步）
     * @param request 请求配置
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     * @note 请求会自动启动（已调用 execute()）
     */
    QCNetworkReply* sendDelete(const QCNetworkRequest &request);

    /**
     * @brief 发送 PATCH 请求（异步）
     * @param request 请求配置
     * @param data 请求体数据
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     * @note 请求会自动启动（已调用 execute()）
     */
    QCNetworkReply* sendPatch(const QCNetworkRequest &request, const QByteArray &data);

    /**
     * @brief 发送 GET 请求（同步，阻塞）
     * @param request 请求配置
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     * @warning 会阻塞当前线程直到请求完成，不要在 UI 线程中调用
     */
    QCNetworkReply* sendGetSync(const QCNetworkRequest &request);

    /**
     * @brief 发送 POST 请求（同步，阻塞）
     * @param request 请求配置
     * @param data 请求体数据
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     * @warning 会阻塞当前线程直到请求完成，不要在 UI 线程中调用
     */
    QCNetworkReply* sendPostSync(const QCNetworkRequest &request, const QByteArray &data);

    // ========================================================================
    // 日志系统、中间件、取消令牌
    // ========================================================================

    /**
     * @brief 设置日志记录器
     * @param logger 日志记录器对象（管理器不持有所有权，由调用者管理生命周期）
     */
    void setLogger(QCNetworkLogger *logger);

    /**
     * @brief 获取当前日志记录器
     * @return 日志记录器指针，如果未设置则返回 nullptr
     */
    QCNetworkLogger* logger() const;

    /**
     * @brief 添加请求/响应中间件
     * @param middleware 中间件对象（管理器不持有所有权）
     */
    void addMiddleware(QCNetworkMiddleware *middleware);

    /**
     * @brief 移除中间件
     * @param middleware 要移除的中间件对象
     */
    void removeMiddleware(QCNetworkMiddleware *middleware);

    /**
     * @brief 清空所有中间件
     */
    void clearMiddlewares();

    /**
     * @brief 获取所有中间件
     * @return 中间件列表
     */
    QList<QCNetworkMiddleware*> middlewares() const;

    // ========================================================================
    // 流式构建器、快捷方法、Mock 工具
    // ========================================================================

    /**
     * @brief 创建流式请求构建器
     * @param url 请求 URL
     * @return 请求构建器对象（值语义，可链式调用）
     */
    QCNetworkRequestBuilder newRequest(const QUrl &url);

    /**
     * @brief 发送 JSON POST 请求
     * @param url 请求 URL
     * @param json JSON 对象
     * @return 网络响应对象
     */
    QCNetworkReply* postJson(const QUrl &url, const QJsonObject &json);

    /**
     * @brief 上传单个文件
     * @param url 请求 URL
     * @param filePath 文件路径
     * @param fieldName 字段名（默认为 "file"）
     * @return 网络响应对象
     */
    QCNetworkReply* uploadFile(const QUrl &url, 
                               const QString &filePath, 
                               const QString &fieldName = QString("file"));

    /**
     * @brief 下载文件到指定路径
     * @param url 请求 URL
     * @param savePath 保存路径
     * @return 网络响应对象（文件自动保存在 finished 信号触发时）
     */
    QCNetworkReply* downloadFile(const QUrl &url, const QString &savePath);

    /**
     * @brief 设置 Mock 处理器（用于单元测试）
     * @param handler Mock 处理器对象（管理器不持有所有权）
     */
    void setMockHandler(QCNetworkMockHandler *handler);

    /**
     * @brief 获取当前 Mock 处理器
     * @return Mock 处理器指针，如果未设置则返回 nullptr
     */
    QCNetworkMockHandler* mockHandler() const;

    // ========================================================================
    // 请求优先级调度
    // ========================================================================

    /**
     * @brief 启用/禁用请求调度器
     * 
     * 启用后，所有通过 scheduleXXX() 方法发送的请求会经过调度器管理。
     * 
     * @param enable true 为启用调度器，false 为禁用
     */
    void enableRequestScheduler(bool enable);

    /**
     * @brief 检查调度器是否已启用
     * 
     * @return true 如果调度器已启用
     */
    bool isSchedulerEnabled() const;

    /**
     * @brief 获取调度器实例
     * 
     * 用于配置调度器参数（并发限制、带宽限制等）。
     * 
     * @return 调度器指针
     * 
     * @code
     * manager.enableRequestScheduler(true);
     * auto *scheduler = manager.scheduler();
     * 
     * QCNetworkRequestScheduler::Config config;
     * config.maxConcurrentRequests = 10;
     * config.maxBandwidthBytesPerSec = 1024 * 1024;  // 1 MB/s
     * scheduler->setConfig(config);
     * @endcode
     */
    QCNetworkRequestScheduler* scheduler() const;

    /**
     * @brief 使用调度器发送 GET 请求
     * 
     * 请求会根据优先级（request.priority()）加入队列。
     * 
     * @param request 请求配置（包含优先级）
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     * 
     * @note 如果调度器未启用，会回退到 sendGet() 方法
     */
    QCNetworkReply* scheduleGet(const QCNetworkRequest &request);

    /**
     * @brief 使用调度器发送 POST 请求
     * 
     * @param request 请求配置（包含优先级）
     * @param data 请求体数据
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     */
    QCNetworkReply* schedulePost(const QCNetworkRequest &request, const QByteArray &data);

    /**
     * @brief 使用调度器发送 PUT 请求
     * 
     * @param request 请求配置（包含优先级）
     * @param data 请求体数据
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     */
    QCNetworkReply* schedulePut(const QCNetworkRequest &request, const QByteArray &data);


    /**
     * @brief POST URL-encoded 表单数据（快捷方法）
     *
     * 自动URL编码表单字段并设置 Content-Type: application/x-www-form-urlencoded
     *
     * @param url 请求 URL
     * @param formData 表单字段键值对
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     *
     * @code
     * QMap<QString, QString> form;
     * form["username"] = "alice";
     * form["password"] = "secret";
     *
     * auto *reply = manager->postForm("https://example.com/login", form);
     * @endcode
     *
     */
    QCNetworkReply* postForm(const QUrl &url, const QMap<QString, QString> &formData);

    // ========================================================================
    // Multipart/form-data 支持
    // ========================================================================

    /**
     * @brief POST Multipart/form-data 数据（快捷方法）
     *
     * 自动设置 Content-Type: multipart/form-data; boundary=xxx
     *
     * @param url 请求 URL
     * @param formData Multipart 表单数据
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     *
     * @code
     * QCMultipartFormData formData;
     * formData.addTextField("username", "alice");
     * formData.addFileField("avatar", "/path/to/avatar.jpg", "image/jpeg");
     *
     * auto *reply = manager->postMultipart("https://api.example.com/upload", formData);
     * connect(reply, &QCNetworkReply::finished, [reply]() {
     *     qDebug() << "Upload status:" << reply->httpStatusCode();
     *     reply->deleteLater();
     * });
     * @endcode
     *
     */
    QCNetworkReply* postMultipart(const QUrl &url, const QCMultipartFormData &formData);

    /**
     * @brief POST Multipart/form-data 数据（带请求配置）
     *
     * @param request 请求配置（可设置超时、SSL、代理等）
     * @param formData Multipart 表单数据
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     *
     * @code
     * QCNetworkRequest request(QUrl("https://api.example.com/upload"));
     * request.setTimeout(std::chrono::seconds(60));  // 60秒超时
     *
     * QCMultipartFormData formData;
     * formData.addTextField("userId", "12345");
     * formData.addFileField("document", "/path/to/large-file.pdf");
     *
     * auto *reply = manager->postMultipart(request, formData);
     * @endcode
     *
     */
    QCNetworkReply* postMultipart(const QCNetworkRequest &request,
                                   const QCMultipartFormData &formData);

    // ========================================================================
    // 文件操作便捷 API
    // ========================================================================


    /**
     * @brief 上传文件（简单版）
     *
     * 使用 multipart/form-data 格式上传单个文件，字段名为 "file"
     *
     * @param url 上传 URL
     * @param filePath 本地文件路径
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     *
     * @code
     * auto *reply = manager->uploadFile("https://api.example.com/upload", "/tmp/photo.jpg");
     * connect(reply, &QCNetworkReply::finished, [reply]() {
     *     qDebug() << "Upload status:" << reply->httpStatusCode();
     *     reply->deleteLater();
     * });
     * @endcode
     *
     */
    QCNetworkReply* uploadFile(const QUrl &url, const QString &filePath);


    // ========================================================================
    // 流式下载/上传 API
    // ========================================================================

    /**
     * @brief 下载文件到 QIODevice（流式下载）
     *
     * 不会将整个文件加载到内存，而是边下载边写入设备。
     * 适用于大文件下载。
     *
     * @param url 下载 URL
     * @param device 目标设备（如 QFile、QBuffer），必须可写
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     *
     * @code
     * QFile *file = new QFile("/tmp/large-video.mp4");
     * if (file->open(QIODevice::WriteOnly)) {
     *     auto *reply = manager->downloadToDevice(url, file);
     *     connect(reply, &QCNetworkReply::downloadProgress,
     *             [](qint64 received, qint64 total) {
     *         qDebug() << "Progress:" << (received * 100 / total) << "%";
     *     });
     *     connect(reply, &QCNetworkReply::finished, [file]() {
     *         file->close();
     *         file->deleteLater();
     *     });
     * }
     * @endcode
     *
     * @note device 必须在请求完成前保持有效
     */
    QCNetworkReply* downloadToDevice(const QUrl &url, QIODevice *device);

    /**
     * @brief 从 QIODevice 上传数据（流式上传）
     *
     * 不会将整个文件加载到内存，而是边读取边上传。
     * 适用于大文件上传。
     *
     * @param url 上传 URL
     * @param fieldName 字段名
     * @param device 源设备（如 QFile），必须可读
     * @param fileName 文件名（会出现在请求中）
     * @param mimeType MIME 类型
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     *
     * @code
     * QFile *largeFile = new QFile("/tmp/large-file.bin");
     * if (largeFile->open(QIODevice::ReadOnly)) {
     *     auto *reply = manager->uploadFromDevice(url, "file", largeFile,
     *                                             "large-file.bin",
     *                                             "application/octet-stream");
     *     connect(reply, &QCNetworkReply::uploadProgress,
     *             [](qint64 sent, qint64 total) {
     *         qDebug() << "Progress:" << (sent * 100 / total) << "%";
     *     });
     *     connect(reply, &QCNetworkReply::finished, [largeFile]() {
     *         largeFile->close();
     *         largeFile->deleteLater();
     *     });
     * }
     * @endcode
     *
     * @note device 必须在请求完成前保持有效
     */
    QCNetworkReply* uploadFromDevice(const QUrl &url, const QString &fieldName,
                                      QIODevice *device, const QString &fileName,
                                      const QString &mimeType);

    /**
     * @brief 下载文件（支持断点续传）
     *
     * 如果目标文件已存在，会自动从已下载的大小处继续下载（断点续传）。
     * 如果不存在，则从头开始下载。
     *
     * @param url 下载 URL
     * @param savePath 保存路径
     * @param overwrite 如果文件已存在，是否覆盖（默认 false，即断点续传）
     * @return 网络响应对象，调用者需要调用 deleteLater() 释放
     *
     * @code
     * // 支持断点续传的下载
     * auto *reply = manager->downloadFileResumable(
     *     QUrl("https://example.com/large-file.zip"),
     *     "/tmp/large-file.zip"
     * );
     *
     * connect(reply, &QCNetworkReply::downloadProgress,
     *         [](qint64 received, qint64 total) {
     *     qDebug() << "Progress:" << (received * 100 / total) << "%";
     * });
     *
     * connect(reply, &QCNetworkReply::finished, [reply]() {
     *     if (reply->error() == NetworkError::NoError) {
     *         qDebug() << "Download completed!";
     *     } else {
     *         qWarning() << "Download failed, can retry later";
     *     }
     *     reply->deleteLater();
     * });
     *
     * // 下载失败后，再次调用会从断点继续
     * auto *retryReply = manager->downloadFileResumable(
     *     QUrl("https://example.com/large-file.zip"),
     *     "/tmp/large-file.zip"
     * );
     * @endcode
     *
     * @note 服务器必须支持 HTTP Range 请求（检查响应头 Accept-Ranges: bytes）
     * @warning 如果服务器不支持 Range 请求，会从头开始下载（覆盖现有文件）
     */
    QCNetworkReply* downloadFileResumable(const QUrl &url,
                                           const QString &savePath,
                                           bool overwrite = false);

    // ========================================================================
    // 缓存管理
    // ========================================================================

    /**
     * @brief 设置缓存实例
     *
     * 设置后，所有请求将根据其缓存策略使用此缓存。
     *
     * @param cache 缓存实例（QCNetworkMemoryCache 或 QCNetworkDiskCache）
     *
     * @code
     * auto *cache = new QCNetworkMemoryCache();
     * cache->setMaxCacheSize(20 * 1024 * 1024);  // 20MB
     * manager->setCache(cache);
     * @endcode
     *
     * @note Manager 不会获取缓存的所有权，调用者需要管理缓存的生命周期
     */
    void setCache(QCNetworkCache *cache);

    /**
     * @brief 获取当前缓存实例
     * @return 缓存指针，如果未设置返回 nullptr
     */
    QCNetworkCache* cache() const;

    /**
     * @brief 设置磁盘缓存路径（便捷方法）
     *
     * 自动创建 QCNetworkDiskCache 实例并设置缓存目录。
     *
     * @param path 缓存目录路径
     * @param maxSize 最大缓存大小（字节），默认 50MB
     *
     * @code
     * manager->setCachePath("/tmp/qcurl_cache", 100 * 1024 * 1024);  // 100MB
     * @endcode
     *
     * @note 会替换现有的缓存实例
     */
    void setCachePath(const QString &path, qint64 maxSize = 50 * 1024 * 1024);

signals:

public slots:

private:
    QCNetworkAccessManagerPrivate *const d_ptr;
    Q_DECLARE_PRIVATE(QCNetworkAccessManager)
    CookieFileModeFlag m_cookieModeFlag;
    QString                     m_cookieFilePath;
    bool                        m_schedulerEnabled;
    QCNetworkCache             *m_cache;
};

} //namespace QCurl
#endif // QCNETWORKACCESSMANAGER_H
