#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "QCCurlMultiManager.h"
#include "QCNetworkError.h"
#include "QCNetworkRetryPolicy.h"
#include "QCNetworkProxyConfig.h"
#include "QCUtility.h"
#include "QCNetworkTimeoutConfig.h"
#include "QCNetworkSslConfig.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkCache.h"
#include "QCNetworkCachePolicy.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkConnectionPoolManager.h"

#include <QDebug>
#include <QUrl>
#include <QThread>
#include <QTimer>
#include <QFileInfo>  // 用于检查系统 CA 证书路径

namespace QCurl {

// ============================================================================
// QCNetworkReplyPrivate 实现
// ============================================================================

QCNetworkReplyPrivate::QCNetworkReplyPrivate(QCNetworkReply *q,
                                             const QCNetworkRequest &req,
                                             HttpMethod method,
                                             ExecutionMode mode,
                                             const QByteArray &body)
    : q_ptr(q),
      request(req),
      httpMethod(method),
      executionMode(mode),
      requestBody(body),
      multiProcessor(nullptr),
      state(ReplyState::Idle),
      errorCode(NetworkError::NoError),
      bytesDownloaded(0),
      bytesUploaded(0),
      downloadTotal(-1),
      uploadTotal(-1),
      attemptCount(0),
      cookieMode(0)
{
}

QCNetworkReplyPrivate::~QCNetworkReplyPrivate()
{
    // 如果正在运行（异步模式），从多句柄管理器移除
    // 注意：cancel() 会在 ~QCNetworkReply() 中被调用，所以这里通常不需要额外处理
    // 但为安全起见，如果对象直接销毁且状态仍为 Running，确保清理
    if (executionMode == ExecutionMode::Async
        && (state == ReplyState::Running || state == ReplyState::Paused)
        && q_ptr) {
        QCCurlMultiManager::instance()->removeReply(q_ptr);
    }
}

bool QCNetworkReplyPrivate::configureCurlOptions()
{
    CURL *handle = curlManager.handle();
    if (!handle) {
        qCritical() << "QCNetworkReply: curl handle is null";
        return false;
    }

    // ========================================================================
    // 1. 基础配置
    // ========================================================================

    // 设置 URL
    QByteArray urlBytes = request.url().toString().toUtf8();
    curl_easy_setopt(handle, CURLOPT_URL, urlBytes.constData());

    // 设置私有数据（用于回调中识别对象）
    curl_easy_setopt(handle, CURLOPT_PRIVATE, this);

    // 跟随重定向
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION,
                    request.followLocation() ? 1L : 0L);

    // 多线程安全（禁用信号处理）
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);

    // ========================================================================
    // 2. HTTP 方法配置
    // ========================================================================

    switch (httpMethod) {
        case HttpMethod::Head:
            // HEAD 请求：只获取响应头，不获取响应体
            curl_easy_setopt(handle, CURLOPT_NOBODY, 1L);
            break;

        case HttpMethod::Get:
            // GET 请求：获取完整响应
            curl_easy_setopt(handle, CURLOPT_HTTPGET, 1L);
            break;

        case HttpMethod::Post:
            // POST 请求：发送数据
            curl_easy_setopt(handle, CURLOPT_POST, 1L);
            curl_easy_setopt(handle, CURLOPT_POSTFIELDS, requestBody.constData());
            curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE,
                           static_cast<long>(requestBody.size()));
            break;

        case HttpMethod::Put:
            // PUT 请求：上传资源
            curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "PUT");
            if (!requestBody.isEmpty()) {
                curl_easy_setopt(handle, CURLOPT_POSTFIELDS, requestBody.constData());
                curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE,
                               static_cast<long>(requestBody.size()));
            }
            break;

        case HttpMethod::Delete:
            // DELETE 请求：删除资源
            curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "DELETE");
            break;

        case HttpMethod::Patch:
            // PATCH 请求：部分更新
            // PATCH 必须使用 CUSTOMREQUEST，因为 libcurl 没有专门的 PATCH 选项
            curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "PATCH");
            if (!requestBody.isEmpty()) {
                curl_easy_setopt(handle, CURLOPT_POSTFIELDS, requestBody.constData());
                curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE,
                               static_cast<long>(requestBody.size()));
            }
            break;

        default:
            qWarning() << "QCNetworkReply: Unknown HTTP method";
            return false;
    }

    // ========================================================================
    // 3. 自定义 HTTP Headers
    // ========================================================================

    QList<QByteArray> headerNames = request.rawHeaderList();
    for (const QByteArray &headerName : headerNames) {
        QByteArray headerValue = request.rawHeader(headerName);
        // 格式化为 "Name: Value" 格式
        QString headerLine = QString::fromUtf8(headerName) + ": " + QString::fromUtf8(headerValue);
        curlManager.appendHeader(headerLine);
    }

    // 应用 header 列表
    if (curlManager.headerList()) {
        curl_easy_setopt(handle, CURLOPT_HTTPHEADER, curlManager.headerList());
    }

    // ========================================================================
    // 4. Range 请求配置
    // ========================================================================

    if (request.rangeStart() >= 0 && request.rangeEnd() > request.rangeStart()) {
        QString rangeStr = QString("%1-%2")
                          .arg(request.rangeStart())
                          .arg(request.rangeEnd());
        QByteArray rangeBytes = rangeStr.toUtf8();
        curl_easy_setopt(handle, CURLOPT_RANGE, rangeBytes.constData());
    }

    // ========================================================================
    // 5. 代理配置
    // ========================================================================

    if (const auto proxyConfigOpt = request.proxyConfig(); proxyConfigOpt.has_value()) {
        const QCNetworkProxyConfig &proxyConfig = proxyConfigOpt.value();
        if (proxyConfig.type != QCNetworkProxyConfig::ProxyType::None) {
            if (!proxyConfig.isValid()) {
                qWarning() << "QCNetworkReply: invalid proxy configuration ignored";
            } else {
                proxyHostBytes = proxyConfig.hostName.toUtf8();
                curl_easy_setopt(handle, CURLOPT_PROXY, proxyHostBytes.constData());

                if (proxyConfig.port > 0) {
                    curl_easy_setopt(handle, CURLOPT_PROXYPORT,
                                      static_cast<long>(proxyConfig.port));
                }

                long proxyType = CURLPROXY_HTTP;
                switch (proxyConfig.type) {
                case QCNetworkProxyConfig::ProxyType::Http:
                    proxyType = CURLPROXY_HTTP;
                    break;
                case QCNetworkProxyConfig::ProxyType::Https:
#ifdef CURLPROXY_HTTPS
                    proxyType = CURLPROXY_HTTPS;
#else
                    proxyType = CURLPROXY_HTTP;
#endif
                    break;
                case QCNetworkProxyConfig::ProxyType::Socks4:
                    proxyType = CURLPROXY_SOCKS4;
                    break;
                case QCNetworkProxyConfig::ProxyType::Socks4A:
                    proxyType = CURLPROXY_SOCKS4A;
                    break;
                case QCNetworkProxyConfig::ProxyType::Socks5:
                    proxyType = CURLPROXY_SOCKS5;
                    break;
                case QCNetworkProxyConfig::ProxyType::Socks5Hostname:
                    proxyType = CURLPROXY_SOCKS5_HOSTNAME;
                    break;
                default:
                    proxyType = CURLPROXY_HTTP;
                    break;
                }
                curl_easy_setopt(handle, CURLOPT_PROXYTYPE, proxyType);

                if (!proxyConfig.userName.isEmpty()) {
                    proxyUserBytes = proxyConfig.userName.toUtf8();
                    curl_easy_setopt(handle, CURLOPT_PROXYUSERNAME,
                                     proxyUserBytes.constData());
                } else {
                    proxyUserBytes.clear();
                }

                if (!proxyConfig.password.isEmpty()) {
                    proxyPasswordBytes = proxyConfig.password.toUtf8();
                    curl_easy_setopt(handle, CURLOPT_PROXYPASSWORD,
                                     proxyPasswordBytes.constData());
                } else {
                    proxyPasswordBytes.clear();
                }

                if (!proxyConfig.userName.isEmpty() || !proxyConfig.password.isEmpty()) {
                    curl_easy_setopt(handle, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
                }
            }
        } else {
            proxyHostBytes.clear();
            proxyUserBytes.clear();
            proxyPasswordBytes.clear();
        }
    } else {
        proxyHostBytes.clear();
        proxyUserBytes.clear();
        proxyPasswordBytes.clear();
    }

    // ========================================================================
    // 连接池配置 (v2.14.0)
    // ========================================================================

    // 先应用连接池的通用配置（DNS/复用等），再由请求级别配置覆盖（例如 HTTP 版本）。
    auto *poolManager = QCNetworkConnectionPoolManager::instance();
    const QString host = request.url().host();
    poolManager->configureCurlHandle(handle, host);

    // ========================================================================
    // HTTP 版本配置
    // ========================================================================

    const QCNetworkHttpVersion httpVer = request.httpVersion();
    if (httpVer != QCNetworkHttpVersion::Http1_1 || request.isHttpVersionExplicit()) {
        long curlVersion = toCurlHttpVersion(httpVer);
        curl_easy_setopt(handle, CURLOPT_HTTP_VERSION, curlVersion);
    }

    // ========================================================================
    // 6. SSL 配置（基于 QCNetworkSslConfig）
    // ========================================================================

    const QCNetworkSslConfig sslConfig = request.sslConfig();
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, sslConfig.verifyPeer ? 1L : 0L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, sslConfig.verifyHost ? 2L : 0L);

    if (!sslConfig.caCertPath.isEmpty()) {
        sslCaCertPathBytes = sslConfig.caCertPath.toUtf8();
        curl_easy_setopt(handle, CURLOPT_CAINFO, sslCaCertPathBytes.constData());
    } else {
        // ✅ 修复：如果未指定 CA 证书路径，尝试使用系统默认路径
        // 优先使用常见的系统 CA 证书位置
        static const char* systemCaPaths[] = {
            "/etc/ssl/certs/ca-certificates.crt",  // Debian/Ubuntu/Arch
            "/etc/pki/tls/certs/ca-bundle.crt",    // RHEL/CentOS
            "/etc/ssl/cert.pem",                    // Alpine/OpenBSD
            "/usr/local/share/certs/ca-root-nss.crt", // FreeBSD
            nullptr
        };
        
        bool caPathSet = false;
        for (int i = 0; systemCaPaths[i] != nullptr; ++i) {
            QFileInfo fi(systemCaPaths[i]);
            if (fi.exists() && fi.isReadable()) {
                curl_easy_setopt(handle, CURLOPT_CAINFO, systemCaPaths[i]);
                caPathSet = true;
                break;
            }
        }
        
        if (!caPathSet) {
            // 回退到 libcurl 默认行为（使用编译时配置的路径）
            sslCaCertPathBytes.clear();
            curl_easy_setopt(handle, CURLOPT_CAINFO, nullptr);
        }
    }

    if (!sslConfig.clientCertPath.isEmpty()) {
        sslClientCertPathBytes = sslConfig.clientCertPath.toUtf8();
        curl_easy_setopt(handle, CURLOPT_SSLCERT, sslClientCertPathBytes.constData());
    } else {
        sslClientCertPathBytes.clear();
        curl_easy_setopt(handle, CURLOPT_SSLCERT, nullptr);
    }

    if (!sslConfig.clientKeyPath.isEmpty()) {
        sslClientKeyPathBytes = sslConfig.clientKeyPath.toUtf8();
        curl_easy_setopt(handle, CURLOPT_SSLKEY, sslClientKeyPathBytes.constData());
    } else {
        sslClientKeyPathBytes.clear();
        curl_easy_setopt(handle, CURLOPT_SSLKEY, nullptr);
    }

    if (!sslConfig.clientKeyPassword.isEmpty()) {
        sslClientKeyPasswordBytes = sslConfig.clientKeyPassword.toUtf8();
        curl_easy_setopt(handle, CURLOPT_KEYPASSWD, sslClientKeyPasswordBytes.constData());
    } else {
        sslClientKeyPasswordBytes.clear();
        curl_easy_setopt(handle, CURLOPT_KEYPASSWD, nullptr);
    }

    // ========================================================================
    // 7. 超时配置
    // ========================================================================

    QCNetworkTimeoutConfig timeout = request.timeoutConfig();

    // 连接超时（TCP 三次握手超时）
    if (timeout.connectTimeout.has_value() && timeout.connectTimeout->count() > 0) {
        curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS,
                        static_cast<long>(timeout.connectTimeout->count()));
    }

    // 总超时（整个请求的最大时间）
    if (timeout.totalTimeout.has_value() && timeout.totalTimeout->count() > 0) {
        curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS,
                        static_cast<long>(timeout.totalTimeout->count()));
    }

    // 低速检测：如果在 lowSpeedTime 内速度低于 lowSpeedLimit，则超时
    if (timeout.lowSpeedTime.has_value() && timeout.lowSpeedTime->count() > 0) {
        curl_easy_setopt(handle, CURLOPT_LOW_SPEED_TIME,
                        static_cast<long>(timeout.lowSpeedTime->count()));
    }

    if (timeout.lowSpeedLimit.has_value() && *timeout.lowSpeedLimit > 0) {
        curl_easy_setopt(handle, CURLOPT_LOW_SPEED_LIMIT,
                        static_cast<long>(*timeout.lowSpeedLimit));
    }

    // ========================================================================
    // 8. 回调函数配置
    // ========================================================================

    // 响应体写回调
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, QCNetworkReplyPrivate::curlWriteCallback);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, this);

    // 响应头回调
    curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, QCNetworkReplyPrivate::curlHeaderCallback);
    curl_easy_setopt(handle, CURLOPT_HEADERDATA, this);

    // 注意：READFUNCTION 已被移除，因为 PUT/PATCH 使用 CURLOPT_POSTFIELDS
    // 如果未来需要支持大文件上传，可以使用 CURLOPT_READFUNCTION 替代 POST FIELDS

    // 定位回调
    curl_easy_setopt(handle, CURLOPT_SEEKFUNCTION, QCNetworkReplyPrivate::curlSeekCallback);
    curl_easy_setopt(handle, CURLOPT_SEEKDATA, this);

    // 进度回调
    curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, QCNetworkReplyPrivate::curlProgressCallback);
    curl_easy_setopt(handle, CURLOPT_XFERINFODATA, this);
    curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);  // 启用进度回调

    return true;
}

void QCNetworkReplyPrivate::setState(ReplyState newState)
{
    Q_Q(QCNetworkReply);

    if (state == newState) {
        return;
    }

    state = newState;

    // 发射状态变更信号
    emit q->stateChanged(newState);

    // 根据新状态发射对应信号
    if (newState == ReplyState::Finished) {
        // 完成时解析响应头
        parseHeaders();

        // ========================================================================
        // Cookie jar flush：当启用 COOKIEJAR 时，确保请求完成后立即落盘
        // ========================================================================
#ifdef CURLOPT_COOKIELIST
        if (curlManager.handle() && (cookieMode & 0x2) && !cookieFilePath.isEmpty()) {
            curl_easy_setopt(curlManager.handle(), CURLOPT_COOKIELIST, "FLUSH");
        }
#endif

        // ========================================================================
        // 连接池统计 - 记录连接复用情况
        // ========================================================================
        if (curlManager.handle()) {
            // 使用 NUM_CONNECTS 检查连接复用
            // 返回值含义：0 = 未知, 1 = 新连接, >1 = 可能复用了连接
            long numConnects = 0;
            curl_easy_getinfo(curlManager.handle(), CURLINFO_NUM_CONNECTS, &numConnects);
            
            // 注意：NUM_CONNECTS 是累计值，不是精确的复用标志
            // 这里简化处理：每个请求都视为"使用了连接池"
            // 实际复用率由 libcurl 内部管理
            auto *poolManager = QCNetworkConnectionPoolManager::instance();
            poolManager->recordRequestCompleted(curlManager.handle(), false);
        }

        // ========================================================================
        // 缓存集成 - 请求完成后自动写入缓存
        // ========================================================================
        QCNetworkAccessManager *manager = qobject_cast<QCNetworkAccessManager*>(q->parent());
        if (manager) {
            QCNetworkCache *cache = manager->cache();
            QCNetworkCachePolicy policy = request.cachePolicy();

            // OnlyNetwork 策略：不缓存
            if (cache && policy != QCNetworkCachePolicy::OnlyNetwork) {
                // 只缓存成功响应
                if (errorCode == NetworkError::NoError) {
                    // 解析响应头（检查可缓存性）
                    QMap<QByteArray, QByteArray> headers = q->parseResponseHeaders();

                    if (QCNetworkCache::isCacheable(headers)) {
                        // 准备元数据
                        QCNetworkCacheMetadata meta;
                        meta.url = request.url();
                        meta.headers = headers;
                        meta.expirationDate = QCNetworkCache::parseExpirationDate(headers);

                        // 获取响应数据（不移除缓冲区数据）
                        QByteArray responseData = bodyBuffer.read(bodyBuffer.byteAmount());

                        // 写入缓存
                        cache->insert(request.url(), responseData, meta);
                        qDebug() << "QCNetworkReply: Cached response for" << request.url().toString();

                        // 重新放回缓冲区（确保 readAll() 仍可用）
                        bodyBuffer.append(responseData);
                    } else {
                        qDebug() << "QCNetworkReply: Response not cacheable for" << request.url().toString();
                    }
                }
            }
        }

        emit q->finished();
    } else if (newState == ReplyState::Error) {
        // 错误时同时发射 error 和 finished 信号
        emit q->error(errorCode);
        emit q->finished();
    } else if (newState == ReplyState::Cancelled) {
        // 取消时发射 cancelled 信号
        emit q->cancelled();
    }
}

void QCNetworkReplyPrivate::setError(NetworkError error, const QString &message)
{
    errorCode = error;
    errorMessage = message;
}

void QCNetworkReplyPrivate::parseHeaders()
{
    headerMap.clear();

    // 按行分割响应头数据
    QList<QByteArray> lines = headerData.split('\n');

    for (const QByteArray &line : lines) {
        // 查找冒号分隔符
        int colonPos = line.indexOf(':');
        if (colonPos > 0) {
            QByteArray key = line.left(colonPos).trimmed();
            QByteArray value = line.mid(colonPos + 1).trimmed();

            // 存储到 map 中（键值对转为 QString）
            headerMap.insert(QString::fromUtf8(key), QString::fromUtf8(value));
        }
    }
}

// ============================================================================
// Curl 静态回调函数实现
// ============================================================================

size_t QCNetworkReplyPrivate::curlWriteCallback(char *ptr, size_t size,
                                                size_t nmemb, void *userdata)
{
    auto *d = static_cast<QCNetworkReplyPrivate*>(userdata);
    if (!d || !d->q_ptr) {
        return 0;  // 对象已销毁，中止传输
    }

    const size_t totalSize = size * nmemb;

    if (d->state == ReplyState::Cancelled) {
        // 取消后禁止继续产生可观测数据事件（readyRead 等）；
        // 返回 totalSize 以避免触发额外的 libcurl 错误路径。
        return totalSize;
    }

    if (d->executionMode == ExecutionMode::Async) {
        // 异步模式：累积数据到缓冲区
        d->bodyBuffer.append(QByteArray(ptr, static_cast<int>(totalSize)));
        d->bytesDownloaded += static_cast<qint64>(totalSize);

        // 发射 readyRead 信号
        emit d->q_ptr->readyRead();
    } else {
        // 同步模式：优先调用用户回调，否则累积到缓冲区
        if (d->writeCallback) {
            return d->writeCallback(ptr, totalSize);
        } else {
            // 没有回调函数时，也累积数据（支持 readAll()）
            d->bodyBuffer.append(QByteArray(ptr, static_cast<int>(totalSize)));
            d->bytesDownloaded += static_cast<qint64>(totalSize);
        }
    }

    return totalSize;
}

size_t QCNetworkReplyPrivate::curlHeaderCallback(char *ptr, size_t size,
                                                 size_t nmemb, void *userdata)
{
    auto *d = static_cast<QCNetworkReplyPrivate*>(userdata);
    if (!d || !d->q_ptr) {
        return 0;  // 对象已销毁，中止传输
    }

    const size_t totalSize = size * nmemb;

    // 累积原始响应头数据（异步和同步模式都需要）
    d->headerData.append(ptr, static_cast<int>(totalSize));

    // 同步模式：调用用户回调
    if (d->executionMode == ExecutionMode::Sync && d->headerCallback) {
        d->headerCallback(ptr, totalSize);
    }

    return totalSize;
}

size_t QCNetworkReplyPrivate::curlReadCallback(char* /* ptr */, size_t /* size */,
                                               size_t /* nmemb */, void *userdata)
{
    auto *d = static_cast<QCNetworkReplyPrivate*>(userdata);
    if (!d || !d->q_ptr) {
        return CURL_READFUNC_ABORT;  // 对象已销毁，中止传输
    }

    // 用于上传操作（PUT 等）
    // 目前使用 CURLOPT_POSTFIELDS，这个回调暂不使用
    // 后续可扩展支持流式上传

    return 0;
}

int QCNetworkReplyPrivate::curlSeekCallback(void *userdata,
                                            curl_off_t offset, int origin)
{
    auto *d = static_cast<QCNetworkReplyPrivate*>(userdata);
    if (!d || !d->q_ptr) {
        return CURL_SEEKFUNC_FAIL;  // 对象已销毁
    }

    // 同步模式：调用用户回调
    if (d->executionMode == ExecutionMode::Sync && d->seekCallback) {
        return d->seekCallback(static_cast<qint64>(offset), origin);
    }

    return CURL_SEEKFUNC_CANTSEEK;  // 暂不支持
}

int QCNetworkReplyPrivate::curlProgressCallback(void *userdata,
                                                curl_off_t dltotal, curl_off_t dlnow,
                                                curl_off_t ultotal, curl_off_t ulnow)
{
    auto *d = static_cast<QCNetworkReplyPrivate*>(userdata);
    if (!d || !d->q_ptr) {
        return 1;  // 对象已销毁，中止传输
    }

    if (d->state == ReplyState::Cancelled) {
        // 取消后禁止继续产生可观测事件（downloadProgress/uploadProgress）。
        return 0;
    }

    // 更新进度数据
    d->downloadTotal = static_cast<qint64>(dltotal);
    d->bytesDownloaded = static_cast<qint64>(dlnow);
    d->uploadTotal = static_cast<qint64>(ultotal);
    d->bytesUploaded = static_cast<qint64>(ulnow);

    if (d->executionMode == ExecutionMode::Async) {
        // 异步模式：发射进度信号
        emit d->q_ptr->downloadProgress(d->bytesDownloaded, d->downloadTotal);
        emit d->q_ptr->uploadProgress(d->bytesUploaded, d->uploadTotal);
    } else {
        // 同步模式：调用用户回调
        if (d->progressCallback) {
            d->progressCallback(dltotal, dlnow, ultotal, ulnow);
        }
    }

    // 返回 0 继续传输，非 0 中止传输
    return 0;
}

// ============================================================================
// QCNetworkReply 公共接口实现
// ============================================================================

QCNetworkReply::QCNetworkReply(const QCNetworkRequest &request,
                               HttpMethod method,
                               ExecutionMode mode,
                               const QByteArray &requestBody,
                               QObject *parent)
    : QObject(parent),
      d_ptr(new QCNetworkReplyPrivate(this, request, method, mode, requestBody))
{
    Q_D(QCNetworkReply);

    // 配置 curl 选项
    if (!d->configureCurlOptions()) {
        d->setError(NetworkError::InvalidRequest,
                   QStringLiteral("Failed to configure curl options"));
        d->setState(ReplyState::Error);
    }
}

QCNetworkReply::~QCNetworkReply()
{
    Q_D(QCNetworkReply);

    // 如果正在运行，先取消
    if (d->state == ReplyState::Running || d->state == ReplyState::Paused) {
        cancel();
    }

    // 删除私有实现
    delete d;
}

// ========================================================================
// 执行控制
// ========================================================================

void QCNetworkReply::execute()
{
    Q_D(QCNetworkReply);

    if (d->state == ReplyState::Running || d->state == ReplyState::Paused) {
        qWarning() << "QCNetworkReply::execute() called while already running";
        return;
    }

    // ========================================================================
    // 缓存集成 - 在发起网络请求前检查缓存
    // ========================================================================
    QCNetworkAccessManager *manager = qobject_cast<QCNetworkAccessManager*>(parent());
    QCNetworkCache *cache = manager ? manager->cache() : nullptr;
    if (cache) {
        QCNetworkCachePolicy policy = d->request.cachePolicy();

        switch (policy) {
            case QCNetworkCachePolicy::OnlyNetwork:
                // 仅网络：跳过缓存检查
                break;

            case QCNetworkCachePolicy::OnlyCache:
                // 仅缓存：不发起网络请求
                if (!loadFromCache(true)) {
                    // 使用 QPointer 防止在异步执行期间对象被删除
                    QPointer<QCNetworkReply> safeThis(this);
                    d->setError(NetworkError::InvalidRequest, "Cache miss with OnlyCache policy");
                    // 异步发射信号，避免信号在用户连接之前发射
                    QTimer::singleShot(0, this, [safeThis]() {
                        if (safeThis) {
                            safeThis->d_func()->setState(ReplyState::Error);
                        }
                    });
                }
                return;

            case QCNetworkCachePolicy::PreferCache:
                // 优先缓存：缓存命中则返回
                if (loadFromCache(false)) {
                    return;
                }
                break;

            case QCNetworkCachePolicy::AlwaysCache:
                // 总是缓存：忽略过期时间
                if (loadFromCache(true)) {
                    return;
                }
                break;

            case QCNetworkCachePolicy::PreferNetwork:
                // 优先网络：标记允许回退到缓存
                d->fallbackToCache = true;
                break;
        }
    }

    // ========================================================================
    // 应用 Cookie 配置（在 QCNetworkAccessManager 中设置）
    // ========================================================================
    // 注意：cookieFilePath 和 cookieMode 是在构造函数之后通过 d_func() 设置的，
    // 所以必须在 execute() 中应用，而不是在 configureCurlOptions() 中
    CURL *handle = d->curlManager.handle();
    if (handle && d->cookieMode != 0 && !d->cookieFilePath.isEmpty()) {
        QByteArray cookiePathBytes = d->cookieFilePath.toUtf8();

        // ReadOnly (0x1) 或 ReadWrite (0x3)：从文件读取 cookie
        if (d->cookieMode & 0x1) {
            curl_easy_setopt(handle, CURLOPT_COOKIEFILE, cookiePathBytes.constData());
        }

        // WriteOnly (0x2) 或 ReadWrite (0x3)：将 cookie 写入文件
        if (d->cookieMode & 0x2) {
            curl_easy_setopt(handle, CURLOPT_COOKIEJAR, cookiePathBytes.constData());
        }
    }

    d->setState(ReplyState::Running);

    if (d->executionMode == ExecutionMode::Async) {
        // 异步模式：注册到多句柄管理器
        QCCurlMultiManager::instance()->addReply(this);
        qDebug() << "QCNetworkReply::execute: Started async request for" << d->request.url();
    } else {
        // ========================================================================
        // 同步模式：阻塞执行（支持重试）
        // ========================================================================
        if (!handle) {
            d->setError(NetworkError::InvalidRequest,
                       QStringLiteral("Invalid curl handle"));
            d->setState(ReplyState::Error);
            return;
        }

        // 获取重试策略
        QCNetworkRetryPolicy policy = d->request.retryPolicy();

        // 重试循环（attemptCount 从 0 开始，表示首次尝试）
        while (true) {
            // 执行请求（阻塞调用）
            CURLcode result = curl_easy_perform(handle);

            // 检查 HTTP 状态码（即使 CURLcode 成功）
            long httpCode = 0;
            curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &httpCode);

            // 确定最终错误：优先使用 HTTP 错误，否则使用 curl 错误
            NetworkError error = NetworkError::NoError;
            QString errorMsg;

            if (result != CURLE_OK) {
                // libcurl 层面的错误
                error = fromCurlCode(result);
                errorMsg = QString::fromUtf8(curl_easy_strerror(result));
            } else if (httpCode >= 400) {
                // HTTP 错误（4xx, 5xx）
                error = fromHttpCode(httpCode);
                errorMsg = QString("HTTP error %1").arg(httpCode);
            }

            // 如果没有错误，标记为成功
            if (error == NetworkError::NoError) {
                qDebug() << "QCNetworkReply::execute: Sync request succeeded for"
                         << d->request.url() << "after" << d->attemptCount << "retries";
                d->setState(ReplyState::Finished);
                return;
            }

            // 检查是否应该重试
            if (policy.shouldRetry(error, d->attemptCount)) {
                // 增加重试计数
                d->attemptCount++;

                // 发射重试尝试信号
                emit retryAttempt(d->attemptCount, error);

                // 计算延迟时间
                auto delay = policy.delayForAttempt(d->attemptCount - 1);

                qDebug() << "QCNetworkReply::execute: Sync request failed, retrying after"
                         << delay.count() << "ms. Attempt" << d->attemptCount
                         << "Error:" << errorMsg;

                // 同步等待（阻塞当前线程）
                QThread::msleep(delay.count());

                // 重置缓冲区和状态（准备重试）
                d->bodyBuffer.clear();
                d->headerData.clear();
                d->headerMap.clear();
                d->bytesDownloaded = 0;
                d->bytesUploaded = 0;
                d->downloadTotal = -1;
                d->uploadTotal = -1;

                // 继续循环重试
                continue;
            }

            // 超过最大重试次数或错误不可重试
            qDebug() << "QCNetworkReply::execute: Sync request failed after"
                     << d->attemptCount << "attempts. Error:" << errorMsg;

            d->setError(error, errorMsg);
            d->setState(ReplyState::Error);
            return;
        }
    }
}

void QCNetworkReply::cancel()
{
    Q_D(QCNetworkReply);

    // 如果已经取消或已完成，不需要再操作
    if (d->state == ReplyState::Cancelled
        || d->state == ReplyState::Finished
        || d->state == ReplyState::Error) {
        return;
    }

    // 异步模式：从多句柄管理器移除（Running/Paused 状态）
    if (d->executionMode == ExecutionMode::Async
        && (d->state == ReplyState::Running || d->state == ReplyState::Paused)) {
        // ⚠️ cancel 可能在 libcurl 回调栈内触发（例如在 downloadProgress 槽函数中）。
        // 直接调用 curl_multi_remove_handle 会引入重入风险，并可能触发 CURLM_BAD_EASY_HANDLE。
        // 这里延迟到事件循环中移除，避免与 curl_multi_socket_action 重叠。
        auto *multiManager = QCCurlMultiManager::instance();
        QPointer<QCNetworkReply> safeThis(this);
        QMetaObject::invokeMethod(
            multiManager,
            [multiManager, safeThis]() {
                if (safeThis) {
                    multiManager->removeReply(safeThis.data());
                }
            },
            Qt::QueuedConnection);
    }
    // 同步模式：无法真正取消阻塞的 curl_easy_perform()

    // 取消属于可观测错误语义：外部应能稳定区分“用户取消”与“空 body / 尚无数据”等情况
    d->setError(NetworkError::OperationCancelled, QCurl::errorString(NetworkError::OperationCancelled));

    // 设置取消状态（这会发射 cancelled 信号）
    // 注意：即使在 Idle 状态（重试延迟期间）也允许取消
    d->setState(ReplyState::Cancelled);
}

void QCNetworkReply::pause()
{
    pause(PauseMode::All);
}

void QCNetworkReply::pause(PauseMode mode)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, mode]() { pause(mode); }, Qt::QueuedConnection);
        return;
    }

    Q_D(QCNetworkReply);

    if (d->executionMode == ExecutionMode::Sync) {
        qWarning() << "QCNetworkReply::pause: Sync mode does not support transfer pause/resume";
        return;
    }

    if (d->state != ReplyState::Running) {
        return;
    }

    CURL *handle = d->curlManager.handle();
    if (handle) {
        int flags = CURLPAUSE_ALL;
        switch (mode) {
        case PauseMode::Recv:
            flags = CURLPAUSE_RECV;
            break;
        case PauseMode::Send:
            flags = CURLPAUSE_SEND;
            break;
        case PauseMode::All:
            flags = CURLPAUSE_ALL;
            break;
        }

        auto *multiManager = QCCurlMultiManager::instance();
        CURLcode result = CURLE_OK;
        if (QThread::currentThread() == multiManager->thread()) {
            result = curl_easy_pause(handle, flags);
        } else {
            QMetaObject::invokeMethod(
                multiManager,
                [handle, flags, &result]() { result = curl_easy_pause(handle, flags); },
                Qt::BlockingQueuedConnection);
        }

        if (result != CURLE_OK) {
            qWarning() << "QCNetworkReply::pause: curl_easy_pause failed:"
                       << curl_easy_strerror(result);
            return;
        }

        d->setState(ReplyState::Paused);
    }
}

void QCNetworkReply::resume()
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this]() { resume(); }, Qt::QueuedConnection);
        return;
    }

    Q_D(QCNetworkReply);

    if (d->executionMode == ExecutionMode::Sync) {
        qWarning() << "QCNetworkReply::resume: Sync mode does not support transfer pause/resume";
        return;
    }

    if (d->state != ReplyState::Paused) {
        return;
    }

    CURL *handle = d->curlManager.handle();
    if (handle) {
        auto *multiManager = QCCurlMultiManager::instance();
        CURLcode result = CURLE_OK;
        if (QThread::currentThread() == multiManager->thread()) {
            result = curl_easy_pause(handle, CURLPAUSE_CONT);
        } else {
            QMetaObject::invokeMethod(
                multiManager,
                [handle, &result]() { result = curl_easy_pause(handle, CURLPAUSE_CONT); },
                Qt::BlockingQueuedConnection);
        }

        if (result != CURLE_OK) {
            qWarning() << "QCNetworkReply::resume: curl_easy_pause failed:"
                       << curl_easy_strerror(result);
            return;
        }

        d->setState(ReplyState::Running);
        multiManager->wakeup();
    }
}

// ========================================================================
// 数据访问（现代 C++17 风格）
// ========================================================================

std::optional<QByteArray> QCNetworkReply::readAll() const
{
    Q_D(const QCNetworkReply);

    if (d->bodyBuffer.isEmpty()) {
        // 约定：在“终态且响应体为空”的场景下，返回空 QByteArray（而不是 std::nullopt），
        // 否则会把“空 body”与“尚无数据可读”混为一谈，导致可观测层面不可区分。
        if (d->state == ReplyState::Finished
            || d->state == ReplyState::Error
            || d->state == ReplyState::Cancelled) {
            return QByteArray();
        }
        return std::nullopt;
    }

    // 注意：这会清空缓冲区（对异步和同步模式都适用）
    return const_cast<QCByteDataBuffer&>(d->bodyBuffer).readAll();
}

std::optional<QByteArray> QCNetworkReply::readBody() const
{
    Q_D(const QCNetworkReply);
    
    // 安全检查：错误状态或未完成状态不应读取 body
    if (!d || d->state == ReplyState::Error || d->state == ReplyState::Idle) {
        qWarning() << "QCNetworkReply::readBody: Invalid state" 
                   << (d ? static_cast<int>(d->state) : -1);
        return std::nullopt;
    }
    
    return readAll();  // 别名方法
}

QList<RawHeaderPair> QCNetworkReply::rawHeaders() const
{
    Q_D(const QCNetworkReply);
    
    // 安全检查：错误状态或未完成状态下 headers 可能未初始化
    if (!d || d->state == ReplyState::Error || d->state == ReplyState::Idle) {
        qWarning() << "QCNetworkReply::rawHeaders: Invalid state" 
                   << (d ? static_cast<int>(d->state) : -1);
        return QList<RawHeaderPair>();  // 返回空列表而非崩溃
    }

    QList<RawHeaderPair> list;
    for (auto it = d->headerMap.cbegin(); it != d->headerMap.cend(); ++it) {
        list.append(qMakePair(it.key().toUtf8(), it.value().toUtf8()));
    }
    return list;
}

QByteArray QCNetworkReply::rawHeaderData() const
{
    Q_D(const QCNetworkReply);
    
    // 安全检查
    if (!d || d->state == ReplyState::Error || d->state == ReplyState::Idle) {
        qWarning() << "QCNetworkReply::rawHeaderData: Invalid state";
        return QByteArray();
    }
    
    return d->headerData;
}

QUrl QCNetworkReply::url() const
{
    Q_D(const QCNetworkReply);
    return d->request.url();
}

qint64 QCNetworkReply::bytesAvailable() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->bodyBuffer.byteAmount();
}

// ========================================================================
// 状态查询
// ========================================================================

ReplyState QCNetworkReply::state() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->state;
}

NetworkError QCNetworkReply::error() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->errorCode;
}

QString QCNetworkReply::errorString() const
{
    Q_D(const QCNetworkReply);
    return d->errorMessage;
}

bool QCNetworkReply::isFinished() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->state == ReplyState::Finished;
}

bool QCNetworkReply::isRunning() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->state == ReplyState::Running;
}

bool QCNetworkReply::isPaused() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->state == ReplyState::Paused;
}

qint64 QCNetworkReply::bytesReceived() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->bytesDownloaded;
}

qint64 QCNetworkReply::bytesTotal() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->downloadTotal;
}

// ========================================================================
// 同步模式专用 API
// ========================================================================

void QCNetworkReply::setRequestBody(const QByteArray &data)
{
    Q_D(QCNetworkReply);
    d->requestBody = data;
}

void QCNetworkReply::setWriteCallback(const DataFunction &func)
{
    Q_D(QCNetworkReply);
    d->writeCallback = func;
}

void QCNetworkReply::setHeaderCallback(const DataFunction &func)
{
    Q_D(QCNetworkReply);
    d->headerCallback = func;
}

void QCNetworkReply::setSeekCallback(const SeekFunction &func)
{
    Q_D(QCNetworkReply);
    d->seekCallback = func;
}

void QCNetworkReply::setProgressCallback(const ProgressFunction &func)
{
    Q_D(QCNetworkReply);
    d->progressCallback = func;
}

// ========================================================================
// 公共槽
// ========================================================================

void QCNetworkReply::deleteLater()
{
    QObject::deleteLater();
}

// ============================================================================
// 缓存集成私有方法实现
// ============================================================================

bool QCNetworkReply::loadFromCache(bool ignoreExpiry)
{
    Q_D(QCNetworkReply);

    QCNetworkAccessManager *manager = qobject_cast<QCNetworkAccessManager*>(parent());
    QCNetworkCache *cache = manager ? manager->cache() : nullptr;
    if (!cache) return false;

    auto meta = cache->metadata(d->request.url());

    // 检查缓存有效性
    if (!ignoreExpiry && !meta.isValid()) {
        return false;
    }

    QByteArray data = cache->data(d->request.url());
    if (data.isEmpty()) {
        return false;
    }

    // 模拟网络请求行为
    d->bodyBuffer.append(data);
    d->setState(ReplyState::Finished);
    d->errorCode = NetworkError::NoError;

    // 异步发射信号（与网络请求一致）
    QTimer::singleShot(0, this, [this]() {
        emit readyRead();
        emit finished();
    });

    return true;
}

QMap<QByteArray, QByteArray> QCNetworkReply::parseResponseHeaders()
{
    Q_D(QCNetworkReply);

    QMap<QByteArray, QByteArray> headers;
    QList<QByteArray> lines = d->headerData.split('\n');

    for (const QByteArray &line : lines) {
        int colonPos = line.indexOf(':');
        if (colonPos > 0) {
            headers[line.left(colonPos).trimmed()] = line.mid(colonPos + 1).trimmed();
        }
    }

    return headers;
}

} // namespace QCurl
