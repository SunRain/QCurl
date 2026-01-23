#include "QCNetworkReply.h"

#include "CurlFeatureProbe.h"
#include "QCCurlMultiManager.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkCache.h"
#include "QCNetworkCachePolicy.h"
#include "QCNetworkConnectionPoolManager.h"
#include "QCNetworkError.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkLogRedaction.h"
#include "QCNetworkLogger.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkReply_p.h"
#include "QCNetworkRetryPolicy.h"
#include "QCNetworkSslConfig.h"
#include "QCNetworkTimeoutConfig.h"
#include "QCUtility.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo> // 用于检查系统 CA 证书路径
#include <QIODevice>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <cstdio>
#include <ctime>
#include <limits>

namespace QCurl {

namespace {

bool isCapabilityRelatedCurlError(CURLcode code)
{
    return code == CURLE_UNKNOWN_OPTION || code == CURLE_NOT_BUILT_IN;
}

#ifdef QCURL_ENABLE_TEST_HOOKS
bool shouldForceCapabilityErrorForOption(const char *optionName)
{
    const QByteArray raw = qgetenv("QCURL_TEST_FORCE_CAPABILITY_ERROR");
    if (raw.isEmpty()) {
        return false;
    }

    const QByteArray trimmed = raw.trimmed();
    if (trimmed == "1" || trimmed == "all") {
        return true;
    }

    const QList<QByteArray> parts = raw.split(',');
    for (const QByteArray &p : parts) {
        if (p.trimmed() == optionName) {
            return true;
        }
    }
    return false;
}
#endif

template<typename T>
CURLcode curlEasySetoptWithTestHook(CURL *handle, CURLoption option, const char *optionName, T value)
{
#ifdef QCURL_ENABLE_TEST_HOOKS
    if (shouldForceCapabilityErrorForOption(optionName)) {
        return CURLE_NOT_BUILT_IN;
    }
#endif
    return curl_easy_setopt(handle, option, value);
}

void appendCapabilityWarning(QCNetworkReplyPrivate *d, const QString &message)
{
    if (!d) {
        return;
    }

    d->capabilityWarnings.append(message);
    qWarning() << "QCNetworkReply capability warning:" << message;
}

bool setOptionalLongOption(
    QCNetworkReplyPrivate *d, CURL *handle, CURLoption option, const char *optionName, long value)
{
    if (!handle) {
        return false;
    }

    const CURLcode rc = curl_easy_setopt(handle, option, value);
    if (rc == CURLE_OK) {
        return true;
    }

    if (isCapabilityRelatedCurlError(rc)) {
        appendCapabilityWarning(d,
                                QStringLiteral("libcurl 不支持 %1（%2）")
                                    .arg(QString::fromUtf8(optionName))
                                    .arg(QString::fromUtf8(curl_easy_strerror(rc))));
        return false;
    }

    appendCapabilityWarning(d,
                            QStringLiteral("设置 %1 失败（%2）")
                                .arg(QString::fromUtf8(optionName))
                                .arg(QString::fromUtf8(curl_easy_strerror(rc))));
    return false;
}

bool setOptionalStringOption(QCNetworkReplyPrivate *d,
                             CURL *handle,
                             CURLoption option,
                             const char *optionName,
                             const QByteArray &value)
{
    if (!handle) {
        return false;
    }

    const CURLcode rc = curl_easy_setopt(handle, option, value.constData());
    if (rc == CURLE_OK) {
        return true;
    }

    if (isCapabilityRelatedCurlError(rc)) {
        appendCapabilityWarning(d,
                                QStringLiteral("libcurl 不支持 %1（%2）")
                                    .arg(QString::fromUtf8(optionName))
                                    .arg(QString::fromUtf8(curl_easy_strerror(rc))));
        return false;
    }

    appendCapabilityWarning(d,
                            QStringLiteral("设置 %1 失败（%2）")
                                .arg(QString::fromUtf8(optionName))
                                .arg(QString::fromUtf8(curl_easy_strerror(rc))));
    return false;
}

bool setOptionalOffTOption(QCNetworkReplyPrivate *d,
                           CURL *handle,
                           CURLoption option,
                           const char *optionName,
                           curl_off_t value)
{
    if (!handle) {
        return false;
    }

    const CURLcode rc = curl_easy_setopt(handle, option, value);
    if (rc == CURLE_OK) {
        return true;
    }

    if (isCapabilityRelatedCurlError(rc)) {
        appendCapabilityWarning(d,
                                QStringLiteral("libcurl 不支持 %1（%2）")
                                    .arg(QString::fromUtf8(optionName))
                                    .arg(QString::fromUtf8(curl_easy_strerror(rc))));
        return false;
    }

    appendCapabilityWarning(d,
                            QStringLiteral("设置 %1 失败（%2）")
                                .arg(QString::fromUtf8(optionName))
                                .arg(QString::fromUtf8(curl_easy_strerror(rc))));
    return false;
}

curl_slist *buildSlistFromStrings(const QStringList &entries)
{
    curl_slist *list = nullptr;
    for (const QString &e : entries) {
        const QByteArray bytes = e.toUtf8();
        list                   = curl_slist_append(list, bytes.constData());
    }
    return list;
}

bool setOptionalSlistOption(QCNetworkReplyPrivate *d,
                            CURL *handle,
                            CURLoption option,
                            const char *optionName,
                            curl_slist *list)
{
    if (!handle) {
        return false;
    }

    const CURLcode rc = curl_easy_setopt(handle, option, list);
    if (rc == CURLE_OK) {
        return true;
    }

    if (isCapabilityRelatedCurlError(rc)) {
        appendCapabilityWarning(d,
                                QStringLiteral("libcurl 不支持 %1（%2）")
                                    .arg(QString::fromUtf8(optionName))
                                    .arg(QString::fromUtf8(curl_easy_strerror(rc))));
        return false;
    }

    appendCapabilityWarning(d,
                            QStringLiteral("设置 %1 失败（%2）")
                                .arg(QString::fromUtf8(optionName))
                                .arg(QString::fromUtf8(curl_easy_strerror(rc))));
    return false;
}

std::optional<long> toCurlSslVersionMin(QCNetworkTlsVersion version)
{
    switch (version) {
        case QCNetworkTlsVersion::Default:
            return std::nullopt;
        case QCNetworkTlsVersion::Tls1_0:
            return static_cast<long>(CURL_SSLVERSION_TLSv1);
        case QCNetworkTlsVersion::Tls1_1:
            return static_cast<long>(CURL_SSLVERSION_TLSv1_1);
        case QCNetworkTlsVersion::Tls1_2:
            return static_cast<long>(CURL_SSLVERSION_TLSv1_2);
        case QCNetworkTlsVersion::Tls1_3:
#ifdef CURL_SSLVERSION_TLSv1_3
            return static_cast<long>(CURL_SSLVERSION_TLSv1_3);
#else
            return std::nullopt;
#endif
    }
    return std::nullopt;
}

namespace {

thread_local int s_curlCallbackDepth = 0;

class CurlCallbackScope
{
public:
    CurlCallbackScope() { ++s_curlCallbackDepth; }
    ~CurlCallbackScope() { --s_curlCallbackDepth; }
};

[[nodiscard]] bool isInCurlCallback()
{
    return s_curlCallbackDepth > 0;
}

} // namespace

std::optional<std::chrono::milliseconds> parseRetryAfterDelay(const QMap<QString, QString> &headers)
{
    for (auto it = headers.cbegin(); it != headers.cend(); ++it) {
        if (it.key().compare(QStringLiteral("Retry-After"), Qt::CaseInsensitive) != 0) {
            continue;
        }

        const QString raw = it.value().trimmed();
        if (raw.isEmpty()) {
            return std::nullopt;
        }

        bool ok              = false;
        const qint64 seconds = raw.toLongLong(&ok);
        if (ok) {
            if (seconds < 0) {
                return std::nullopt;
            }
            return std::chrono::milliseconds(seconds * 1000);
        }

        const QByteArray dateBytes = raw.toUtf8();
        const time_t parsed        = curl_getdate(dateBytes.constData(), nullptr);
        if (parsed < 0) {
            return std::nullopt;
        }

        const time_t now = std::time(nullptr);
        if (parsed <= now) {
            return std::chrono::milliseconds(0);
        }

        const qint64 deltaSeconds = static_cast<qint64>(parsed) - static_cast<qint64>(now);
        if (deltaSeconds > (std::numeric_limits<qint64>::max() / 1000)) {
            return std::chrono::milliseconds(std::numeric_limits<qint64>::max());
        }

        return std::chrono::milliseconds(deltaSeconds * 1000);
    }
    return std::nullopt;
}

} // namespace

// ==================
// QCNetworkReplyPrivate 实现
// ==================

QCNetworkReplyPrivate::QCNetworkReplyPrivate(QCNetworkReply *q,
                                             const QCNetworkRequest &req,
                                             HttpMethod method,
                                             ExecutionMode mode,
                                             const QByteArray &body)
    : q_ptr(q)
    , request(req)
    , httpMethod(method)
    , executionMode(mode)
    , requestBody(body)
    , multiProcessor(nullptr)
    , state(ReplyState::Idle)
    , errorCode(NetworkError::NoError)
    , bytesDownloaded(0)
    , bytesUploaded(0)
    , downloadTotal(-1)
    , uploadTotal(-1)
    , attemptCount(0)
    , cookieMode(0)
{
    const qint64 limitBytes = request.backpressureLimitBytes();
    if (limitBytes > 0) {
        backpressureLimitBytes = limitBytes;
        const qint64 resumeBytes = request.backpressureResumeBytes();
        if (resumeBytes > 0 && resumeBytes < limitBytes) {
            backpressureResumeBytes = resumeBytes;
        } else {
            backpressureResumeBytes = limitBytes / 2;
        }
    }
}

QCNetworkReplyPrivate::~QCNetworkReplyPrivate()
{
    // 如果正在运行（异步模式），从多句柄管理器移除
    // 注意：cancel() 会在 ~QCNetworkReply() 中被调用，所以这里通常不需要额外处理
    // 但为安全起见，如果对象直接销毁且状态仍为 Running，确保清理
    if (executionMode == ExecutionMode::Async
        && (state == ReplyState::Running || state == ReplyState::Paused) && q_ptr) {
        if (QThread::currentThread() == q_ptr->thread()) {
            QCCurlMultiManager::instance()->removeReply(q_ptr);
        } else {
            qWarning() << "QCNetworkReplyPrivate: reply 在非所属线程销毁，无法安全从 multi engine "
                          "移除（请使用 deleteLater 或在 reply 线程销毁）";
        }
    }

    if (ownedUploadFile) {
        if (ownedUploadFile->isOpen()) {
            ownedUploadFile->close();
        }
        // 事件循环析构，避免手动 delete QObject
        ownedUploadFile->deleteLater();
        ownedUploadFile = nullptr;
    }

    if (resolveSlist) {
        curl_slist_free_all(resolveSlist);
        resolveSlist = nullptr;
    }

    if (connectToSlist) {
        curl_slist_free_all(connectToSlist);
        connectToSlist = nullptr;
    }
}

bool QCNetworkReplyPrivate::configureCurlOptions()
{
    CURL *handle = curlManager.handle();
    if (!handle) {
        qCritical() << "QCNetworkReply: curl handle is null";
        return false;
    }

    // ==================
    // 1. 基础配置
    // ==================

    // 设置 URL
    QByteArray urlBytes = request.url().toString().toUtf8();
    curl_easy_setopt(handle, CURLOPT_URL, urlBytes.constData());

    // 设置私有数据（用于回调中识别对象）
    curl_easy_setopt(handle, CURLOPT_PRIVATE, this);

    // 跟随重定向
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, request.followLocation() ? 1L : 0L);

    // ==================
    // 1.1 重定向策略（M1）
    // ==================

    if (request.followLocation()) {
        if (const auto maxRedirects = request.maxRedirects(); maxRedirects.has_value()) {
            setOptionalLongOption(this,
                                  handle,
                                  CURLOPT_MAXREDIRS,
                                  "CURLOPT_MAXREDIRS",
                                  static_cast<long>(maxRedirects.value()));
        }

        switch (request.postRedirectPolicy()) {
            case QCNetworkPostRedirectPolicy::Default:
                break;
            case QCNetworkPostRedirectPolicy::KeepPost301:
                setOptionalLongOption(this,
                                      handle,
                                      CURLOPT_POSTREDIR,
                                      "CURLOPT_POSTREDIR",
                                      CURL_REDIR_POST_301);
                break;
            case QCNetworkPostRedirectPolicy::KeepPost302:
                setOptionalLongOption(this,
                                      handle,
                                      CURLOPT_POSTREDIR,
                                      "CURLOPT_POSTREDIR",
                                      CURL_REDIR_POST_302);
                break;
            case QCNetworkPostRedirectPolicy::KeepPost303:
                setOptionalLongOption(this,
                                      handle,
                                      CURLOPT_POSTREDIR,
                                      "CURLOPT_POSTREDIR",
                                      CURL_REDIR_POST_303);
                break;
            case QCNetworkPostRedirectPolicy::KeepPostAll:
                setOptionalLongOption(this,
                                      handle,
                                      CURLOPT_POSTREDIR,
                                      "CURLOPT_POSTREDIR",
                                      CURL_REDIR_POST_ALL);
                break;
        }

        if (request.autoRefererEnabled()) {
            setOptionalLongOption(this, handle, CURLOPT_AUTOREFERER, "CURLOPT_AUTOREFERER", 1L);
        }
    }

    // ==================
    // 1.3 网络路径与 DNS 控制（M4）
    // ==================

    if (const auto ipResolveOpt = request.ipResolve(); ipResolveOpt.has_value()) {
        long ipResolveValue = CURL_IPRESOLVE_WHATEVER;
        switch (ipResolveOpt.value()) {
            case QCNetworkIpResolve::Any:
                ipResolveValue = CURL_IPRESOLVE_WHATEVER;
                break;
            case QCNetworkIpResolve::Ipv4:
                ipResolveValue = CURL_IPRESOLVE_V4;
                break;
            case QCNetworkIpResolve::Ipv6:
                ipResolveValue = CURL_IPRESOLVE_V6;
                break;
        }

        setOptionalLongOption(this, handle, CURLOPT_IPRESOLVE, "CURLOPT_IPRESOLVE", ipResolveValue);
    }

    if (const auto heOpt = request.happyEyeballsTimeout(); heOpt.has_value()) {
        setOptionalLongOption(this,
                              handle,
                              CURLOPT_HAPPY_EYEBALLS_TIMEOUT_MS,
                              "CURLOPT_HAPPY_EYEBALLS_TIMEOUT_MS",
                              static_cast<long>(heOpt.value().count()));
    }

    if (const auto ifaceOpt = request.networkInterface(); ifaceOpt.has_value()) {
        interfaceBytes = ifaceOpt.value().toUtf8();
        setOptionalStringOption(this, handle, CURLOPT_INTERFACE, "CURLOPT_INTERFACE", interfaceBytes);
    }

    if (const auto localPortOpt = request.localPort(); localPortOpt.has_value()) {
        setOptionalLongOption(this,
                              handle,
                              CURLOPT_LOCALPORT,
                              "CURLOPT_LOCALPORT",
                              static_cast<long>(localPortOpt.value()));
    }

    if (const auto localPortRangeOpt = request.localPortRange(); localPortRangeOpt.has_value()) {
        setOptionalLongOption(this,
                              handle,
                              CURLOPT_LOCALPORTRANGE,
                              "CURLOPT_LOCALPORTRANGE",
                              static_cast<long>(localPortRangeOpt.value()));
    }

    if (const auto resolveOverrideOpt = request.resolveOverride(); resolveOverrideOpt.has_value()) {
        resolveSlist = buildSlistFromStrings(resolveOverrideOpt.value());
        if (!resolveSlist) {
            appendCapabilityWarning(this, QStringLiteral("CURLOPT_RESOLVE: 构造参数列表失败"));
        } else {
            if (!setOptionalSlistOption(this,
                                        handle,
                                        CURLOPT_RESOLVE,
                                        "CURLOPT_RESOLVE",
                                        resolveSlist)) {
                curl_slist_free_all(resolveSlist);
                resolveSlist = nullptr;
            }
        }
    }

    if (const auto connectToOpt = request.connectTo(); connectToOpt.has_value()) {
        connectToSlist = buildSlistFromStrings(connectToOpt.value());
        if (!connectToSlist) {
            appendCapabilityWarning(this, QStringLiteral("CURLOPT_CONNECT_TO: 构造参数列表失败"));
        } else {
            if (!setOptionalSlistOption(this,
                                        handle,
                                        CURLOPT_CONNECT_TO,
                                        "CURLOPT_CONNECT_TO",
                                        connectToSlist)) {
                curl_slist_free_all(connectToSlist);
                connectToSlist = nullptr;
            }
        }
    }

    if (const auto dnsServersOpt = request.dnsServers(); dnsServersOpt.has_value()) {
        dnsServersBytes = dnsServersOpt.value().join(QStringLiteral(",")).toUtf8();
        setOptionalStringOption(this,
                                handle,
                                CURLOPT_DNS_SERVERS,
                                "CURLOPT_DNS_SERVERS",
                                dnsServersBytes);
    }

    if (const auto dohUrlOpt = request.dohUrl(); dohUrlOpt.has_value()) {
        dohUrlBytes = dohUrlOpt.value().toString().toUtf8();
        setOptionalStringOption(this, handle, CURLOPT_DOH_URL, "CURLOPT_DOH_URL", dohUrlBytes);
    }

    // ==================
    // 1.4 协议白名单（M5，安全）
    // ==================

    const QCUnsupportedSecurityOptionPolicy securityPolicy = request
                                                                 .unsupportedSecurityOptionPolicy();

    if (const auto allowedOpt = request.allowedProtocols(); allowedOpt.has_value()) {
#ifdef CURLOPT_PROTOCOLS_STR
        allowedProtocolsBytes = allowedOpt.value().join(QStringLiteral(",")).toUtf8();
        const CURLcode rc     = curlEasySetoptWithTestHook(handle,
                                                       CURLOPT_PROTOCOLS_STR,
                                                       "CURLOPT_PROTOCOLS_STR",
                                                       allowedProtocolsBytes.constData());
        if (rc != CURLE_OK) {
            if (isCapabilityRelatedCurlError(rc)) {
                const QString msg = QStringLiteral("libcurl 不支持 CURLOPT_PROTOCOLS_STR（%1）")
                                        .arg(QString::fromUtf8(curl_easy_strerror(rc)));
                if (securityPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                    setError(NetworkError::InvalidRequest, msg);
                    return false;
                }
                appendCapabilityWarning(this, msg);
            } else {
                setError(NetworkError::InvalidRequest,
                         QStringLiteral("设置 CURLOPT_PROTOCOLS_STR 失败（%1）")
                             .arg(QString::fromUtf8(curl_easy_strerror(rc))));
                return false;
            }
        }
#else
        const QString msg = QStringLiteral("当前构建的 libcurl 不支持 CURLOPT_PROTOCOLS_STR");
        if (securityPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
            setError(NetworkError::InvalidRequest, msg);
            return false;
        }
        appendCapabilityWarning(this, msg);
#endif
    }

    if (const auto redirOpt = request.allowedRedirectProtocols(); redirOpt.has_value()) {
#ifdef CURLOPT_REDIR_PROTOCOLS_STR
        allowedRedirectProtocolsBytes = redirOpt.value().join(QStringLiteral(",")).toUtf8();
        const CURLcode rc             = curlEasySetoptWithTestHook(handle,
                                                       CURLOPT_REDIR_PROTOCOLS_STR,
                                                       "CURLOPT_REDIR_PROTOCOLS_STR",
                                                       allowedRedirectProtocolsBytes.constData());
        if (rc != CURLE_OK) {
            if (isCapabilityRelatedCurlError(rc)) {
                const QString msg = QStringLiteral(
                                        "libcurl 不支持 CURLOPT_REDIR_PROTOCOLS_STR（%1）")
                                        .arg(QString::fromUtf8(curl_easy_strerror(rc)));
                if (securityPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                    setError(NetworkError::InvalidRequest, msg);
                    return false;
                }
                appendCapabilityWarning(this, msg);
            } else {
                setError(NetworkError::InvalidRequest,
                         QStringLiteral("设置 CURLOPT_REDIR_PROTOCOLS_STR 失败（%1）")
                             .arg(QString::fromUtf8(curl_easy_strerror(rc))));
                return false;
            }
        }
#else
        const QString msg = QStringLiteral("当前构建的 libcurl 不支持 CURLOPT_REDIR_PROTOCOLS_STR");
        if (securityPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
            setError(NetworkError::InvalidRequest, msg);
            return false;
        }
        appendCapabilityWarning(this, msg);
#endif
    }

    // 多线程安全（禁用信号处理）
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);

    // ==================
    // 1.2 流式上传源准备（M2）
    // ==================

    // 清理上一次配置残留（正常情况下构造期只会调用一次，但保持幂等）
    uploadDevice            = nullptr;
    uploadDeviceSeekable    = false;
    uploadDeviceBasePos     = 0;
    uploadBodySizeBytes     = -1;
    uploadBytesRead         = 0;
    hasUploadErrorOverride  = false;
    uploadErrorOverrideCode = NetworkError::NoError;
    uploadErrorOverrideMessage.clear();

    if (ownedUploadFile) {
        if (ownedUploadFile->isOpen()) {
            ownedUploadFile->close();
        }
        // 事件循环析构，避免手动 delete QObject
        ownedUploadFile->deleteLater();
        ownedUploadFile = nullptr;
    }

    QIODevice *reqUploadDevice = request.uploadDevice();
    if (!reqUploadDevice) {
        const auto filePathOpt = request.uploadFilePath();
        if (filePathOpt.has_value()) {
            ownedUploadFile = new QFile(filePathOpt.value(), q_ptr);
            if (!ownedUploadFile->open(QIODevice::ReadOnly)) {
                setError(NetworkError::InvalidRequest,
                         QStringLiteral("uploadFile: 无法打开文件：%1")
                             .arg(ownedUploadFile->errorString()));
                ownedUploadFile->deleteLater();
                ownedUploadFile = nullptr;
                return false;
            }
            reqUploadDevice = ownedUploadFile;
        }
    }

    if (reqUploadDevice) {
        if (!reqUploadDevice->isReadable()) {
            setError(NetworkError::InvalidRequest,
                     QStringLiteral("uploadDevice: 源 QIODevice 不可读"));
            return false;
        }

        // 异步模式下，curl 回调运行在 QCCurlMultiManager 线程；必须保证线程一致性。
        if (executionMode == ExecutionMode::Async) {
            auto *multiManager = QCCurlMultiManager::instance();
            if (q_ptr && q_ptr->thread() != multiManager->thread()) {
                setError(
                    NetworkError::InvalidRequest,
                    QStringLiteral(
                        "uploadDevice: Reply 线程与 MultiManager 线程不一致，无法安全流式读取"));
                return false;
            }
        }

        if (q_ptr && reqUploadDevice->thread() != q_ptr->thread()) {
            setError(NetworkError::InvalidRequest,
                     QStringLiteral("uploadDevice: 源 QIODevice 与 Reply 不在同一线程"));
            return false;
        }

        uploadDevice         = reqUploadDevice;
        uploadDeviceBasePos  = reqUploadDevice->pos();
        uploadDeviceSeekable = !reqUploadDevice->isSequential();

        if (const auto sizeOpt = request.uploadBodySizeBytes(); sizeOpt.has_value()) {
            uploadBodySizeBytes = sizeOpt.value();
        } else if (uploadDeviceSeekable) {
            const qint64 totalSize = reqUploadDevice->size();
            if (totalSize >= 0 && totalSize >= uploadDeviceBasePos) {
                uploadBodySizeBytes = totalSize - uploadDeviceBasePos;
            }
        }

        if (uploadBodySizeBytes < 0) {
            const bool allowChunkedPost = (httpMethod == HttpMethod::Post)
                                          && request.allowChunkedUploadForPost();
            if (!allowChunkedPost) {
                setError(
                    NetworkError::InvalidRequest,
                    QStringLiteral(
                        "uploadDevice: 未指定 sizeBytes，且无法从设备推导长度（如需 unknown size "
                        "的 POST，请显式启用 request.setAllowChunkedUploadForPost(true)）"));
                return false;
            }

            if (request.httpVersion() != QCNetworkHttpVersion::Http1_1) {
                setError(NetworkError::InvalidRequest,
                         QStringLiteral("uploadDevice: unknown size 的 POST chunked 仅支持 "
                                        "HTTP/1.1（请改为 Http1_1 或指定 sizeBytes）"));
                return false;
            }
        }

        if (executionMode == ExecutionMode::Async && uploadDevice && q_ptr) {
            QObject::connect(uploadDevice,
                             &QIODevice::readyRead,
                             q_ptr,
                             [this]() { resumeSendFromUploadSourceIfNeeded(); });
        }
    }

    if (uploadDevice && httpMethod != HttpMethod::Post && httpMethod != HttpMethod::Put) {
        setError(NetworkError::InvalidRequest,
                 QStringLiteral("uploadDevice/uploadFile 仅支持 PUT/POST（当前方法未支持）"));
        return false;
    }

    if (uploadDevice && request.retryPolicy().isEnabled() && !uploadDeviceSeekable) {
        setError(NetworkError::InvalidRequest,
                 QStringLiteral("uploadDevice: non-seekable body 不支持自动重试（需要重发 "
                                "body；请关闭 retryPolicy 或使用 seekable 来源）"));
        return false;
    }

    // ==================
    // 2. HTTP 方法配置
    // ==================

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
            if (uploadDevice) {
                curl_easy_setopt(handle,
                                 CURLOPT_READFUNCTION,
                                 QCNetworkReplyPrivate::curlReadCallback);
                curl_easy_setopt(handle, CURLOPT_READDATA, this);
                curl_easy_setopt(handle, CURLOPT_POSTFIELDS, nullptr);
                curl_easy_setopt(handle,
                                 CURLOPT_POSTFIELDSIZE_LARGE,
                                 static_cast<curl_off_t>(uploadBodySizeBytes));
            } else {
                curl_easy_setopt(handle, CURLOPT_POSTFIELDS, requestBody.constData());
                curl_easy_setopt(handle,
                                 CURLOPT_POSTFIELDSIZE,
                                 static_cast<long>(requestBody.size()));
            }
            break;

        case HttpMethod::Put:
            // PUT 请求：上传资源
            if (uploadDevice) {
                curl_easy_setopt(handle, CURLOPT_UPLOAD, 1L);
                curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "PUT");
                curl_easy_setopt(handle,
                                 CURLOPT_READFUNCTION,
                                 QCNetworkReplyPrivate::curlReadCallback);
                curl_easy_setopt(handle, CURLOPT_READDATA, this);
                curl_easy_setopt(handle,
                                 CURLOPT_INFILESIZE_LARGE,
                                 static_cast<curl_off_t>(uploadBodySizeBytes));
            } else {
                curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "PUT");
                if (!requestBody.isEmpty()) {
                    curl_easy_setopt(handle, CURLOPT_POSTFIELDS, requestBody.constData());
                    curl_easy_setopt(handle,
                                     CURLOPT_POSTFIELDSIZE,
                                     static_cast<long>(requestBody.size()));
                }
            }
            break;

        case HttpMethod::Delete:
            // DELETE 请求：删除资源
            curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "DELETE");
            // 支持 DELETE 携带请求体（RFC 允许，部分服务端依赖该语义）
            if (!requestBody.isEmpty()) {
                curl_easy_setopt(handle, CURLOPT_POSTFIELDS, requestBody.constData());
                curl_easy_setopt(handle,
                                 CURLOPT_POSTFIELDSIZE,
                                 static_cast<long>(requestBody.size()));
            }
            break;

        case HttpMethod::Patch:
            // PATCH 请求：部分更新
            // PATCH 必须使用 CUSTOMREQUEST，因为 libcurl 没有专门的 PATCH 选项
            curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "PATCH");
            if (!requestBody.isEmpty()) {
                curl_easy_setopt(handle, CURLOPT_POSTFIELDS, requestBody.constData());
                curl_easy_setopt(handle,
                                 CURLOPT_POSTFIELDSIZE,
                                 static_cast<long>(requestBody.size()));
            }
            break;

        default:
            qWarning() << "QCNetworkReply: Unknown HTTP method";
            return false;
    }

    // ==================
    // 2.1 Expect: 100-continue 等待超时（P1，可选）
    // ==================

    if (const auto timeoutOpt = request.expect100ContinueTimeout(); timeoutOpt.has_value()) {
        const bool isPutOrPost = httpMethod == HttpMethod::Put || httpMethod == HttpMethod::Post;
        const bool hasBody     = uploadDevice || !requestBody.isEmpty();
        if (!isPutOrPost || !hasBody) {
            appendCapabilityWarning(
                this,
                QStringLiteral(
                    "请求配置：Expect: 100-continue timeout 仅对 PUT/POST 且有 body 生效，已忽略"));
        } else {
            const long long timeoutMs = static_cast<long long>(timeoutOpt.value().count());
            if (timeoutMs < 0) {
                appendCapabilityWarning(
                    this,
                    QStringLiteral("请求配置：Expect: 100-continue timeout 必须 >= 0（已忽略）"));
            } else {
                long timeoutMsLong = 0;
                if (timeoutMs > static_cast<long long>(std::numeric_limits<long>::max())) {
                    timeoutMsLong = std::numeric_limits<long>::max();
                    appendCapabilityWarning(
                        this,
                        QStringLiteral(
                            "请求配置：Expect: 100-continue timeout 过大，已截断为 LONG_MAX ms"));
                } else {
                    timeoutMsLong = static_cast<long>(timeoutMs);
                }
                setOptionalLongOption(this,
                                      handle,
                                      CURLOPT_EXPECT_100_TIMEOUT_MS,
                                      "CURLOPT_EXPECT_100_TIMEOUT_MS",
                                      timeoutMsLong);
            }
        }
    }

    // ==================
    // 3. 自定义 HTTP Headers
    // ==================

    bool hasExplicitAuthorizationHeader  = false;
    bool hasExplicitRefererHeader        = false;
    bool hasExplicitAcceptEncodingHeader = false;
    bool hasSensitiveHeader              = false;
    QList<QByteArray> headerNames        = request.rawHeaderList();
    for (const QByteArray &headerName : headerNames) {
        const QByteArray normalizedName = headerName.trimmed().toLower();
        if (normalizedName == QByteArray("authorization")) {
            hasExplicitAuthorizationHeader = true;
            hasSensitiveHeader             = true;
        } else if (normalizedName == QByteArray("proxy-authorization")) {
            hasSensitiveHeader = true;
        } else if (normalizedName == QByteArray("cookie")) {
            hasSensitiveHeader = true;
        } else if (normalizedName == QByteArray("set-cookie")) {
            hasSensitiveHeader = true;
        } else if (normalizedName == QByteArray("referer")) {
            hasExplicitRefererHeader = true;
        } else if (normalizedName == QByteArray("accept-encoding")) {
            hasExplicitAcceptEncodingHeader = true;
        }
        QByteArray headerValue = request.rawHeader(headerName);
        // 格式化为 "Name: Value" 格式
        QString headerLine = QString::fromUtf8(headerName) + ": " + QString::fromUtf8(headerValue);
        curlManager.appendHeader(headerLine);
    }

    // 应用 header 列表
    if (curlManager.headerList()) {
        curl_easy_setopt(handle, CURLOPT_HTTPHEADER, curlManager.headerList());
    }

    // ==================
    // 3.1 Referer / 自动解压（M1）
    // ==================

    if (!hasExplicitRefererHeader) {
        const QString referer = request.referer();
        if (!referer.isEmpty()) {
            refererBytes = referer.toUtf8();
            setOptionalStringOption(this, handle, CURLOPT_REFERER, "CURLOPT_REFERER", refererBytes);
        }
    } else if (!request.referer().isEmpty()) {
        appendCapabilityWarning(
            this,
            QStringLiteral(
                "请求配置冲突：已显式设置 Referer header，将忽略 request.setReferer(...)"));
    }

    if (hasExplicitAcceptEncodingHeader) {
        if (request.autoDecompressionEnabled() || !request.acceptedEncodings().isEmpty()) {
            appendCapabilityWarning(this,
                                    QStringLiteral(
                                        "请求配置冲突：已显式设置 Accept-Encoding header，将忽略 "
                                        "autoDecompression/acceptedEncodings（不会自动解压）"));
        }
    } else if (request.autoDecompressionEnabled()) {
        const QStringList encodings = request.acceptedEncodings();
        if (!encodings.isEmpty()) {
            QStringList normalized;
            normalized.reserve(encodings.size());
            for (const QString &encoding : encodings) {
                const QString trimmed = encoding.trimmed();
                if (!trimmed.isEmpty()) {
                    normalized.append(trimmed);
                }
            }

            if (!normalized.isEmpty()) {
                acceptEncodingBytes = normalized.join(QLatin1Char(',')).toUtf8();
                setOptionalStringOption(this,
                                        handle,
                                        CURLOPT_ACCEPT_ENCODING,
                                        "CURLOPT_ACCEPT_ENCODING",
                                        acceptEncodingBytes);
            }
        } else {
            // 空字符串：让 libcurl 使用其内置支持的编码列表，并启用自动解压
            acceptEncodingBytes = QByteArray("");
            setOptionalStringOption(this,
                                    handle,
                                    CURLOPT_ACCEPT_ENCODING,
                                    "CURLOPT_ACCEPT_ENCODING",
                                    acceptEncodingBytes);
        }
    }

    // ==================
    // 3.2 传输限速（M1）
    // ==================

    if (const auto maxDownloadBytesPerSec = request.maxDownloadBytesPerSec();
        maxDownloadBytesPerSec.has_value()) {
        const qint64 bytesPerSec = maxDownloadBytesPerSec.value();
        if (bytesPerSec > 0) {
            setOptionalOffTOption(this,
                                  handle,
                                  CURLOPT_MAX_RECV_SPEED_LARGE,
                                  "CURLOPT_MAX_RECV_SPEED_LARGE",
                                  static_cast<curl_off_t>(bytesPerSec));
        }
    }

    if (const auto maxUploadBytesPerSec = request.maxUploadBytesPerSec();
        maxUploadBytesPerSec.has_value()) {
        const qint64 bytesPerSec = maxUploadBytesPerSec.value();
        if (bytesPerSec > 0) {
            setOptionalOffTOption(this,
                                  handle,
                                  CURLOPT_MAX_SEND_SPEED_LARGE,
                                  "CURLOPT_MAX_SEND_SPEED_LARGE",
                                  static_cast<curl_off_t>(bytesPerSec));
        }
    }

    // ==================
    // 4. HTTP 认证（Authorization: Basic / HTTPAUTH）
    // ==================

    bool wantsUnrestrictedSensitiveHeaders = request.allowUnrestrictedSensitiveHeadersOnRedirect();

    const auto httpAuthOpt = request.httpAuth();
    if (httpAuthOpt.has_value()) {
        const auto &cfg = httpAuthOpt.value();

        // Basic over HTTP 风险提示（不阻断）
        if (cfg.method == QCNetworkHttpAuthMethod::Basic && cfg.warnIfBasicOverHttp
            && request.url().scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) == 0) {
            qWarning() << "QCNetworkReply: Basic authentication over HTTP is insecure, consider "
                          "HTTPS. url="
                       << request.url().toString();
        }

        // 显式 Authorization header 优先：存在时不启用 libcurl 自动认证（避免重复/覆盖不确定性）
        if (!hasExplicitAuthorizationHeader) {
            hasSensitiveHeader = true;
            if (cfg.allowUnrestrictedAuth && request.followLocation()) {
                wantsUnrestrictedSensitiveHeaders = true;
            }

            httpAuthUserBytes     = cfg.userName.toUtf8();
            httpAuthPasswordBytes = cfg.password.toUtf8();

            curl_easy_setopt(handle, CURLOPT_USERNAME, httpAuthUserBytes.constData());
            curl_easy_setopt(handle, CURLOPT_PASSWORD, httpAuthPasswordBytes.constData());

            unsigned long httpAuth = CURLAUTH_BASIC;
            switch (cfg.method) {
                case QCNetworkHttpAuthMethod::Basic:
                    httpAuth = CURLAUTH_BASIC;
                    break;
                case QCNetworkHttpAuthMethod::Any:
                    httpAuth = CURLAUTH_ANY;
                    break;
                case QCNetworkHttpAuthMethod::AnySafe:
                    httpAuth = CURLAUTH_ANYSAFE;
                    break;
            }
            curl_easy_setopt(handle, CURLOPT_HTTPAUTH, httpAuth);
        }
    }

    // ==================
    // 4.1 重定向敏感头跨站策略（M1，安全基线）
    // ==================

    if (request.followLocation() && wantsUnrestrictedSensitiveHeaders && hasSensitiveHeader) {
        appendCapabilityWarning(this,
                                QStringLiteral("安全风险：已启用跨站发送敏感头（CURLOPT_"
                                               "UNRESTRICTED_AUTH=1），请确认重定向目标可信"));
        setOptionalLongOption(this,
                              handle,
                              CURLOPT_UNRESTRICTED_AUTH,
                              "CURLOPT_UNRESTRICTED_AUTH",
                              1L);
    }

    // ==================
    // 5. Range 请求配置
    // ==================

    if (request.rangeStart() >= 0 && request.rangeEnd() > request.rangeStart()) {
        QString rangeStr = QStringLiteral("%1-%2").arg(request.rangeStart()).arg(request.rangeEnd());
        QByteArray rangeBytes = rangeStr.toUtf8();
        curl_easy_setopt(handle, CURLOPT_RANGE, rangeBytes.constData());
    }

    // ==================
    // 6. 代理配置
    // ==================

    if (const auto proxyConfigOpt = request.proxyConfig(); proxyConfigOpt.has_value()) {
        const QCNetworkProxyConfig &proxyConfig = proxyConfigOpt.value();
        if (proxyConfig.type != QCNetworkProxyConfig::ProxyType::None) {
            if (!proxyConfig.isValid()) {
                qWarning() << "QCNetworkReply: invalid proxy configuration ignored";
            } else {
                proxyHostBytes = proxyConfig.hostName.toUtf8();
                curl_easy_setopt(handle, CURLOPT_PROXY, proxyHostBytes.constData());

                if (proxyConfig.port > 0) {
                    curl_easy_setopt(handle, CURLOPT_PROXYPORT, static_cast<long>(proxyConfig.port));
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
                        if (proxyConfig.tlsConfig.has_value()
                            && proxyConfig.tlsConfig->unsupportedSecurityPolicy
                                   == QCUnsupportedSecurityOptionPolicy::Fail) {
                            setError(NetworkError::InvalidRequest,
                                     QStringLiteral("当前构建的 libcurl 不支持 HTTPS "
                                                    "代理（CURLPROXY_HTTPS 未定义）"));
                            return false;
                        }
                        proxyType = CURLPROXY_HTTP;
                        appendCapabilityWarning(
                            this,
                            QStringLiteral("当前构建的 libcurl 不支持 HTTPS "
                                           "代理（CURLPROXY_HTTPS 未定义），已按 HTTP 代理处理"));
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
                    curl_easy_setopt(handle, CURLOPT_PROXYUSERNAME, proxyUserBytes.constData());
                } else {
                    proxyUserBytes.clear();
                }

                if (!proxyConfig.password.isEmpty()) {
                    proxyPasswordBytes = proxyConfig.password.toUtf8();
                    curl_easy_setopt(handle, CURLOPT_PROXYPASSWORD, proxyPasswordBytes.constData());
                } else {
                    proxyPasswordBytes.clear();
                }

                if (!proxyConfig.userName.isEmpty() || !proxyConfig.password.isEmpty()) {
                    curl_easy_setopt(handle, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
                }

                // ==================
                // Proxy TLS（M5，可选）：仅 HTTPS proxy + 显式配置时生效
                // ==================

#ifdef CURLPROXY_HTTPS
                if (proxyConfig.type == QCNetworkProxyConfig::ProxyType::Https
                    && proxyConfig.tlsConfig.has_value() && proxyType == CURLPROXY_HTTPS) {
                    const auto &tlsCfg = proxyConfig.tlsConfig.value();
                    const QCUnsupportedSecurityOptionPolicy tlsPolicy
                        = tlsCfg.unsupportedSecurityPolicy;

#ifdef CURLOPT_PROXY_SSL_VERIFYPEER
                    {
                        const CURLcode rc
                            = curlEasySetoptWithTestHook(handle,
                                                         CURLOPT_PROXY_SSL_VERIFYPEER,
                                                         "CURLOPT_PROXY_SSL_VERIFYPEER",
                                                         tlsCfg.verifyPeer ? 1L : 0L);
                        if (rc != CURLE_OK) {
                            if (isCapabilityRelatedCurlError(rc)) {
                                const QString msg
                                    = QStringLiteral(
                                          "libcurl 不支持 CURLOPT_PROXY_SSL_VERIFYPEER（%1）")
                                          .arg(QString::fromUtf8(curl_easy_strerror(rc)));
                                if (tlsPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                                    setError(NetworkError::InvalidRequest, msg);
                                    return false;
                                }
                                appendCapabilityWarning(this, msg);
                            } else {
                                setError(NetworkError::InvalidRequest,
                                         QStringLiteral(
                                             "设置 CURLOPT_PROXY_SSL_VERIFYPEER 失败（%1）")
                                             .arg(QString::fromUtf8(curl_easy_strerror(rc))));
                                return false;
                            }
                        }
                    }
#else
                    if (tlsPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                        setError(NetworkError::InvalidRequest,
                                 QStringLiteral(
                                     "当前构建的 libcurl 不支持 CURLOPT_PROXY_SSL_VERIFYPEER"));
                        return false;
                    }
                    appendCapabilityWarning(
                        this,
                        QStringLiteral("当前构建的 libcurl 不支持 CURLOPT_PROXY_SSL_VERIFYPEER"));
#endif

#ifdef CURLOPT_PROXY_SSL_VERIFYHOST
                    {
                        const CURLcode rc
                            = curlEasySetoptWithTestHook(handle,
                                                         CURLOPT_PROXY_SSL_VERIFYHOST,
                                                         "CURLOPT_PROXY_SSL_VERIFYHOST",
                                                         tlsCfg.verifyHost ? 2L : 0L);
                        if (rc != CURLE_OK) {
                            if (isCapabilityRelatedCurlError(rc)) {
                                const QString msg
                                    = QStringLiteral(
                                          "libcurl 不支持 CURLOPT_PROXY_SSL_VERIFYHOST（%1）")
                                          .arg(QString::fromUtf8(curl_easy_strerror(rc)));
                                if (tlsPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                                    setError(NetworkError::InvalidRequest, msg);
                                    return false;
                                }
                                appendCapabilityWarning(this, msg);
                            } else {
                                setError(NetworkError::InvalidRequest,
                                         QStringLiteral(
                                             "设置 CURLOPT_PROXY_SSL_VERIFYHOST 失败（%1）")
                                             .arg(QString::fromUtf8(curl_easy_strerror(rc))));
                                return false;
                            }
                        }
                    }
#else
                    if (tlsPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                        setError(NetworkError::InvalidRequest,
                                 QStringLiteral(
                                     "当前构建的 libcurl 不支持 CURLOPT_PROXY_SSL_VERIFYHOST"));
                        return false;
                    }
                    appendCapabilityWarning(
                        this,
                        QStringLiteral("当前构建的 libcurl 不支持 CURLOPT_PROXY_SSL_VERIFYHOST"));
#endif

                    if (!tlsCfg.caCertPath.isEmpty()) {
#ifdef CURLOPT_PROXY_CAINFO
                        proxySslCaCertPathBytes = tlsCfg.caCertPath.toUtf8();
                        const CURLcode rc
                            = curlEasySetoptWithTestHook(handle,
                                                         CURLOPT_PROXY_CAINFO,
                                                         "CURLOPT_PROXY_CAINFO",
                                                         proxySslCaCertPathBytes.constData());
                        if (rc != CURLE_OK) {
                            if (isCapabilityRelatedCurlError(rc)) {
                                const QString msg = QStringLiteral(
                                                        "libcurl 不支持 CURLOPT_PROXY_CAINFO（%1）")
                                                        .arg(QString::fromUtf8(
                                                            curl_easy_strerror(rc)));
                                if (tlsPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                                    setError(NetworkError::InvalidRequest, msg);
                                    return false;
                                }
                                appendCapabilityWarning(this, msg);
                            } else {
                                setError(NetworkError::InvalidRequest,
                                         QStringLiteral("设置 CURLOPT_PROXY_CAINFO 失败（%1）")
                                             .arg(QString::fromUtf8(curl_easy_strerror(rc))));
                                return false;
                            }
                        }
#else
                        if (tlsPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                            setError(NetworkError::InvalidRequest,
                                     QStringLiteral(
                                         "当前构建的 libcurl 不支持 CURLOPT_PROXY_CAINFO"));
                            return false;
                        }
                        appendCapabilityWarning(
                            this, QStringLiteral("当前构建的 libcurl 不支持 CURLOPT_PROXY_CAINFO"));
#endif
                    }

                    if (tlsCfg.minTlsVersion.has_value()) {
                        const std::optional<long> sslVer = toCurlSslVersionMin(
                            tlsCfg.minTlsVersion.value());
                        if (!sslVer.has_value()) {
                            const QString msg = QStringLiteral("不支持的 TLS 版本配置（proxy）");
                            if (tlsPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                                setError(NetworkError::InvalidRequest, msg);
                                return false;
                            }
                            appendCapabilityWarning(this, msg);
                        } else {
#ifdef CURLOPT_PROXY_SSLVERSION
                            const CURLcode rc
                                = curlEasySetoptWithTestHook(handle,
                                                             CURLOPT_PROXY_SSLVERSION,
                                                             "CURLOPT_PROXY_SSLVERSION",
                                                             sslVer.value());
                            if (rc != CURLE_OK) {
                                if (isCapabilityRelatedCurlError(rc)) {
                                    const QString msg
                                        = QStringLiteral(
                                              "libcurl 不支持 CURLOPT_PROXY_SSLVERSION（%1）")
                                              .arg(QString::fromUtf8(curl_easy_strerror(rc)));
                                    if (tlsPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                                        setError(NetworkError::InvalidRequest, msg);
                                        return false;
                                    }
                                    appendCapabilityWarning(this, msg);
                                } else {
                                    setError(NetworkError::InvalidRequest,
                                             QStringLiteral(
                                                 "设置 CURLOPT_PROXY_SSLVERSION 失败（%1）")
                                                 .arg(QString::fromUtf8(curl_easy_strerror(rc))));
                                    return false;
                                }
                            }
#else
                            if (tlsPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                                setError(NetworkError::InvalidRequest,
                                         QStringLiteral(
                                             "当前构建的 libcurl 不支持 CURLOPT_PROXY_SSLVERSION"));
                                return false;
                            }
                            appendCapabilityWarning(
                                this,
                                QStringLiteral(
                                    "当前构建的 libcurl 不支持 CURLOPT_PROXY_SSLVERSION"));
#endif
                        }
                    }

                    if (!tlsCfg.cipherList.isEmpty()) {
#ifdef CURLOPT_PROXY_SSL_CIPHER_LIST
                        proxySslCipherListBytes = tlsCfg.cipherList.toUtf8();
                        const CURLcode rc
                            = curlEasySetoptWithTestHook(handle,
                                                         CURLOPT_PROXY_SSL_CIPHER_LIST,
                                                         "CURLOPT_PROXY_SSL_CIPHER_LIST",
                                                         proxySslCipherListBytes.constData());
                        if (rc != CURLE_OK) {
                            if (isCapabilityRelatedCurlError(rc)) {
                                const QString msg
                                    = QStringLiteral(
                                          "libcurl 不支持 CURLOPT_PROXY_SSL_CIPHER_LIST（%1）")
                                          .arg(QString::fromUtf8(curl_easy_strerror(rc)));
                                if (tlsPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                                    setError(NetworkError::InvalidRequest, msg);
                                    return false;
                                }
                                appendCapabilityWarning(this, msg);
                            } else {
                                setError(NetworkError::InvalidRequest,
                                         QStringLiteral(
                                             "设置 CURLOPT_PROXY_SSL_CIPHER_LIST 失败（%1）")
                                             .arg(QString::fromUtf8(curl_easy_strerror(rc))));
                                return false;
                            }
                        }
#else
                        if (tlsPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                            setError(
                                NetworkError::InvalidRequest,
                                QStringLiteral(
                                    "当前构建的 libcurl 不支持 CURLOPT_PROXY_SSL_CIPHER_LIST"));
                            return false;
                        }
                        appendCapabilityWarning(
                            this,
                            QStringLiteral(
                                "当前构建的 libcurl 不支持 CURLOPT_PROXY_SSL_CIPHER_LIST"));
#endif
                    }

                    if (!tlsCfg.tls13Ciphers.isEmpty()) {
#ifdef CURLOPT_PROXY_TLS13_CIPHERS
                        proxySslTls13CiphersBytes = tlsCfg.tls13Ciphers.toUtf8();
                        const CURLcode rc
                            = curlEasySetoptWithTestHook(handle,
                                                         CURLOPT_PROXY_TLS13_CIPHERS,
                                                         "CURLOPT_PROXY_TLS13_CIPHERS",
                                                         proxySslTls13CiphersBytes.constData());
                        if (rc != CURLE_OK) {
                            if (isCapabilityRelatedCurlError(rc)) {
                                const QString msg
                                    = QStringLiteral(
                                          "libcurl 不支持 CURLOPT_PROXY_TLS13_CIPHERS（%1）")
                                          .arg(QString::fromUtf8(curl_easy_strerror(rc)));
                                if (tlsPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                                    setError(NetworkError::InvalidRequest, msg);
                                    return false;
                                }
                                appendCapabilityWarning(this, msg);
                            } else {
                                setError(NetworkError::InvalidRequest,
                                         QStringLiteral(
                                             "设置 CURLOPT_PROXY_TLS13_CIPHERS 失败（%1）")
                                             .arg(QString::fromUtf8(curl_easy_strerror(rc))));
                                return false;
                            }
                        }
#else
                        if (tlsPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                            setError(NetworkError::InvalidRequest,
                                     QStringLiteral(
                                         "当前构建的 libcurl 不支持 CURLOPT_PROXY_TLS13_CIPHERS"));
                            return false;
                        }
                        appendCapabilityWarning(
                            this,
                            QStringLiteral(
                                "当前构建的 libcurl 不支持 CURLOPT_PROXY_TLS13_CIPHERS"));
#endif
                    }
                }
#endif
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

    // ==================
    // 连接池配置 (v2.14.0)
    // ==================

    // 先应用连接池的通用配置（DNS/复用等），再由请求级别配置覆盖（例如 HTTP 版本）。
    auto *poolManager  = QCNetworkConnectionPoolManager::instance();
    const QString host = request.url().host();
    poolManager->configureCurlHandle(handle, host);

    // ==================
    // HTTP 版本配置
    // ==================

    const QCNetworkHttpVersion requestedHttpVer = request.httpVersion();
    QCNetworkHttpVersion effectiveHttpVer       = requestedHttpVer;

    const long features        = CurlFeatureProbe::instance().runtimeFeatures();
    const bool runtimeHasHttp3 = (features & CURL_VERSION_HTTP3) != 0;
    const bool requireHttp3    = qgetenv("QCURL_REQUIRE_HTTP3").trimmed() == "1";

    if (requireHttp3
        && (requestedHttpVer == QCNetworkHttpVersion::Http3
            || requestedHttpVer == QCNetworkHttpVersion::Http3Only)
        && !runtimeHasHttp3) {
        setError(NetworkError::InvalidRequest,
                 QStringLiteral("交付门禁 QCURL_REQUIRE_HTTP3=1：运行时 libcurl 不支持 "
                                "HTTP/3（CURL_VERSION_HTTP3 缺失），请求被拒绝"));
        return false;
    }

    if (requestedHttpVer == QCNetworkHttpVersion::Http3Only) {
        if (!runtimeHasHttp3) {
            setError(
                NetworkError::InvalidRequest,
                QStringLiteral(
                    "运行时 libcurl 不支持 HTTP/3（CURL_VERSION_HTTP3 缺失），Http3Only 无法执行"));
            return false;
        }
#if !defined(CURL_HTTP_VERSION_3ONLY)
        appendCapabilityWarning(this,
                                QStringLiteral(
                                    "当前构建的 libcurl 不支持 CURL_HTTP_VERSION_3ONLY，Http3Only "
                                    "将退化为 Http3（可能发生协议降级）"));
#endif
    } else if (requestedHttpVer == QCNetworkHttpVersion::Http3) {
        if (!runtimeHasHttp3) {
            effectiveHttpVer = QCNetworkHttpVersion::Http2TLS;
            appendCapabilityWarning(
                this,
                QStringLiteral(
                    "运行时 libcurl 不支持 HTTP/3（CURL_VERSION_HTTP3 缺失），已降级为 HTTP/2TLS"));
        }
    }

    if (effectiveHttpVer != QCNetworkHttpVersion::Http1_1 || request.isHttpVersionExplicit()) {
        long curlVersion = toCurlHttpVersion(effectiveHttpVer);
        curl_easy_setopt(handle, CURLOPT_HTTP_VERSION, curlVersion);
    }

    // ==================
    // 7. SSL 配置（基于 QCNetworkSslConfig）
    // ==================

    const QCNetworkSslConfig sslConfig = request.sslConfig();
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, sslConfig.verifyPeer ? 1L : 0L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, sslConfig.verifyHost ? 2L : 0L);

    if (!sslConfig.caCertPath.isEmpty()) {
        sslCaCertPathBytes = sslConfig.caCertPath.toUtf8();
        curl_easy_setopt(handle, CURLOPT_CAINFO, sslCaCertPathBytes.constData());
    } else {
        // ✅ 修复：如果未指定 CA 证书路径，尝试使用系统默认路径
        // 优先使用常见的系统 CA 证书位置
        static const char *systemCaPaths[]
            = {"/etc/ssl/certs/ca-certificates.crt",     // Debian/Ubuntu/Arch
               "/etc/pki/tls/certs/ca-bundle.crt",       // RHEL/CentOS
               "/etc/ssl/cert.pem",                      // Alpine/OpenBSD
               "/usr/local/share/certs/ca-root-nss.crt", // FreeBSD
               nullptr};

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

    // ==================
    // 7.1 TLS 策略（M5，可选；安全相关能力默认失败，可配置为 warning）
    // ==================

    const QCUnsupportedSecurityOptionPolicy sslPolicy = sslConfig.unsupportedSecurityPolicy;

    if (!sslConfig.pinnedPublicKey.isEmpty()) {
        sslPinnedPublicKeyBytes = sslConfig.pinnedPublicKey.toUtf8();
        const CURLcode rc       = curlEasySetoptWithTestHook(handle,
                                                       CURLOPT_PINNEDPUBLICKEY,
                                                       "CURLOPT_PINNEDPUBLICKEY",
                                                       sslPinnedPublicKeyBytes.constData());
        if (rc != CURLE_OK) {
            if (isCapabilityRelatedCurlError(rc)) {
                const QString msg = QStringLiteral("libcurl 不支持 CURLOPT_PINNEDPUBLICKEY（%1）")
                                        .arg(QString::fromUtf8(curl_easy_strerror(rc)));
                if (sslPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                    setError(NetworkError::InvalidRequest, msg);
                    return false;
                }
                appendCapabilityWarning(this, msg);
            } else {
                setError(NetworkError::InvalidRequest,
                         QStringLiteral("设置 CURLOPT_PINNEDPUBLICKEY 失败（%1）")
                             .arg(QString::fromUtf8(curl_easy_strerror(rc))));
                return false;
            }
        }
    }

    if (sslConfig.minTlsVersion.has_value()) {
        const std::optional<long> sslVer = toCurlSslVersionMin(sslConfig.minTlsVersion.value());
        if (!sslVer.has_value()) {
            const QString msg = QStringLiteral("不支持的 TLS 版本配置（ssl）");
            if (sslPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                setError(NetworkError::InvalidRequest, msg);
                return false;
            }
            appendCapabilityWarning(this, msg);
        } else {
#ifdef CURLOPT_SSLVERSION
            const CURLcode rc = curlEasySetoptWithTestHook(handle,
                                                           CURLOPT_SSLVERSION,
                                                           "CURLOPT_SSLVERSION",
                                                           sslVer.value());
            if (rc != CURLE_OK) {
                if (isCapabilityRelatedCurlError(rc)) {
                    const QString msg = QStringLiteral("libcurl 不支持 CURLOPT_SSLVERSION（%1）")
                                            .arg(QString::fromUtf8(curl_easy_strerror(rc)));
                    if (sslPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                        setError(NetworkError::InvalidRequest, msg);
                        return false;
                    }
                    appendCapabilityWarning(this, msg);
                } else {
                    setError(NetworkError::InvalidRequest,
                             QStringLiteral("设置 CURLOPT_SSLVERSION 失败（%1）")
                                 .arg(QString::fromUtf8(curl_easy_strerror(rc))));
                    return false;
                }
            }
#else
            const QString msg = QStringLiteral("当前构建的 libcurl 不支持 CURLOPT_SSLVERSION");
            if (sslPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                setError(NetworkError::InvalidRequest, msg);
                return false;
            }
            appendCapabilityWarning(this, msg);
#endif
        }
    }

    if (!sslConfig.cipherList.isEmpty()) {
#ifdef CURLOPT_SSL_CIPHER_LIST
        sslCipherListBytes = sslConfig.cipherList.toUtf8();
        const CURLcode rc  = curlEasySetoptWithTestHook(handle,
                                                       CURLOPT_SSL_CIPHER_LIST,
                                                       "CURLOPT_SSL_CIPHER_LIST",
                                                       sslCipherListBytes.constData());
        if (rc != CURLE_OK) {
            if (isCapabilityRelatedCurlError(rc)) {
                const QString msg = QStringLiteral("libcurl 不支持 CURLOPT_SSL_CIPHER_LIST（%1）")
                                        .arg(QString::fromUtf8(curl_easy_strerror(rc)));
                if (sslPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                    setError(NetworkError::InvalidRequest, msg);
                    return false;
                }
                appendCapabilityWarning(this, msg);
            } else {
                setError(NetworkError::InvalidRequest,
                         QStringLiteral("设置 CURLOPT_SSL_CIPHER_LIST 失败（%1）")
                             .arg(QString::fromUtf8(curl_easy_strerror(rc))));
                return false;
            }
        }
#else
        const QString msg = QStringLiteral("当前构建的 libcurl 不支持 CURLOPT_SSL_CIPHER_LIST");
        if (sslPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
            setError(NetworkError::InvalidRequest, msg);
            return false;
        }
        appendCapabilityWarning(this, msg);
#endif
    }

    if (!sslConfig.tls13Ciphers.isEmpty()) {
#ifdef CURLOPT_TLS13_CIPHERS
        sslTls13CiphersBytes = sslConfig.tls13Ciphers.toUtf8();
        const CURLcode rc    = curlEasySetoptWithTestHook(handle,
                                                       CURLOPT_TLS13_CIPHERS,
                                                       "CURLOPT_TLS13_CIPHERS",
                                                       sslTls13CiphersBytes.constData());
        if (rc != CURLE_OK) {
            if (isCapabilityRelatedCurlError(rc)) {
                const QString msg = QStringLiteral("libcurl 不支持 CURLOPT_TLS13_CIPHERS（%1）")
                                        .arg(QString::fromUtf8(curl_easy_strerror(rc)));
                if (sslPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                    setError(NetworkError::InvalidRequest, msg);
                    return false;
                }
                appendCapabilityWarning(this, msg);
            } else {
                setError(NetworkError::InvalidRequest,
                         QStringLiteral("设置 CURLOPT_TLS13_CIPHERS 失败（%1）")
                             .arg(QString::fromUtf8(curl_easy_strerror(rc))));
                return false;
            }
        }
#else
        const QString msg = QStringLiteral("当前构建的 libcurl 不支持 CURLOPT_TLS13_CIPHERS");
        if (sslPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
            setError(NetworkError::InvalidRequest, msg);
            return false;
        }
        appendCapabilityWarning(this, msg);
#endif
    }

    // ==================
    // 8. 超时配置
    // ==================

    QCNetworkTimeoutConfig timeout = request.timeoutConfig();

    // 连接超时（TCP 三次握手超时）
    if (timeout.connectTimeout.has_value() && timeout.connectTimeout->count() > 0) {
        curl_easy_setopt(handle,
                         CURLOPT_CONNECTTIMEOUT_MS,
                         static_cast<long>(timeout.connectTimeout->count()));
    }

    // 总超时（整个请求的最大时间）
    if (timeout.totalTimeout.has_value() && timeout.totalTimeout->count() > 0) {
        curl_easy_setopt(handle,
                         CURLOPT_TIMEOUT_MS,
                         static_cast<long>(timeout.totalTimeout->count()));
    }

    // 低速检测：如果在 lowSpeedTime 内速度低于 lowSpeedLimit，则超时
    if (timeout.lowSpeedTime.has_value() && timeout.lowSpeedTime->count() > 0) {
        curl_easy_setopt(handle,
                         CURLOPT_LOW_SPEED_TIME,
                         static_cast<long>(timeout.lowSpeedTime->count()));
    }

    if (timeout.lowSpeedLimit.has_value() && *timeout.lowSpeedLimit > 0) {
        curl_easy_setopt(handle, CURLOPT_LOW_SPEED_LIMIT, static_cast<long>(*timeout.lowSpeedLimit));
    }

    // ==================
    // 9. 回调函数配置
    // ==================

    // 响应体写回调
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, QCNetworkReplyPrivate::curlWriteCallback);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, this);

    // 响应头回调
    curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, QCNetworkReplyPrivate::curlHeaderCallback);
    curl_easy_setopt(handle, CURLOPT_HEADERDATA, this);

    // 注意：READFUNCTION 仅在 M2 流式上传（uploadDevice/uploadFile）时启用；
    // 旧的 QByteArray body 路径仍使用 CURLOPT_POSTFIELDS（避免默认行为变化）。

    // 定位回调
    curl_easy_setopt(handle, CURLOPT_SEEKFUNCTION, QCNetworkReplyPrivate::curlSeekCallback);
    curl_easy_setopt(handle, CURLOPT_SEEKDATA, this);

    // 进度回调
    curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, QCNetworkReplyPrivate::curlProgressCallback);
    curl_easy_setopt(handle, CURLOPT_XFERINFODATA, this);
    curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L); // 启用进度回调

    return true;
}

void QCNetworkReplyPrivate::setState(ReplyState newState)
{
    Q_Q(QCNetworkReply);

    if (state == newState) {
        return;
    }

    state = newState;

    if (newState == ReplyState::Running) {
        if (!elapsedTimerStarted) {
            elapsedTimer.start();
            elapsedTimerStarted = true;
            durationMs          = -1;
        }
    } else if (newState == ReplyState::Finished || newState == ReplyState::Error
               || newState == ReplyState::Cancelled) {
        if (elapsedTimerStarted && durationMs < 0) {
            durationMs = elapsedTimer.elapsed();
        }
    }

    // 发射状态变更信号
    emit q->stateChanged(newState);

    // 终态收敛：内部流控状态不应残留（避免影响后续诊断/合同采集）
    if (newState == ReplyState::Finished || newState == ReplyState::Error
        || newState == ReplyState::Cancelled) {
        if (internalPauseMask & CURLPAUSE_RECV) {
            internalPauseMask &= ~CURLPAUSE_RECV;
        }
        if (internalPauseMask & CURLPAUSE_SEND) {
            internalPauseMask &= ~CURLPAUSE_SEND;
        }
        setBackpressureActive(false);
        setUploadSendPaused(false);
    }

    // 根据新状态发射对应信号
    if (newState == ReplyState::Finished) {
        // 完成时解析响应头
        parseHeaders();

        // ==================
        // Cookie jar flush：当启用 COOKIEJAR 时，确保请求完成后立即落盘
        // ==================
        if (curlManager.handle() && (cookieMode & 0x2) && !cookieFilePath.isEmpty()) {
            const CURLcode rc = curl_easy_setopt(curlManager.handle(), CURLOPT_COOKIELIST, "FLUSH");
            if (rc != CURLE_OK) {
                if (isCapabilityRelatedCurlError(rc)) {
                    appendCapabilityWarning(
                        this,
                        QStringLiteral(
                            "libcurl 不支持 Cookie "
                            "flush（CURLOPT_COOKIELIST，%1），CookieJAR 可能不会立即落盘")
                            .arg(QString::fromUtf8(curl_easy_strerror(rc))));
                } else {
                    appendCapabilityWarning(this,
                                            QStringLiteral("Cookie flush 失败（%1）")
                                                .arg(QString::fromUtf8(curl_easy_strerror(rc))));
                }
            }
        }

        // ==================
        // 连接池统计 - 记录连接复用情况
        // ==================
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

        // ==================
        // 缓存集成 - 请求完成后自动写入缓存
        // ==================
        QCNetworkAccessManager *manager = qobject_cast<QCNetworkAccessManager *>(q->parent());
        if (manager) {
            QCNetworkCache *cache       = manager->cache();
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
                        meta.url            = request.url();
                        meta.headers        = headers;
                        meta.expirationDate = QCNetworkCache::parseExpirationDate(headers);

                        // 获取响应数据（不移除缓冲区数据）
                        QByteArray responseData = bodyBuffer.read(bodyBuffer.byteAmount());

                        // 写入缓存
                        cache->insert(request.url(), responseData, meta);
                        qDebug() << "QCNetworkReply: Cached response for"
                                 << request.url().toString();

                        // 重新放回缓冲区（确保 readAll() 仍可用）
                        bodyBuffer.append(responseData);
                    } else {
                        qDebug() << "QCNetworkReply: Response not cacheable for"
                                 << request.url().toString();
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
        // 取消属于终态：同时发射 cancelled + finished（对齐 QNetworkReply 行为）
        emit q->cancelled();
        emit q->finished();
    }
}

void QCNetworkReplyPrivate::setError(NetworkError error, const QString &message)
{
    errorCode    = error;
    errorMessage = message;
}

void QCNetworkReplyPrivate::parseHeaders()
{
    headerMap.clear();

    // 解析并 unfold 响应头数据（兼容折叠行与 TAB；对齐 curl header API 的可观测语义）
    //
    // 规则（与 curl/tests/data/test1274/test1940/test798 的预期一致）：
    // - continuation line：以 SP/HTAB 开头的行视为上一条 header 的延续（obsolete line folding）
    // - unfold：将各段按“单空格”拼接，并对每段做 trim（去掉首尾空白与末尾多余空格）
    //
    // 说明：
    // - headerMap 为单值 map，会覆盖重复头（如 Set-Cookie 多值）；重复头应通过 rawHeaderData()
    // 观测。
    auto flushCurrent = [&](const QByteArray &name, const QList<QByteArray> &segments) {
        if (name.isEmpty()) {
            return;
        }
        QByteArray value;
        for (const QByteArray &seg : segments) {
            const QByteArray part = seg.trimmed();
            if (part.isEmpty()) {
                continue;
            }
            if (!value.isEmpty()) {
                value.append(' ');
            }
            value.append(part);
        }
        headerMap.insert(QString::fromUtf8(name), QString::fromUtf8(value));
    };

    QByteArray currentName;
    QList<QByteArray> currentSegments;

    const QList<QByteArray> lines = headerData.split('\n');
    for (QByteArray line : lines) {
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        if (line.isEmpty()) {
            flushCurrent(currentName, currentSegments);
            currentName.clear();
            currentSegments.clear();
            continue;
        }
        if (line.startsWith("HTTP/")) {
            // 新的 header block（redirect/1xx/CONNECT 等）开始，先落盘上一段
            flushCurrent(currentName, currentSegments);
            currentName.clear();
            currentSegments.clear();
            continue;
        }

        const bool isContinuation = !currentName.isEmpty()
                                    && (line.startsWith(' ') || line.startsWith('\t'));
        if (isContinuation) {
            currentSegments.append(line);
            continue;
        }

        const int colonPos = line.indexOf(':');
        if (colonPos <= 0) {
            continue;
        }

        flushCurrent(currentName, currentSegments);
        currentName = line.left(colonPos).trimmed();
        currentSegments.clear();
        currentSegments.append(line.mid(colonPos + 1));
    }

    flushCurrent(currentName, currentSegments);
}

bool QCNetworkReplyPrivate::applyPauseMask(int desiredMask)
{
    if (executionMode == ExecutionMode::Sync) {
        return false;
    }

    CURL *handle = curlManager.handle();
    if (!handle) {
        return false;
    }

    const int normalized = desiredMask & (CURLPAUSE_RECV | CURLPAUSE_SEND);
    if (normalized == appliedPauseMask) {
        return true;
    }

    const int flags = (normalized == 0) ? CURLPAUSE_CONT : normalized;

    auto *multiManager = QCCurlMultiManager::instance();
    CURLcode result    = CURLE_OK;
    if (QThread::currentThread() == multiManager->thread()) {
        result = curl_easy_pause(handle, flags);
    } else {
        QMetaObject::invokeMethod(
            multiManager,
            [handle, flags, &result]() { result = curl_easy_pause(handle, flags); },
            Qt::BlockingQueuedConnection);
    }

    if (result != CURLE_OK) {
        qWarning() << "QCNetworkReplyPrivate::applyPauseMask: curl_easy_pause failed:"
                   << curl_easy_strerror(result) << "desiredMask=" << normalized;
        return false;
    }

    appliedPauseMask = normalized;
    return true;
}

void QCNetworkReplyPrivate::setBackpressureActive(bool active)
{
    if (backpressureLimitBytes <= 0) {
        backpressureActive = false;
        return;
    }

    if (backpressureActive == active) {
        return;
    }

    backpressureActive = active;
    if (q_ptr) {
        emit q_ptr->backpressureStateChanged(active, bodyBuffer.byteAmount(), backpressureLimitBytes);
    }
}

void QCNetworkReplyPrivate::setUploadSendPaused(bool paused)
{
    if (executionMode != ExecutionMode::Async) {
        uploadSendPaused = false;
        return;
    }

    if (uploadSendPaused == paused) {
        return;
    }

    uploadSendPaused = paused;
    if (q_ptr) {
        emit q_ptr->uploadSendPausedChanged(paused);
    }
}

void QCNetworkReplyPrivate::maybeResumeRecvFromBackpressure()
{
    if (executionMode != ExecutionMode::Async) {
        return;
    }
    if (backpressureLimitBytes <= 0) {
        return;
    }
    if ((internalPauseMask & CURLPAUSE_RECV) == 0) {
        return;
    }
    if (state == ReplyState::Cancelled || state == ReplyState::Error || state == ReplyState::Finished) {
        return;
    }

    const qint64 buffered = bodyBuffer.byteAmount();
    if (buffered > backpressureResumeBytes) {
        return;
    }

    const int oldMask = appliedPauseMask;
    const int desiredAfterResume = desiredPauseMask() & ~CURLPAUSE_RECV;
    if (!applyPauseMask(desiredAfterResume)) {
        return;
    }

    internalPauseMask &= ~CURLPAUSE_RECV;
    setBackpressureActive(false);

    if ((oldMask & CURLPAUSE_RECV) && ((appliedPauseMask & CURLPAUSE_RECV) == 0)) {
        QCCurlMultiManager::instance()->wakeup();
    }
}

void QCNetworkReplyPrivate::resumeSendFromUploadSourceIfNeeded()
{
    if (executionMode != ExecutionMode::Async) {
        return;
    }
    if ((internalPauseMask & CURLPAUSE_SEND) == 0) {
        return;
    }
    if (state == ReplyState::Cancelled || state == ReplyState::Error || state == ReplyState::Finished) {
        return;
    }

    QIODevice *device = uploadDevice.data();
    if (!device || !device->isReadable()) {
        return;
    }

    if (device->bytesAvailable() <= 0 && !device->atEnd()) {
        return;
    }

    const int oldMask = appliedPauseMask;
    internalPauseMask &= ~CURLPAUSE_SEND;

    if (!applyPauseMask(desiredPauseMask())) {
        internalPauseMask |= CURLPAUSE_SEND;
        return;
    }

    setUploadSendPaused(false);
    if ((oldMask & CURLPAUSE_SEND) && ((appliedPauseMask & CURLPAUSE_SEND) == 0)) {
        QCCurlMultiManager::instance()->wakeup();
    }
}

// ==================
// Curl 静态回调函数实现
// ==================

size_t QCNetworkReplyPrivate::curlWriteCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    CurlCallbackScope callbackScope;

    auto *d = static_cast<QCNetworkReplyPrivate *>(userdata);
    if (!d || !d->q_ptr) {
        return 0; // 对象已销毁，中止传输
    }

    const size_t totalSize = size * nmemb;

    if (d->state == ReplyState::Cancelled || d->state == ReplyState::Error) {
        // 取消/错误后禁止继续产生可观测数据事件（readyRead 等）；
        // 返回 totalSize 以避免触发额外的 libcurl 错误路径。
        return totalSize;
    }

    if (d->executionMode == ExecutionMode::Async) {
        // P2-0：回调边界 pause hook（用于对齐 baseline 的“pause 在 RECV 回调边界生效”语义）
        if ((d->userPauseMask & CURLPAUSE_RECV) && ((d->appliedPauseMask & CURLPAUSE_RECV) == 0)) {
            d->appliedPauseMask |= CURLPAUSE_RECV;
            return CURL_WRITEFUNC_PAUSE;
        }

        // 异步模式：累积数据到缓冲区
        d->bodyBuffer.append(QByteArray(ptr, static_cast<int>(totalSize)));
        if (d->backpressureLimitBytes > 0) {
            d->backpressurePeakBufferedBytes
                = qMax(d->backpressurePeakBufferedBytes, d->bodyBuffer.byteAmount());
        }
        d->bytesDownloaded += static_cast<qint64>(totalSize);

        // 发射 readyRead 信号
        emit d->q_ptr->readyRead();

        // P2-1：下载 backpressure（soft limit，高水位线；允许有界超限）
        if (d->backpressureLimitBytes > 0) {
            const qint64 buffered = d->bodyBuffer.byteAmount();
            if (buffered >= d->backpressureLimitBytes) {
                // 注意：此处不能通过返回 CURL_WRITEFUNC_PAUSE 实现暂停，否则会退化为 hard cap
                // （本次 write callback 数据无法交付上层，可能导致 tiny limit 场景卡住）。
                const bool userRecvPaused     = (d->userPauseMask & CURLPAUSE_RECV) != 0;
                const bool internalRecvPaused = (d->internalPauseMask & CURLPAUSE_RECV) != 0;
                if (!userRecvPaused) {
                    const int desiredBase = d->desiredPauseMask();
                    const int desiredMask = internalRecvPaused ? desiredBase : (desiredBase | CURLPAUSE_RECV);
                    if (d->applyPauseMask(desiredMask) && !internalRecvPaused) {
                        d->internalPauseMask |= CURLPAUSE_RECV;
                        d->setBackpressureActive(true);
                    }
                }
            }
        }
    } else {
        // 同步模式：优先调用用户回调，否则累积到缓冲区
        if (d->writeCallback) {
            return d->writeCallback(ptr, totalSize);
        }

        // 没有回调函数时，也累积数据（支持 readAll()）
        d->bodyBuffer.append(QByteArray(ptr, static_cast<int>(totalSize)));
        d->bytesDownloaded += static_cast<qint64>(totalSize);
    }

    return totalSize;
}

size_t QCNetworkReplyPrivate::curlHeaderCallback(char *ptr,
                                                 size_t size,
                                                 size_t nmemb,
                                                 void *userdata)
{
    auto *d = static_cast<QCNetworkReplyPrivate *>(userdata);
    if (!d || !d->q_ptr) {
        return 0; // 对象已销毁，中止传输
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

size_t QCNetworkReplyPrivate::curlReadCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    CurlCallbackScope callbackScope;

    auto *d = static_cast<QCNetworkReplyPrivate *>(userdata);
    if (!d || !d->q_ptr) {
        return CURL_READFUNC_ABORT; // 对象已销毁，中止传输
    }

    if (d->state == ReplyState::Cancelled || d->state == ReplyState::Error) {
        return CURL_READFUNC_ABORT;
    }

    QIODevice *device = d->uploadDevice.data();
    if (!device) {
        d->hasUploadErrorOverride     = true;
        d->uploadErrorOverrideCode    = NetworkError::InvalidRequest;
        d->uploadErrorOverrideMessage = QStringLiteral("uploadDevice: 源 QIODevice 在传输中被销毁");
        return CURL_READFUNC_ABORT;
    }

    if (!device->isReadable()) {
        d->hasUploadErrorOverride     = true;
        d->uploadErrorOverrideCode    = NetworkError::InvalidRequest;
        d->uploadErrorOverrideMessage = QStringLiteral("uploadDevice: 源 QIODevice 已不可读");
        return CURL_READFUNC_ABORT;
    }

    const size_t totalSize = size * nmemb;
    if (totalSize == 0) {
        return 0;
    }

    if (d->uploadBodySizeBytes < 0) {
        const qint64 n = device->read(ptr, static_cast<qint64>(totalSize));
        if (n < 0) {
            d->hasUploadErrorOverride     = true;
            d->uploadErrorOverrideCode    = NetworkError::InvalidRequest;
            d->uploadErrorOverrideMessage = QStringLiteral("uploadDevice: 读取失败: %1")
                                                .arg(device->errorString());
            return CURL_READFUNC_ABORT;
        }
        if (n == 0) {
            if (device->atEnd()) {
                return 0; // unknown size：EOF
            }

            // P2-2：source not ready → pause sending，等待 readyRead 恢复
            d->internalPauseMask |= CURLPAUSE_SEND;
            d->appliedPauseMask |= CURLPAUSE_SEND;
            d->setUploadSendPaused(true);
            return CURL_READFUNC_PAUSE;
        }
        d->uploadBytesRead += n;
        return static_cast<size_t>(n);
    }

    const qint64 remaining = d->uploadBodySizeBytes - d->uploadBytesRead;
    if (remaining <= 0) {
        return 0;
    }

    const qint64 want = qMin(static_cast<qint64>(totalSize), remaining);
    const qint64 n    = device->read(ptr, want);
    if (n < 0) {
        d->hasUploadErrorOverride     = true;
        d->uploadErrorOverrideCode    = NetworkError::InvalidRequest;
        d->uploadErrorOverrideMessage = QStringLiteral("uploadDevice: 读取失败: %1")
                                            .arg(device->errorString());
        return CURL_READFUNC_ABORT;
    }

    if (n == 0 && remaining > 0) {
        if (device->atEnd()) {
            d->hasUploadErrorOverride  = true;
            d->uploadErrorOverrideCode = NetworkError::InvalidRequest;
            d->uploadErrorOverrideMessage
                = QStringLiteral("uploadDevice: 数据提前结束（期望剩余 %1 bytes）").arg(remaining);
            return CURL_READFUNC_ABORT;
        }

        // P2-2：source not ready → pause sending，等待 readyRead 恢复
        d->internalPauseMask |= CURLPAUSE_SEND;
        d->appliedPauseMask |= CURLPAUSE_SEND;
        d->setUploadSendPaused(true);
        return CURL_READFUNC_PAUSE;
    }

    d->uploadBytesRead += n;
    return static_cast<size_t>(n);
}

int QCNetworkReplyPrivate::curlSeekCallback(void *userdata, curl_off_t offset, int origin)
{
    auto *d = static_cast<QCNetworkReplyPrivate *>(userdata);
    if (!d || !d->q_ptr) {
        return CURL_SEEKFUNC_FAIL; // 对象已销毁
    }

    if (d->state == ReplyState::Cancelled || d->state == ReplyState::Error) {
        return CURL_SEEKFUNC_FAIL;
    }

    if (QIODevice *device = d->uploadDevice.data()) {
        if (!d->uploadDeviceSeekable) {
            d->hasUploadErrorOverride     = true;
            d->uploadErrorOverrideCode    = NetworkError::InvalidRequest;
            d->uploadErrorOverrideMessage = QStringLiteral(
                "uploadDevice: 无法重发 body：源 QIODevice 不支持 seek（重定向/重试/认证协商）");
            return CURL_SEEKFUNC_CANTSEEK;
        }

        qint64 targetPos = -1;
        const qint64 off = static_cast<qint64>(offset);
        switch (origin) {
            case SEEK_SET:
                targetPos = d->uploadDeviceBasePos + off;
                break;
            case SEEK_CUR:
                targetPos = device->pos() + off;
                break;
            case SEEK_END:
                if (d->uploadBodySizeBytes < 0) {
                    d->hasUploadErrorOverride     = true;
                    d->uploadErrorOverrideCode    = NetworkError::InvalidRequest;
                    d->uploadErrorOverrideMessage = QStringLiteral(
                        "uploadDevice: unknown size 不支持 SEEK_END（无法重发 body）");
                    return CURL_SEEKFUNC_FAIL;
                }
                targetPos = d->uploadDeviceBasePos + d->uploadBodySizeBytes + off;
                break;
            default:
                return CURL_SEEKFUNC_FAIL;
        }

        if (targetPos < d->uploadDeviceBasePos) {
            return CURL_SEEKFUNC_FAIL;
        }
        if (d->uploadBodySizeBytes >= 0
            && targetPos > (d->uploadDeviceBasePos + d->uploadBodySizeBytes)) {
            return CURL_SEEKFUNC_FAIL;
        }

        if (!device->seek(targetPos)) {
            d->hasUploadErrorOverride  = true;
            d->uploadErrorOverrideCode = NetworkError::InvalidRequest;
            d->uploadErrorOverrideMessage
                = QStringLiteral("uploadDevice: 无法重发 body：seek(%1) 失败").arg(targetPos);
            return CURL_SEEKFUNC_FAIL;
        }

        d->uploadBytesRead = targetPos - d->uploadDeviceBasePos;
        return CURL_SEEKFUNC_OK;
    }

    // 同步模式：调用用户回调
    if (d->executionMode == ExecutionMode::Sync && d->seekCallback) {
        return d->seekCallback(static_cast<qint64>(offset), origin);
    }

    return CURL_SEEKFUNC_CANTSEEK; // 不支持
}

int QCNetworkReplyPrivate::curlProgressCallback(
    void *userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    CurlCallbackScope callbackScope;

    auto *d = static_cast<QCNetworkReplyPrivate *>(userdata);
    if (!d || !d->q_ptr) {
        return 1; // 对象已销毁，中止传输
    }

    if (d->state == ReplyState::Cancelled || d->state == ReplyState::Error) {
        // 取消/错误后禁止继续产生可观测事件（downloadProgress/uploadProgress）。
        return 0;
    }

    // 更新进度数据
    d->downloadTotal   = static_cast<qint64>(dltotal);
    d->bytesDownloaded = static_cast<qint64>(dlnow);
    d->uploadTotal     = static_cast<qint64>(ultotal);
    d->bytesUploaded   = static_cast<qint64>(ulnow);

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

int QCNetworkReplyPrivate::curlDebugCallback(
    CURL *handle, curl_infotype type, char *data, size_t size, void *userptr)
{
    Q_UNUSED(handle);

    auto *d = static_cast<QCNetworkReplyPrivate *>(userptr);
    if (!d || !d->q_ptr) {
        return 0;
    }

    if (d->state == ReplyState::Cancelled || d->state == ReplyState::Error) {
        return 0;
    }

    auto *manager = qobject_cast<QCNetworkAccessManager *>(d->q_ptr->parent());
    if (!manager || !manager->debugTraceEnabled()) {
        return 0;
    }

    QCNetworkLogger *logger = manager->logger();
    if (!logger) {
        return 0;
    }

    const QByteArray raw = QByteArray(data, static_cast<int>(size));

    auto redactBlock = [](const QByteArray &block) -> QString {
        const QList<QByteArray> lines = block.split('\n');
        QStringList out;
        out.reserve(lines.size());
        for (const QByteArray &line : lines) {
            if (line.isEmpty()) {
                continue;
            }
            out.append(QCNetworkLogRedaction::redactSensitiveTraceLine(line));
        }
        return out.join(QStringLiteral("\n"));
    };

    QString category = QStringLiteral("Trace");
    QString message;

    switch (type) {
        case CURLINFO_TEXT:
            message = QStringLiteral("TEXT: %1").arg(redactBlock(raw).trimmed());
            break;
        case CURLINFO_HEADER_IN:
            message = QStringLiteral("HEADER_IN: %1").arg(redactBlock(raw).trimmed());
            break;
        case CURLINFO_HEADER_OUT:
            message = QStringLiteral("HEADER_OUT: %1").arg(redactBlock(raw).trimmed());
            break;
        case CURLINFO_DATA_IN:
            message = QStringLiteral("DATA_IN: len=%1").arg(static_cast<qulonglong>(size));
            break;
        case CURLINFO_DATA_OUT:
            message = QStringLiteral("DATA_OUT: len=%1").arg(static_cast<qulonglong>(size));
            break;
        case CURLINFO_SSL_DATA_IN:
            message = QStringLiteral("SSL_DATA_IN: len=%1").arg(static_cast<qulonglong>(size));
            break;
        case CURLINFO_SSL_DATA_OUT:
            message = QStringLiteral("SSL_DATA_OUT: len=%1").arg(static_cast<qulonglong>(size));
            break;
        default:
            message = QStringLiteral("TRACE_%1: len=%2")
                          .arg(static_cast<int>(type))
                          .arg(static_cast<qulonglong>(size));
            break;
    }

    if (message.size() > 4096) {
        message = message.left(4096) + QStringLiteral("…(truncated)");
    }

    logger->log(NetworkLogLevel::Debug, category, message);
    return 0;
}

// ==================
// QCNetworkReply 公共接口实现
// ==================

QCNetworkReply::QCNetworkReply(const QCNetworkRequest &request,
                               HttpMethod method,
                               ExecutionMode mode,
                               const QByteArray &requestBody,
                               QObject *parent)
    : QObject(parent)
    , d_ptr(new QCNetworkReplyPrivate(this, request, method, mode, requestBody))
{
    Q_D(QCNetworkReply);

    // 配置 curl 选项
    if (!d->configureCurlOptions()) {
        if (d->errorCode == NetworkError::NoError) {
            d->setError(NetworkError::InvalidRequest,
                        QStringLiteral("Failed to configure curl options"));
        }
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

// ==================
// 执行控制
// ==================

void QCNetworkReply::execute()
{
    if (QThread::currentThread() != thread()) {
        Q_D(QCNetworkReply);
        if (d->executionMode == ExecutionMode::Async) {
            QMetaObject::invokeMethod(this, [this]() { execute(); }, Qt::QueuedConnection);
            return;
        }

        qWarning()
            << "QCNetworkReply::execute: Sync 模式不支持跨线程调用（需要在 reply 所在线程执行）";
        abortWithError(NetworkError::InvalidRequest,
                       QStringLiteral("QCNetworkReply::execute(Sync) 必须在 reply 所在线程调用"));
        return;
    }

    Q_D(QCNetworkReply);

    if (d->state == ReplyState::Running || d->state == ReplyState::Paused) {
        qWarning() << "QCNetworkReply::execute() called while already running";
        return;
    }

    if (d->state == ReplyState::Cancelled || d->state == ReplyState::Finished
        || d->state == ReplyState::Error) {
        return;
    }

    // ==================
    // 缓存集成 - 在发起网络请求前检查缓存
    // ==================
    QCNetworkAccessManager *manager = qobject_cast<QCNetworkAccessManager *>(parent());
    QCNetworkCache *cache           = manager ? manager->cache() : nullptr;
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

    // ==================
    // MockHandler 集成（离线回归/单元测试）
    // ==================
    if (manager && manager->mockHandler()) {
        auto *mock = manager->mockHandler();

        // 请求捕获：用于离线断言 middleware/header 注入、body 形态等
        if (mock->captureEnabled()) {
            QCNetworkMockHandler::CapturedRequest captured;
            captured.url    = d->request.url();
            captured.method = d->httpMethod;

            const auto headerNames = d->request.rawHeaderList();
            for (const auto &name : headerNames) {
                captured.headers.append(qMakePair(name, d->request.rawHeader(name)));
            }

            captured.bodySize      = d->requestBody.size();
            const int previewLimit = mock->captureBodyPreviewLimit();
            captured.bodyPreview   = previewLimit > 0 ? d->requestBody.left(previewLimit)
                                                      : QByteArray();
            mock->recordRequest(captured);
        }

        // 仅当存在 mock 规则时才进入回放分支；否则继续走真实网络
        if (mock->hasMock(d->httpMethod, d->request.url())) {
            // 离线回放同样应具备“可诊断配置冲突”能力（避免仅在真实网络路径提示）
            bool hasExplicitAcceptEncodingHeader = false;
            const QList<QByteArray> headerNames  = d->request.rawHeaderList();
            for (const QByteArray &headerName : headerNames) {
                if (headerName.trimmed().toLower() == QByteArrayLiteral("accept-encoding")) {
                    hasExplicitAcceptEncodingHeader = true;
                    break;
                }
            }

            if (hasExplicitAcceptEncodingHeader) {
                if (d->request.autoDecompressionEnabled()
                    || !d->request.acceptedEncodings().isEmpty()) {
                    appendCapabilityWarning(
                        d,
                        QStringLiteral("请求配置冲突：已显式设置 Accept-Encoding header，将忽略 "
                                       "autoDecompression/acceptedEncodings（不会自动解压）"));
                }
            }

            const int responseDelayMs = mock->globalDelay();

            // 进入
            // Running（与真实网络保持一致）；完成/错误信号通过延迟触发，避免在连接信号前同步发射
            d->setState(ReplyState::Running);

            // 同步模式：阻塞执行（支持重试）
            if (d->executionMode == ExecutionMode::Sync) {
                QCNetworkRetryPolicy policy = d->request.retryPolicy();

                while (true) {
                    QCNetworkMockHandler::MockData mockData;
                    if (!mock->consumeMock(d->httpMethod, d->request.url(), mockData)) {
                        d->setError(NetworkError::InvalidRequest,
                                    QStringLiteral("MockHandler: no mock matched for %1")
                                        .arg(d->request.url().toString()));
                        d->setState(ReplyState::Error);
                        return;
                    }

                    if (responseDelayMs > 0) {
                        QThread::msleep(static_cast<unsigned long>(responseDelayMs));
                    }

                    d->bodyBuffer.clear();
                    d->headerData.clear();
                    d->headerMap.clear();
                    d->bytesDownloaded = 0;
                    d->bytesUploaded   = 0;
                    d->downloadTotal   = -1;
                    d->uploadTotal     = -1;

                    if (!mockData.response.isEmpty()) {
                        d->bodyBuffer.append(mockData.response);
                        d->bytesDownloaded = mockData.response.size();
                    }

                    if (mockData.rawHeaderData.has_value()) {
                        d->headerData = mockData.rawHeaderData.value();
                    } else {
                        for (auto it = mockData.headers.cbegin(); it != mockData.headers.cend();
                             ++it) {
                            d->headerData.append(it.key());
                            d->headerData.append(": ");
                            d->headerData.append(it.value());
                            d->headerData.append('\n');
                        }
                    }

                    d->httpStatusCode = mockData.statusCode;

                    NetworkError error = NetworkError::NoError;
                    QString errorMsg;
                    if (mockData.isError && mockData.error != NetworkError::NoError) {
                        error    = mockData.error;
                        errorMsg = QCurl::errorString(error);
                    } else if (mockData.statusCode >= 400) {
                        error    = fromHttpCode(mockData.statusCode);
                        errorMsg = QStringLiteral("HTTP error %1").arg(mockData.statusCode);
                    }

                    if (error == NetworkError::NoError) {
                        if (!mockData.response.isEmpty()) {
                            emit readyRead();
                        }
                        d->setState(ReplyState::Finished);
                        return;
                    }

                    const bool httpGetOnlyBlocked = policy.retryHttpStatusErrorsForGetOnly
                                                    && isHttpError(error)
                                                    && (d->httpMethod != HttpMethod::Get);

                    if (!httpGetOnlyBlocked && policy.shouldRetry(error, d->attemptCount)) {
                        d->attemptCount++;
                        emit retryAttempt(d->attemptCount, error);

                        d->parseHeaders();
                        const std::optional<std::chrono::milliseconds> retryAfter
                            = (error == NetworkError::HttpTooManyRequests)
                                  ? parseRetryAfterDelay(d->headerMap)
                                  : std::nullopt;

                        const auto delay = policy.delayForAttempt(d->attemptCount - 1, retryAfter);
                        QThread::msleep(static_cast<unsigned long>(delay.count()));
                        continue;
                    }

                    d->setError(error, errorMsg);
                    d->setState(ReplyState::Error);
                    return;
                }
            }

            // 异步模式：以定时器回放，支持重试/取消
            QPointer<QCNetworkReply> safeThis(this);
            QTimer::singleShot(responseDelayMs > 0 ? responseDelayMs : 0, this, [safeThis]() {
                if (!safeThis) {
                    return;
                }

                auto *d = safeThis->d_func();
                if (d->state == ReplyState::Cancelled || d->state == ReplyState::Finished
                    || d->state == ReplyState::Error) {
                    return;
                }

                auto *manager = qobject_cast<QCNetworkAccessManager *>(safeThis->parent());
                auto *mock    = manager ? manager->mockHandler() : nullptr;
                if (!mock) {
                    d->setError(NetworkError::InvalidRequest,
                                QStringLiteral("MockHandler: not set"));
                    d->setState(ReplyState::Error);
                    return;
                }

                QCNetworkMockHandler::MockData mockData;
                if (!mock->consumeMock(d->httpMethod, d->request.url(), mockData)) {
                    d->setError(NetworkError::InvalidRequest,
                                QStringLiteral("MockHandler: no mock matched for %1")
                                    .arg(d->request.url().toString()));
                    d->setState(ReplyState::Error);
                    return;
                }

                d->bodyBuffer.clear();
                d->headerData.clear();
                d->headerMap.clear();
                d->bytesDownloaded = 0;
                d->bytesUploaded   = 0;
                d->downloadTotal   = -1;
                d->uploadTotal     = -1;

                if (!mockData.response.isEmpty()) {
                    d->bodyBuffer.append(mockData.response);
                    d->bytesDownloaded = mockData.response.size();
                    emit safeThis->readyRead();
                }

                if (mockData.rawHeaderData.has_value()) {
                    d->headerData = mockData.rawHeaderData.value();
                } else {
                    for (auto it = mockData.headers.cbegin(); it != mockData.headers.cend(); ++it) {
                        d->headerData.append(it.key());
                        d->headerData.append(": ");
                        d->headerData.append(it.value());
                        d->headerData.append('\n');
                    }
                }

                d->httpStatusCode = mockData.statusCode;

                NetworkError error = NetworkError::NoError;
                QString errorMsg;
                if (mockData.isError && mockData.error != NetworkError::NoError) {
                    error    = mockData.error;
                    errorMsg = QCurl::errorString(error);
                } else if (mockData.statusCode >= 400) {
                    error    = fromHttpCode(mockData.statusCode);
                    errorMsg = QStringLiteral("HTTP error %1").arg(mockData.statusCode);
                }

                if (error == NetworkError::NoError) {
                    d->setState(ReplyState::Finished);
                    return;
                }

                QCNetworkRetryPolicy policy   = d->request.retryPolicy();
                const bool httpGetOnlyBlocked = policy.retryHttpStatusErrorsForGetOnly
                                                && isHttpError(error)
                                                && (d->httpMethod != HttpMethod::Get);

                if (!httpGetOnlyBlocked && policy.shouldRetry(error, d->attemptCount)) {
                    d->attemptCount++;
                    emit safeThis->retryAttempt(d->attemptCount, error);

                    d->parseHeaders();
                    const std::optional<std::chrono::milliseconds> retryAfter
                        = (error == NetworkError::HttpTooManyRequests)
                              ? parseRetryAfterDelay(d->headerMap)
                              : std::nullopt;

                    const auto delay = policy.delayForAttempt(d->attemptCount - 1, retryAfter);

                    QPointer<QCNetworkReply> safeReply(safeThis);
                    QTimer::singleShot(delay.count(), safeThis.data(), [safeReply, d]() {
                        if (!safeReply) {
                            return;
                        }
                        if (d->state == ReplyState::Cancelled) {
                            return;
                        }

                        // 准备重试：不发射额外信号，保持与真实网络一致
                        d->state     = ReplyState::Idle;
                        d->errorCode = NetworkError::NoError;
                        d->errorMessage.clear();
                        d->bodyBuffer.clear();
                        d->headerData.clear();
                        d->headerMap.clear();
                        d->bytesDownloaded = 0;
                        d->bytesUploaded   = 0;
                        d->downloadTotal   = -1;
                        d->uploadTotal     = -1;

                        safeReply->execute();
                    });

                    return;
                }

                d->setError(error, errorMsg);
                d->setState(ReplyState::Error);
            });

            return;
        }
    }

    // ==================
    // 应用 Cookie 配置（在 QCNetworkAccessManager 中设置）
    // ==================
    // 注意：
    // - cookieFilePath/cookieMode 可能在构造函数之后通过 d_func() 设置，
    //   所以必须在 execute() 中应用，而不是在 configureCurlOptions() 中；
    // - 对于 scheduler 等路径，如果 replyParent 指向 manager 但未显式写入 d_func()，
    //   这里会回溯 manager 配置以保持行为一致性。
    CURL *handle = d->curlManager.handle();

    if (manager && d->cookieMode == 0 && d->cookieFilePath.isEmpty()) {
        const auto mode = manager->cookieFileMode();
        const auto path = manager->cookieFilePath();
        if (mode != QCNetworkAccessManager::NotOpen && !path.isEmpty()) {
            d->cookieMode     = static_cast<int>(mode);
            d->cookieFilePath = path;
        }
    }

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

    // ==================
    // HSTS/Alt-Svc cache 持久化（LC-50）：默认关闭；显式 opt-in
    // ==================

    if (handle && manager) {
        const auto cacheCfg     = manager->hstsAltSvcCacheConfig();
        d->hstsCachePathBytes   = cacheCfg.hstsFilePath.toUtf8();
        d->altSvcCachePathBytes = cacheCfg.altSvcFilePath.toUtf8();

        if (!d->hstsCachePathBytes.isEmpty()) {
#ifdef CURLOPT_HSTS
            setOptionalStringOption(d, handle, CURLOPT_HSTS, "CURLOPT_HSTS", d->hstsCachePathBytes);
#else
            appendCapabilityWarning(d, QStringLiteral("当前构建的 libcurl 不支持 CURLOPT_HSTS"));
#endif
        }

        if (!d->altSvcCachePathBytes.isEmpty()) {
#ifdef CURLOPT_ALTSVC
            setOptionalStringOption(d,
                                    handle,
                                    CURLOPT_ALTSVC,
                                    "CURLOPT_ALTSVC",
                                    d->altSvcCachePathBytes);
#else
            appendCapabilityWarning(d, QStringLiteral("当前构建的 libcurl 不支持 CURLOPT_ALTSVC"));
#endif
        }
    }

    // ==================
    // Debug trace（M5）：默认关闭；启用后强制脱敏（不输出请求体明文）
    // ==================

    if (handle && manager && manager->debugTraceEnabled() && manager->logger()) {
        curl_easy_setopt(handle, CURLOPT_VERBOSE, 1L);
        curl_easy_setopt(handle, CURLOPT_DEBUGFUNCTION, QCNetworkReplyPrivate::curlDebugCallback);
        curl_easy_setopt(handle, CURLOPT_DEBUGDATA, d);
    }

    // ==================
    // 流式上传（M2）：重试前回滚上传源到起始位置
    // ==================

    d->hasUploadErrorOverride  = false;
    d->uploadErrorOverrideCode = NetworkError::NoError;
    d->uploadErrorOverrideMessage.clear();

    if (d->uploadDevice && d->attemptCount > 0) {
        if (!d->uploadDeviceSeekable) {
            d->setError(NetworkError::InvalidRequest,
                        QStringLiteral(
                            "uploadDevice: non-seekable body 不支持自动重试（需要重发 body）"));
            d->setState(ReplyState::Error);
            return;
        }

        if (!d->uploadDevice->seek(d->uploadDeviceBasePos)) {
            d->setError(NetworkError::InvalidRequest,
                        QStringLiteral("uploadDevice: 重试需要重发 body：seek(%1) 失败")
                            .arg(d->uploadDeviceBasePos));
            d->setState(ReplyState::Error);
            return;
        }
        d->uploadBytesRead = 0;
    }

    d->setState(ReplyState::Running);

    if (d->executionMode == ExecutionMode::Async) {
        // 异步模式：注册到多句柄管理器
        QCCurlMultiManager::instance()->addReply(this);
        qDebug() << "QCNetworkReply::execute: Started async request for" << d->request.url();
    } else {
        // ==================
        // 同步模式：阻塞执行（支持重试）
        // ==================
        if (!handle) {
            d->setError(NetworkError::InvalidRequest, QStringLiteral("Invalid curl handle"));
            d->setState(ReplyState::Error);
            return;
        }

        // 获取重试策略
        QCNetworkRetryPolicy policy = d->request.retryPolicy();

        // 重试循环（attemptCount 从 0 开始，表示首次尝试）
        while (true) {
            d->hasUploadErrorOverride  = false;
            d->uploadErrorOverrideCode = NetworkError::NoError;
            d->uploadErrorOverrideMessage.clear();

            if (d->uploadDevice && d->attemptCount > 0) {
                if (!d->uploadDeviceSeekable) {
                    d->setError(
                        NetworkError::InvalidRequest,
                        QStringLiteral(
                            "uploadDevice: non-seekable body 不支持自动重试（需要重发 body）"));
                    d->setState(ReplyState::Error);
                    return;
                }

                if (!d->uploadDevice->seek(d->uploadDeviceBasePos)) {
                    d->setError(NetworkError::InvalidRequest,
                                QStringLiteral("uploadDevice: 重试需要重发 body：seek(%1) 失败")
                                    .arg(d->uploadDeviceBasePos));
                    d->setState(ReplyState::Error);
                    return;
                }
                d->uploadBytesRead = 0;
            }

            // 执行请求（阻塞调用）
            CURLcode result = curl_easy_perform(handle);

            // 检查 HTTP 状态码（即使 CURLcode 成功）
            long httpCode = 0;
            curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &httpCode);
            d->httpStatusCode = static_cast<int>(httpCode);

            // 确定最终错误：优先使用 HTTP 错误，否则使用 curl 错误
            NetworkError error = NetworkError::NoError;
            QString errorMsg;

            if (result != CURLE_OK) {
                // libcurl 层面的错误
                error    = fromCurlCode(result);
                errorMsg = QString::fromUtf8(curl_easy_strerror(result));
#if defined(CURLE_SEND_FAIL_REWIND)
                if (d->uploadDevice && !d->hasUploadErrorOverride
                    && result == CURLE_SEND_FAIL_REWIND) {
                    error    = NetworkError::InvalidRequest;
                    errorMsg = QStringLiteral("uploadDevice: 无法重发 body（seek/rewind 失败：%1）")
                                   .arg(QString::fromUtf8(curl_easy_strerror(result)));
                }
#endif
                if (d->hasUploadErrorOverride) {
                    error    = d->uploadErrorOverrideCode;
                    errorMsg = d->uploadErrorOverrideMessage;
                }
            } else if (httpCode >= 400) {
                // HTTP 错误（4xx, 5xx）
                error    = fromHttpCode(httpCode);
                errorMsg = QStringLiteral("HTTP error %1").arg(httpCode);
            }

            // 如果没有错误，标记为成功
            if (error == NetworkError::NoError) {
                qDebug() << "QCNetworkReply::execute: Sync request succeeded for"
                         << d->request.url() << "after" << d->attemptCount << "retries";
                d->setState(ReplyState::Finished);
                return;
            }

            const bool httpGetOnlyBlocked = policy.retryHttpStatusErrorsForGetOnly
                                            && isHttpError(error)
                                            && (d->httpMethod != HttpMethod::Get);

            // 检查是否应该重试
            if (!httpGetOnlyBlocked && policy.shouldRetry(error, d->attemptCount)) {
                // 增加重试计数
                d->attemptCount++;

                // 发射重试尝试信号
                emit retryAttempt(d->attemptCount, error);

                // 计算延迟时间
                std::optional<std::chrono::milliseconds> retryAfter;
                if (error == NetworkError::HttpTooManyRequests) {
                    d->parseHeaders();
                    retryAfter = parseRetryAfterDelay(d->headerMap);
                }

                auto delay = policy.delayForAttempt(d->attemptCount - 1, retryAfter);

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
                d->bytesUploaded   = 0;
                d->downloadTotal   = -1;
                d->uploadTotal     = -1;

                // 继续循环重试
                continue;
            }

            // 超过最大重试次数或错误不可重试
            qDebug() << "QCNetworkReply::execute: Sync request failed after" << d->attemptCount
                     << "attempts. Error:" << errorMsg;

            d->setError(error, errorMsg);
            d->setState(ReplyState::Error);
            return;
        }
    }
}

void QCNetworkReply::cancel()
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this]() { cancel(); }, Qt::QueuedConnection);
        return;
    }

    Q_D(QCNetworkReply);

    // 如果已经取消或已完成，不需要再操作
    if (d->state == ReplyState::Cancelled || d->state == ReplyState::Finished
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
    d->setError(NetworkError::OperationCancelled,
                QCurl::errorString(NetworkError::OperationCancelled));

    // 设置取消状态（这会发射 cancelled 信号）
    // 注意：即使在 Idle 状态（重试延迟期间）也允许取消
    d->setState(ReplyState::Cancelled);
}

void QCNetworkReply::abortWithError(NetworkError error, const QString &message)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(
            this,
            [this, error, message]() { abortWithError(error, message); },
            Qt::QueuedConnection);
        return;
    }

    Q_D(QCNetworkReply);

    if (d->state == ReplyState::Cancelled || d->state == ReplyState::Finished
        || d->state == ReplyState::Error) {
        return;
    }

    if (d->executionMode == ExecutionMode::Async
        && (d->state == ReplyState::Running || d->state == ReplyState::Paused)) {
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

    const QString resolvedMessage = message.isEmpty() ? QCurl::errorString(error) : message;
    d->setError(error, resolvedMessage);
    d->setState(ReplyState::Error);
}

void QCNetworkReply::pauseTransport(PauseMode mode)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(
            this, [this, mode]() { pauseTransport(mode); }, Qt::QueuedConnection);
        return;
    }

    Q_D(QCNetworkReply);

    if (d->executionMode == ExecutionMode::Sync) {
        qWarning() << "QCNetworkReply::pauseTransport: Sync mode does not support transfer pause/resume";
        return;
    }

    if (d->state != ReplyState::Running) {
        return;
    }

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

    const int oldUserMask = d->userPauseMask;
    d->userPauseMask      = flags;

    // P2-0：当 pause 从 libcurl 回调链路（如 progress callback）内发起时，优先在 write callback
    // 边界返回 CURL_WRITEFUNC_PAUSE 以生效，避免在回调栈内直接调用 curl_easy_pause。
    const bool deferRecvToWriteCallbackBoundary = (flags == CURLPAUSE_RECV) && isInCurlCallback();
    if (!deferRecvToWriteCallbackBoundary) {
        if (!d->applyPauseMask(d->desiredPauseMask())) {
            d->userPauseMask = oldUserMask;
            return;
        }
    }

    d->setState(ReplyState::Paused);
}

void QCNetworkReply::resumeTransport()
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this]() { resumeTransport(); }, Qt::QueuedConnection);
        return;
    }

    Q_D(QCNetworkReply);

    if (d->executionMode == ExecutionMode::Sync) {
        qWarning()
            << "QCNetworkReply::resumeTransport: Sync mode does not support transfer pause/resume";
        return;
    }

    if (d->state != ReplyState::Paused) {
        return;
    }

    const int oldUserMask = d->userPauseMask;
    d->userPauseMask      = 0;

    if (!d->applyPauseMask(d->desiredPauseMask())) {
        d->userPauseMask = oldUserMask;
        return;
    }

    d->setState(ReplyState::Running);
    QCCurlMultiManager::instance()->wakeup();
}

void QCNetworkReply::pause()
{
    pauseTransport(PauseMode::All);
}

void QCNetworkReply::pause(PauseMode mode)
{
    pauseTransport(mode);
}

void QCNetworkReply::resume()
{
    resumeTransport();
}

// ==================
// 数据访问（现代 C++17 风格）
// ==================

std::optional<QByteArray> QCNetworkReply::readAll() const
{
    Q_D(const QCNetworkReply);

    if (d->bodyBuffer.isEmpty()) {
        // 约定：在“终态且响应体为空”的场景下，返回空 QByteArray（而不是 std::nullopt），
        // 否则会把“空 body”与“尚无数据可读”混为一谈，导致可观测层面不可区分。
        if (d->state == ReplyState::Finished || d->state == ReplyState::Error
            || d->state == ReplyState::Cancelled) {
            return QByteArray();
        }
        return std::nullopt;
    }

    // 注意：这会清空缓冲区（对异步和同步模式都适用）
    QCByteDataBuffer &buffer = const_cast<QCByteDataBuffer &>(d->bodyBuffer);
    QByteArray out           = buffer.readAll();

    // P2-1：backpressure 自动恢复（仅在启用且处于内部 pause 时触发）
    if (d->executionMode == ExecutionMode::Async && d->backpressureLimitBytes > 0
        && (d->internalPauseMask & CURLPAUSE_RECV)) {
        QPointer<QCNetworkReply> safeThis(const_cast<QCNetworkReply *>(this));
        QMetaObject::invokeMethod(
            const_cast<QCNetworkReply *>(this),
            [safeThis]() {
                if (!safeThis) {
                    return;
                }
                safeThis->d_func()->maybeResumeRecvFromBackpressure();
            },
            Qt::QueuedConnection);
    }
    return out;
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

    return readAll(); // 别名方法
}

QList<RawHeaderPair> QCNetworkReply::rawHeaders() const
{
    Q_D(const QCNetworkReply);

    // 安全检查：错误状态或未完成状态下 headers 可能未初始化
    if (!d || d->state == ReplyState::Error || d->state == ReplyState::Idle) {
        qWarning() << "QCNetworkReply::rawHeaders: Invalid state"
                   << (d ? static_cast<int>(d->state) : -1);
        return QList<RawHeaderPair>(); // 返回空列表而非崩溃
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

HttpMethod QCNetworkReply::method() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->httpMethod;
}

int QCNetworkReply::httpStatusCode() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->httpStatusCode;
}

qint64 QCNetworkReply::durationMs() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->durationMs;
}

qint64 QCNetworkReply::bytesAvailable() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->bodyBuffer.byteAmount();
}

QStringList QCNetworkReply::capabilityWarnings() const
{
    Q_D(const QCNetworkReply);
    return d->capabilityWarnings;
}

// ==================
// 状态查询
// ==================

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
    return d->state == ReplyState::Finished || d->state == ReplyState::Cancelled
           || d->state == ReplyState::Error;
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

bool QCNetworkReply::isBackpressureActive() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->backpressureLimitBytes > 0 && d->backpressureActive;
}

bool QCNetworkReply::isUploadSendPaused() const noexcept
{
    Q_D(const QCNetworkReply);
    if (d->executionMode != ExecutionMode::Async) {
        return false;
    }
    if (d->state == ReplyState::Cancelled || d->state == ReplyState::Error
        || d->state == ReplyState::Finished) {
        return false;
    }
    return d->uploadSendPaused && ((d->internalPauseMask & CURLPAUSE_SEND) != 0);
}

qint64 QCNetworkReply::backpressureLimitBytes() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->backpressureLimitBytes > 0 ? d->backpressureLimitBytes : 0;
}

qint64 QCNetworkReply::backpressureResumeBytes() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->backpressureLimitBytes > 0 ? d->backpressureResumeBytes : 0;
}

qint64 QCNetworkReply::backpressureBufferedBytesPeak() const noexcept
{
    Q_D(const QCNetworkReply);
    return d->backpressureLimitBytes > 0 ? d->backpressurePeakBufferedBytes : 0;
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

// ==================
// 同步模式专用 API
// ==================

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

// ==================
// 公共槽
// ==================

void QCNetworkReply::deleteLater()
{
    QObject::deleteLater();
}

// ==================
// 缓存集成私有方法实现
// ==================

bool QCNetworkReply::loadFromCache(bool ignoreExpiry)
{
    Q_D(QCNetworkReply);

    QCNetworkAccessManager *manager = qobject_cast<QCNetworkAccessManager *>(parent());
    QCNetworkCache *cache           = manager ? manager->cache() : nullptr;
    if (!cache) {
        return false;
    }

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
    auto flushCurrent = [&](const QByteArray &name, const QList<QByteArray> &segments) {
        if (name.isEmpty()) {
            return;
        }
        QByteArray value;
        for (const QByteArray &seg : segments) {
            const QByteArray part = seg.trimmed();
            if (part.isEmpty()) {
                continue;
            }
            if (!value.isEmpty()) {
                value.append(' ');
            }
            value.append(part);
        }
        headers.insert(name, value);
    };

    QByteArray currentName;
    QList<QByteArray> currentSegments;

    const QList<QByteArray> lines = d->headerData.split('\n');
    for (QByteArray line : lines) {
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        if (line.isEmpty()) {
            flushCurrent(currentName, currentSegments);
            currentName.clear();
            currentSegments.clear();
            continue;
        }
        if (line.startsWith("HTTP/")) {
            flushCurrent(currentName, currentSegments);
            currentName.clear();
            currentSegments.clear();
            continue;
        }
        const bool isContinuation = !currentName.isEmpty()
                                    && (line.startsWith(' ') || line.startsWith('\t'));
        if (isContinuation) {
            currentSegments.append(line);
            continue;
        }

        const int colonPos = line.indexOf(':');
        if (colonPos <= 0) {
            continue;
        }
        flushCurrent(currentName, currentSegments);
        currentName = line.left(colonPos).trimmed();
        currentSegments.clear();
        currentSegments.append(line.mid(colonPos + 1));
    }

    flushCurrent(currentName, currentSegments);

    return headers;
}

} // namespace QCurl
