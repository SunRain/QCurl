/**
 * @file
 * @brief QCNetworkReply libcurl option configuration.
 */

#include "QCNetworkReply_p.h"

#include "CurlFeatureProbe.h"
#include "QCNetworkConnectionPoolManager_p.h"
#include "QCNetworkError.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRetryPolicy.h"
#include "QCNetworkSslConfig.h"
#include "QCNetworkTimeoutConfig.h"
#include "private/QCNetworkHttpVersion_p.h"
#include "private/QCNetworkReplyRuntime_p.h"

#include <QDebug>
#include <QFileInfo>
#include <QIODevice>
#include <QUrl>

#include <limits>
#include <optional>

namespace QCurl {

namespace {

bool isCapabilityRelatedCurlError(CURLcode code)
{
    return Internal::isReplyCapabilityRelatedCurlError(code);
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

constexpr const char kTestCurlPlanDigestProperty[] = "_qcurl_testCurlPlanDigest";

template<typename T>
CURLcode curlEasySetoptWithTestHook(CURL *handle, CURLoption option, const char *optionName, T value)
{
#ifdef QCURL_ENABLE_TEST_HOOKS
    // 测试环境可按 option 名定向注入 capability 缺失，用于验证降级路径。
    if (shouldForceCapabilityErrorForOption(optionName)) {
        return CURLE_NOT_BUILT_IN;
    }
#endif
    return curl_easy_setopt(handle, option, value);
}

void appendCapabilityWarning(QCNetworkReplyPrivate *d, const QString &message)
{
    Internal::appendReplyCapabilityWarning(d, message);
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


} // namespace


namespace {

[[nodiscard]] bool configureReplyBaseAndNetworkOptions(QCNetworkReplyPrivate *reply,
                                                       CURL *handle,
                                                       const Internal::NormalizedRequest &normalized,
                                                       const Internal::RequestBody &bodySpec)
{
    const QCNetworkRequest &request = normalized.request;
    const HttpMethod httpMethod = normalized.method;
    // ==================
    // 1. 基础配置
    // ==================

    // 设置 URL
    QByteArray urlBytes = request.url().toString().toUtf8();
    curl_easy_setopt(handle, CURLOPT_URL, urlBytes.constData());

    // 设置私有数据（用于回调中识别对象）
    curl_easy_setopt(handle, CURLOPT_PRIVATE, reply);

    // 跟随重定向
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, request.followLocation() ? 1L : 0L);

    // ==================
    // 1.1 重定向策略（M1）
    // ==================

    if (request.followLocation()) {
        if (const auto maxRedirects = request.maxRedirects(); maxRedirects.has_value()) {
            setOptionalLongOption(reply,
                                  handle,
                                  CURLOPT_MAXREDIRS,
                                  "CURLOPT_MAXREDIRS",
                                  static_cast<long>(maxRedirects.value()));
        }

        switch (request.postRedirectPolicy()) {
            case QCNetworkPostRedirectPolicy::Default:
                break;
            case QCNetworkPostRedirectPolicy::KeepPost301:
                setOptionalLongOption(reply,
                                      handle,
                                      CURLOPT_POSTREDIR,
                                      "CURLOPT_POSTREDIR",
                                      CURL_REDIR_POST_301);
                break;
            case QCNetworkPostRedirectPolicy::KeepPost302:
                setOptionalLongOption(reply,
                                      handle,
                                      CURLOPT_POSTREDIR,
                                      "CURLOPT_POSTREDIR",
                                      CURL_REDIR_POST_302);
                break;
            case QCNetworkPostRedirectPolicy::KeepPost303:
                setOptionalLongOption(reply,
                                      handle,
                                      CURLOPT_POSTREDIR,
                                      "CURLOPT_POSTREDIR",
                                      CURL_REDIR_POST_303);
                break;
            case QCNetworkPostRedirectPolicy::KeepPostAll:
                setOptionalLongOption(reply,
                                      handle,
                                      CURLOPT_POSTREDIR,
                                      "CURLOPT_POSTREDIR",
                                      CURL_REDIR_POST_ALL);
                break;
        }

        if (request.autoRefererEnabled()) {
            setOptionalLongOption(reply, handle, CURLOPT_AUTOREFERER, "CURLOPT_AUTOREFERER", 1L);
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

        setOptionalLongOption(reply, handle, CURLOPT_IPRESOLVE, "CURLOPT_IPRESOLVE", ipResolveValue);
    }

#ifdef QCURL_ENABLE_ADVANCED_REQUEST_NETWORK_PATH_API
    if (const auto heOpt = request.happyEyeballsTimeout(); heOpt.has_value()) {
        setOptionalLongOption(reply,
                              handle,
                              CURLOPT_HAPPY_EYEBALLS_TIMEOUT_MS,
                              "CURLOPT_HAPPY_EYEBALLS_TIMEOUT_MS",
                              static_cast<long>(heOpt.value().count()));
    }

    if (const auto ifaceOpt = request.networkInterface(); ifaceOpt.has_value()) {
        reply->interfaceBytes = ifaceOpt.value().toUtf8();
        setOptionalStringOption(reply, handle, CURLOPT_INTERFACE, "CURLOPT_INTERFACE", reply->interfaceBytes);
    }

    if (const auto localPortOpt = request.localPort(); localPortOpt.has_value()) {
        setOptionalLongOption(reply,
                              handle,
                              CURLOPT_LOCALPORT,
                              "CURLOPT_LOCALPORT",
                              static_cast<long>(localPortOpt.value()));
    }

    if (const auto localPortRangeOpt = request.localPortRange(); localPortRangeOpt.has_value()) {
        setOptionalLongOption(reply,
                              handle,
                              CURLOPT_LOCALPORTRANGE,
                              "CURLOPT_LOCALPORTRANGE",
                              static_cast<long>(localPortRangeOpt.value()));
    }

    if (const auto resolveOverrideOpt = request.resolveOverride(); resolveOverrideOpt.has_value()) {
        reply->resolveSlist = buildSlistFromStrings(resolveOverrideOpt.value());
        if (!reply->resolveSlist) {
            appendCapabilityWarning(reply, QStringLiteral("CURLOPT_RESOLVE: 构造参数列表失败"));
        } else {
            if (!setOptionalSlistOption(reply,
                                        handle,
                                        CURLOPT_RESOLVE,
                                        "CURLOPT_RESOLVE",
                                        reply->resolveSlist)) {
                curl_slist_free_all(reply->resolveSlist);
                reply->resolveSlist = nullptr;
            }
        }
    }

    if (const auto connectToOpt = request.connectTo(); connectToOpt.has_value()) {
        reply->connectToSlist = buildSlistFromStrings(connectToOpt.value());
        if (!reply->connectToSlist) {
            appendCapabilityWarning(reply, QStringLiteral("CURLOPT_CONNECT_TO: 构造参数列表失败"));
        } else {
            if (!setOptionalSlistOption(reply,
                                        handle,
                                        CURLOPT_CONNECT_TO,
                                        "CURLOPT_CONNECT_TO",
                                        reply->connectToSlist)) {
                curl_slist_free_all(reply->connectToSlist);
                reply->connectToSlist = nullptr;
            }
        }
    }

    if (const auto dnsServersOpt = request.dnsServers(); dnsServersOpt.has_value()) {
        reply->dnsServersBytes = dnsServersOpt.value().join(QStringLiteral(",")).toUtf8();
        setOptionalStringOption(reply,
                                handle,
                                CURLOPT_DNS_SERVERS,
                                "CURLOPT_DNS_SERVERS",
                                reply->dnsServersBytes);
    }

    if (const auto dohUrlOpt = request.dohUrl(); dohUrlOpt.has_value()) {
        reply->dohUrlBytes = dohUrlOpt.value().toString().toUtf8();
        setOptionalStringOption(reply, handle, CURLOPT_DOH_URL, "CURLOPT_DOH_URL", reply->dohUrlBytes);
    }
#endif

    // ==================
    // 1.4 协议白名单（M5，安全）
    // ==================

    const QCUnsupportedSecurityOptionPolicy securityPolicy = request
                                                                 .unsupportedSecurityOptionPolicy();

    if (const auto allowedOpt = request.allowedProtocols(); allowedOpt.has_value()) {
        reply->allowedProtocolsBytes = allowedOpt.value().join(QStringLiteral(",")).toUtf8();
        const CURLcode rc = curlEasySetoptWithTestHook(handle,
                                                       CURLOPT_PROTOCOLS_STR,
                                                       "CURLOPT_PROTOCOLS_STR",
                                                       reply->allowedProtocolsBytes.constData());
        if (rc == CURLE_OK) {
            // no-op
        } else if (isCapabilityRelatedCurlError(rc)) {
            const QString msg = QStringLiteral(
                                    "allowedProtocols 协议白名单未生效：当前 libcurl 不支持 "
                                    "CURLOPT_PROTOCOLS_STR（%1）")
                                    .arg(QString::fromUtf8(curl_easy_strerror(rc)));
            if (securityPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                reply->setError(NetworkError::InvalidRequest, msg);
                return false;
            }
            appendCapabilityWarning(reply, msg);
        } else {
            reply->setError(NetworkError::InvalidRequest,
                     QStringLiteral("设置 CURLOPT_PROTOCOLS_STR 失败（%1）")
                         .arg(QString::fromUtf8(curl_easy_strerror(rc))));
            return false;
        }
    }

    if (const auto redirOpt = request.allowedRedirectProtocols(); redirOpt.has_value()) {
        reply->allowedRedirectProtocolsBytes = redirOpt.value().join(QStringLiteral(",")).toUtf8();
        const CURLcode rc = curlEasySetoptWithTestHook(handle,
                                                       CURLOPT_REDIR_PROTOCOLS_STR,
                                                       "CURLOPT_REDIR_PROTOCOLS_STR",
                                                       reply->allowedRedirectProtocolsBytes.constData());
        if (rc == CURLE_OK) {
            // no-op
        } else if (isCapabilityRelatedCurlError(rc)) {
            const QString msg = QStringLiteral(
                                    "allowedRedirectProtocols 重定向协议白名单未生效：当前 "
                                    "libcurl 不支持 CURLOPT_REDIR_PROTOCOLS_STR（%1）")
                                    .arg(QString::fromUtf8(curl_easy_strerror(rc)));
            if (securityPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                reply->setError(NetworkError::InvalidRequest, msg);
                return false;
            }
            appendCapabilityWarning(reply, msg);
        } else {
            reply->setError(NetworkError::InvalidRequest,
                     QStringLiteral("设置 CURLOPT_REDIR_PROTOCOLS_STR 失败（%1）")
                         .arg(QString::fromUtf8(curl_easy_strerror(rc))));
            return false;
        }
    }

    // 多线程安全（禁用信号处理）
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);

    // ==================
    // 1.2 流式上传源准备（M2）
    // ==================

    QString bodySourceError;
    if (!Internal::prepareReplyBodySource(reply, bodySpec, &bodySourceError)) {
        reply->setError(NetworkError::InvalidRequest, bodySourceError);
        return false;
    }

    if (reply->requestBodySource.device && reply->q_ptr) {
        QObject::connect(reply->requestBodySource.device, &QIODevice::readyRead, reply->q_ptr, [reply]() {
            reply->resumeSendFromRequestBodySourceIfNeeded();
        });
    }

    if (reply->requestBodySource.device && httpMethod != HttpMethod::Post && httpMethod != HttpMethod::Put) {
        reply->setError(NetworkError::InvalidRequest,
                 QStringLiteral("request body source 仅支持 PUT/POST（当前方法未支持）"));
        return false;
    }

    if (reply->requestBodySource.device && request.retryPolicy().isEnabled() && !reply->requestBodySource.seekable) {
        reply->setError(NetworkError::InvalidRequest,
                 QStringLiteral("request body source: non-seekable body 不支持自动重试（需要重发 "
                                "body；请关闭 retryPolicy 或使用 seekable 来源）"));
        return false;
    }


    return true;
}

[[nodiscard]] bool configureReplyMethodHeadersAndLimits(QCNetworkReplyPrivate *reply,
                                                        CURL *handle,
                                                        const Internal::CurlPlan &plan,
                                                        const Internal::NormalizedRequest &normalized,
                                                        const Internal::RequestBody &bodySpec,
                                                        bool *hasExplicitAuthorizationHeaderOut,
                                                        bool *hasSensitiveHeaderOut)
{
    const QCNetworkRequest &request = normalized.request;
    const HttpMethod httpMethod = normalized.method;
    const QByteArray &requestBody = bodySpec.inlineBytes;
    // ==================
    // 2. HTTP 方法配置
    // ==================

    if (plan.setNoBody) {
        curl_easy_setopt(handle, CURLOPT_NOBODY, 1L);
    }

    if (plan.setHttpGet) {
        curl_easy_setopt(handle, CURLOPT_HTTPGET, 1L);
    }

    if (plan.setPost) {
        curl_easy_setopt(handle, CURLOPT_POST, 1L);
    }

    if (plan.setUpload) {
        curl_easy_setopt(handle, CURLOPT_UPLOAD, 1L);
    }

    if (!plan.customRequest.isEmpty()) {
        curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, plan.customRequest.constData());
    }

    switch (plan.transferMode) {
        case Internal::CurlTransferMode::None:
            break;
        case Internal::CurlTransferMode::InlineBytes:
            curl_easy_setopt(handle, CURLOPT_POSTFIELDS, requestBody.constData());
            curl_easy_setopt(handle,
                             CURLOPT_POSTFIELDSIZE_LARGE,
                             static_cast<curl_off_t>(plan.bodySizeBytes));
            break;
        case Internal::CurlTransferMode::RequestBodySource:
            if (!reply->requestBodySource.device) {
                reply->setError(NetworkError::InvalidRequest,
                         QStringLiteral("request body source 需要有效的设备来源"));
                return false;
            }
            curl_easy_setopt(handle, CURLOPT_READFUNCTION, QCNetworkReplyPrivate::curlReadCallback);
            curl_easy_setopt(handle, CURLOPT_READDATA, reply);
            if (plan.setPost) {
                curl_easy_setopt(handle, CURLOPT_POSTFIELDS, nullptr);
                curl_easy_setopt(handle,
                                 CURLOPT_POSTFIELDSIZE_LARGE,
                                 static_cast<curl_off_t>(reply->requestBodySource.sizeBytes));
            } else if (plan.setUpload) {
                curl_easy_setopt(handle,
                                 CURLOPT_INFILESIZE_LARGE,
                                 static_cast<curl_off_t>(reply->requestBodySource.sizeBytes));
            }
            break;
    }

    // ==================
    // 2.1 Expect: 100-continue 等待超时（P1，可选）
    // ==================

    if (const auto timeoutOpt = request.expect100ContinueTimeout(); timeoutOpt.has_value()) {
        const bool isPutOrPost = httpMethod == HttpMethod::Put || httpMethod == HttpMethod::Post;
        const bool hasBody     = plan.hasRequestBody;
        if (!isPutOrPost || !hasBody) {
            appendCapabilityWarning(
                reply,
                QStringLiteral(
                    "请求配置：Expect: 100-continue timeout 仅对 PUT/POST 且有 body 生效，已忽略"));
        } else {
            const long long timeoutMs = static_cast<long long>(timeoutOpt.value().count());
            if (timeoutMs < 0) {
                appendCapabilityWarning(
                    reply,
                    QStringLiteral("请求配置：Expect: 100-continue timeout 必须 >= 0（已忽略）"));
            } else {
                long timeoutMsLong = 0;
                if (timeoutMs > static_cast<long long>(std::numeric_limits<long>::max())) {
                    timeoutMsLong = std::numeric_limits<long>::max();
                    appendCapabilityWarning(
                        reply,
                        QStringLiteral(
                            "请求配置：Expect: 100-continue timeout 过大，已截断为 LONG_MAX ms"));
                } else {
                    timeoutMsLong = static_cast<long>(timeoutMs);
                }
                setOptionalLongOption(reply,
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
        QString headerLine = QString::fromUtf8(headerName) + QStringLiteral(": ")
                             + QString::fromUtf8(headerValue);
        reply->curlManager.appendHeader(headerLine);
    }

    // 应用 header 列表
    if (reply->curlManager.headerList()) {
        curl_easy_setopt(handle, CURLOPT_HTTPHEADER, reply->curlManager.headerList());
    }

    // ==================
    // 3.1 Referer / 自动解压（M1）
    // ==================

    if (!hasExplicitRefererHeader) {
        const QString referer = request.referer();
        if (!referer.isEmpty()) {
            reply->refererBytes = referer.toUtf8();
            setOptionalStringOption(reply, handle, CURLOPT_REFERER, "CURLOPT_REFERER", reply->refererBytes);
        }
    } else if (!request.referer().isEmpty()) {
        appendCapabilityWarning(
            reply,
            QStringLiteral(
                "请求配置冲突：已显式设置 Referer header，将忽略 request.setReferer(...)"));
    }

    if (hasExplicitAcceptEncodingHeader) {
        if (request.autoDecompressionEnabled() || !request.acceptedEncodings().isEmpty()) {
            appendCapabilityWarning(reply,
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
                reply->acceptEncodingBytes = normalized.join(QLatin1Char(',')).toUtf8();
                setOptionalStringOption(reply,
                                        handle,
                                        CURLOPT_ACCEPT_ENCODING,
                                        "CURLOPT_ACCEPT_ENCODING",
                                        reply->acceptEncodingBytes);
            }
        } else {
            // 空字符串：让 libcurl 使用其内置支持的编码列表，并启用自动解压
            reply->acceptEncodingBytes = QByteArray("");
            setOptionalStringOption(reply,
                                    handle,
                                    CURLOPT_ACCEPT_ENCODING,
                                    "CURLOPT_ACCEPT_ENCODING",
                                    reply->acceptEncodingBytes);
        }
    }

    // ==================
    // 3.2 传输限速（M1）
    // ==================

    if (const auto maxDownloadBytesPerSec = request.maxDownloadBytesPerSec();
        maxDownloadBytesPerSec.has_value()) {
        const qint64 bytesPerSec = maxDownloadBytesPerSec.value();
        if (bytesPerSec > 0) {
            setOptionalOffTOption(reply,
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
            setOptionalOffTOption(reply,
                                  handle,
                                  CURLOPT_MAX_SEND_SPEED_LARGE,
                                  "CURLOPT_MAX_SEND_SPEED_LARGE",
                                  static_cast<curl_off_t>(bytesPerSec));
        }
    }


    if (hasExplicitAuthorizationHeaderOut) {
        *hasExplicitAuthorizationHeaderOut = hasExplicitAuthorizationHeader;
    }
    if (hasSensitiveHeaderOut) {
        *hasSensitiveHeaderOut = hasSensitiveHeader;
    }
    return true;
}

[[nodiscard]] bool configureReplyProxyAndHttpVersion(QCNetworkReplyPrivate *reply,
                                                     CURL *handle,
                                                     const Internal::NormalizedRequest &normalized,
                                                     bool hasExplicitAuthorizationHeader,
                                                     bool hasSensitiveHeader)
{
    const QCNetworkRequest &request = normalized.request;
    // ==================
    // 4. HTTP 认证（Authorization: Basic / HTTPAUTH）
    // ==================

    bool wantsUnrestrictedSensitiveHeaders = request.allowUnrestrictedSensitiveHeadersOnRedirect();

    const auto httpAuthOpt = request.httpAuth();
    if (httpAuthOpt.has_value()) {
        const auto &cfg = httpAuthOpt.value();

        // Basic over HTTP 风险提示（不阻断）
        if (cfg.method() == QCNetworkHttpAuthMethod::Basic && cfg.warnIfBasicOverHttp()
            && request.url().scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) == 0) {
            qWarning() << "QCNetworkReply: Basic authentication over HTTP is insecure, consider "
                          "HTTPS. url="
                       << request.url().toString();
        }

        // 显式 Authorization header 优先：存在时不启用 libcurl 自动认证（避免重复/覆盖不确定性）
        if (!hasExplicitAuthorizationHeader) {
            hasSensitiveHeader = true;
            if (cfg.allowUnrestrictedAuth() && request.followLocation()) {
                wantsUnrestrictedSensitiveHeaders = true;
            }

            reply->httpAuthUserBytes     = cfg.userName().toUtf8();
            reply->httpAuthPasswordBytes = cfg.password().toUtf8();

            curl_easy_setopt(handle, CURLOPT_USERNAME, reply->httpAuthUserBytes.constData());
            curl_easy_setopt(handle, CURLOPT_PASSWORD, reply->httpAuthPasswordBytes.constData());

            unsigned long httpAuth = CURLAUTH_BASIC;
            switch (cfg.method()) {
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
        appendCapabilityWarning(reply,
                                QStringLiteral("安全风险：已启用跨站发送敏感头（CURLOPT_"
                                               "UNRESTRICTED_AUTH=1），请确认重定向目标可信"));
        setOptionalLongOption(reply,
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
        if (proxyConfig.type() != QCNetworkProxyConfig::ProxyType::None) {
            if (!proxyConfig.isValid()) {
                qWarning() << "QCNetworkReply: invalid proxy configuration ignored";
            } else {
                reply->proxyHostBytes = proxyConfig.hostName().toUtf8();
                curl_easy_setopt(handle, CURLOPT_PROXY, reply->proxyHostBytes.constData());

                if (proxyConfig.port() > 0) {
                    curl_easy_setopt(handle,
                                     CURLOPT_PROXYPORT,
                                     static_cast<long>(proxyConfig.port()));
                }

                long proxyType = CURLPROXY_HTTP;
                switch (proxyConfig.type()) {
                    case QCNetworkProxyConfig::ProxyType::Http:
                        proxyType = CURLPROXY_HTTP;
                        break;
                    case QCNetworkProxyConfig::ProxyType::Https:
#ifdef CURLPROXY_HTTPS
                        proxyType = CURLPROXY_HTTPS;
#else
                        if (const auto proxyTlsConfigOpt = proxyConfig.tlsConfig();
                            proxyTlsConfigOpt.has_value()
                            && proxyTlsConfigOpt->unsupportedSecurityPolicy()
                                   == QCUnsupportedSecurityOptionPolicy::Fail) {
                            reply->setError(NetworkError::InvalidRequest,
                                     QStringLiteral("当前构建的 libcurl 不支持 HTTPS "
                                                    "代理（CURLPROXY_HTTPS 未定义）"));
                            return false;
                        }
                        proxyType = CURLPROXY_HTTP;
                        appendCapabilityWarning(
                            reply,
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

                if (!proxyConfig.userName().isEmpty()) {
                    reply->proxyUserBytes = proxyConfig.userName().toUtf8();
                    curl_easy_setopt(handle, CURLOPT_PROXYUSERNAME, reply->proxyUserBytes.constData());
                } else {
                    reply->proxyUserBytes.clear();
                }

                if (!proxyConfig.password().isEmpty()) {
                    reply->proxyPasswordBytes = proxyConfig.password().toUtf8();
                    curl_easy_setopt(handle, CURLOPT_PROXYPASSWORD, reply->proxyPasswordBytes.constData());
                } else {
                    reply->proxyPasswordBytes.clear();
                }

                if (!proxyConfig.userName().isEmpty() || !proxyConfig.password().isEmpty()) {
                    curl_easy_setopt(handle, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
                }

                // ==================
                // Proxy TLS（M5，可选）：仅 HTTPS proxy + 显式配置时生效
                // ==================

#ifdef CURLPROXY_HTTPS
                if (const auto proxyTlsConfigOpt = proxyConfig.tlsConfig();
                    proxyConfig.type() == QCNetworkProxyConfig::ProxyType::Https
                    && proxyTlsConfigOpt.has_value() && proxyType == CURLPROXY_HTTPS) {
                    const auto &tlsCfg = proxyTlsConfigOpt.value();
                    const QCUnsupportedSecurityOptionPolicy tlsPolicy
                        = tlsCfg.unsupportedSecurityPolicy();

#if LIBCURL_VERSION_NUM >= 0x073400 /* 7.52.0 */
                    {
                        const CURLcode rc
                            = curlEasySetoptWithTestHook(handle,
                                                         CURLOPT_PROXY_SSL_VERIFYPEER,
                                                         "CURLOPT_PROXY_SSL_VERIFYPEER",
                                                         tlsCfg.verifyPeer() ? 1L : 0L);
                        if (rc != CURLE_OK) {
                            if (isCapabilityRelatedCurlError(rc)) {
                                const QString msg
                                    = QStringLiteral(
                                          "libcurl 不支持 CURLOPT_PROXY_SSL_VERIFYPEER（%1）")
                                          .arg(QString::fromUtf8(curl_easy_strerror(rc)));
                                if (tlsPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                                    reply->setError(NetworkError::InvalidRequest, msg);
                                    return false;
                                }
                                appendCapabilityWarning(reply, msg);
                            } else {
                                reply->setError(NetworkError::InvalidRequest,
                                         QStringLiteral(
                                             "设置 CURLOPT_PROXY_SSL_VERIFYPEER 失败（%1）")
                                             .arg(QString::fromUtf8(curl_easy_strerror(rc))));
                                return false;
                            }
                        }
                    }
#else
                    if (tlsPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                        reply->setError(NetworkError::InvalidRequest,
                                 QStringLiteral(
                                     "当前构建的 libcurl 不支持 CURLOPT_PROXY_SSL_VERIFYPEER"));
                        return false;
                    }
                    appendCapabilityWarning(
                        reply,
                        QStringLiteral("当前构建的 libcurl 不支持 CURLOPT_PROXY_SSL_VERIFYPEER"));
#endif

#if LIBCURL_VERSION_NUM >= 0x073400 /* 7.52.0 */
                    {
                        const CURLcode rc
                            = curlEasySetoptWithTestHook(handle,
                                                         CURLOPT_PROXY_SSL_VERIFYHOST,
                                                         "CURLOPT_PROXY_SSL_VERIFYHOST",
                                                         tlsCfg.verifyHost() ? 2L : 0L);
                        if (rc != CURLE_OK) {
                            if (isCapabilityRelatedCurlError(rc)) {
                                const QString msg
                                    = QStringLiteral(
                                          "libcurl 不支持 CURLOPT_PROXY_SSL_VERIFYHOST（%1）")
                                          .arg(QString::fromUtf8(curl_easy_strerror(rc)));
                                if (tlsPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                                    reply->setError(NetworkError::InvalidRequest, msg);
                                    return false;
                                }
                                appendCapabilityWarning(reply, msg);
                            } else {
                                reply->setError(NetworkError::InvalidRequest,
                                         QStringLiteral(
                                             "设置 CURLOPT_PROXY_SSL_VERIFYHOST 失败（%1）")
                                             .arg(QString::fromUtf8(curl_easy_strerror(rc))));
                                return false;
                            }
                        }
                    }
#else
                    if (tlsPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                        reply->setError(NetworkError::InvalidRequest,
                                 QStringLiteral(
                                     "当前构建的 libcurl 不支持 CURLOPT_PROXY_SSL_VERIFYHOST"));
                        return false;
                    }
                    appendCapabilityWarning(
                        reply,
                        QStringLiteral("当前构建的 libcurl 不支持 CURLOPT_PROXY_SSL_VERIFYHOST"));
#endif

                    if (!tlsCfg.caCertPath().isEmpty()) {
#if LIBCURL_VERSION_NUM >= 0x073400 /* 7.52.0 */
                        reply->proxySslCaCertPathBytes = tlsCfg.caCertPath().toUtf8();
                        const CURLcode rc
                            = curlEasySetoptWithTestHook(handle,
                                                         CURLOPT_PROXY_CAINFO,
                                                         "CURLOPT_PROXY_CAINFO",
                                                         reply->proxySslCaCertPathBytes.constData());
                        if (rc != CURLE_OK) {
                            if (isCapabilityRelatedCurlError(rc)) {
                                const QString msg = QStringLiteral(
                                                        "libcurl 不支持 CURLOPT_PROXY_CAINFO（%1）")
                                                        .arg(QString::fromUtf8(
                                                            curl_easy_strerror(rc)));
                                if (tlsPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                                    reply->setError(NetworkError::InvalidRequest, msg);
                                    return false;
                                }
                                appendCapabilityWarning(reply, msg);
                            } else {
                                reply->setError(NetworkError::InvalidRequest,
                                         QStringLiteral("设置 CURLOPT_PROXY_CAINFO 失败（%1）")
                                             .arg(QString::fromUtf8(curl_easy_strerror(rc))));
                                return false;
                            }
                        }
#else
                        if (tlsPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                            reply->setError(NetworkError::InvalidRequest,
                                     QStringLiteral(
                                         "当前构建的 libcurl 不支持 CURLOPT_PROXY_CAINFO"));
                            return false;
                        }
                        appendCapabilityWarning(
                            reply, QStringLiteral("当前构建的 libcurl 不支持 CURLOPT_PROXY_CAINFO"));
#endif
                    }

                    if (tlsCfg.minTlsVersion().has_value()) {
                        const std::optional<long> sslVer = toCurlSslVersionMin(
                            tlsCfg.minTlsVersion().value());
                        if (!sslVer.has_value()) {
                            const QString msg = QStringLiteral("不支持的 TLS 版本配置（proxy）");
                            if (tlsPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                                reply->setError(NetworkError::InvalidRequest, msg);
                                return false;
                            }
                            appendCapabilityWarning(reply, msg);
                        } else {
#if LIBCURL_VERSION_NUM >= 0x073400 /* 7.52.0 */
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
                                        reply->setError(NetworkError::InvalidRequest, msg);
                                        return false;
                                    }
                                    appendCapabilityWarning(reply, msg);
                                } else {
                                    reply->setError(NetworkError::InvalidRequest,
                                             QStringLiteral(
                                                 "设置 CURLOPT_PROXY_SSLVERSION 失败（%1）")
                                                 .arg(QString::fromUtf8(curl_easy_strerror(rc))));
                                    return false;
                                }
                            }
#else
                            if (tlsPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                                reply->setError(NetworkError::InvalidRequest,
                                         QStringLiteral(
                                             "当前构建的 libcurl 不支持 CURLOPT_PROXY_SSLVERSION"));
                                return false;
                            }
                            appendCapabilityWarning(
                                reply,
                                QStringLiteral(
                                    "当前构建的 libcurl 不支持 CURLOPT_PROXY_SSLVERSION"));
#endif
                        }
                    }

                    if (!tlsCfg.cipherList().isEmpty()) {
#if LIBCURL_VERSION_NUM >= 0x073400 /* 7.52.0 */
                        reply->proxySslCipherListBytes = tlsCfg.cipherList().toUtf8();
                        const CURLcode rc
                            = curlEasySetoptWithTestHook(handle,
                                                         CURLOPT_PROXY_SSL_CIPHER_LIST,
                                                         "CURLOPT_PROXY_SSL_CIPHER_LIST",
                                                         reply->proxySslCipherListBytes.constData());
                        if (rc != CURLE_OK) {
                            if (isCapabilityRelatedCurlError(rc)) {
                                const QString msg
                                    = QStringLiteral(
                                          "libcurl 不支持 CURLOPT_PROXY_SSL_CIPHER_LIST（%1）")
                                          .arg(QString::fromUtf8(curl_easy_strerror(rc)));
                                if (tlsPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                                    reply->setError(NetworkError::InvalidRequest, msg);
                                    return false;
                                }
                                appendCapabilityWarning(reply, msg);
                            } else {
                                reply->setError(NetworkError::InvalidRequest,
                                         QStringLiteral(
                                             "设置 CURLOPT_PROXY_SSL_CIPHER_LIST 失败（%1）")
                                             .arg(QString::fromUtf8(curl_easy_strerror(rc))));
                                return false;
                            }
                        }
#else
                        if (tlsPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                            reply->setError(
                                NetworkError::InvalidRequest,
                                QStringLiteral(
                                    "当前构建的 libcurl 不支持 CURLOPT_PROXY_SSL_CIPHER_LIST"));
                            return false;
                        }
                        appendCapabilityWarning(
                            reply,
                            QStringLiteral(
                                "当前构建的 libcurl 不支持 CURLOPT_PROXY_SSL_CIPHER_LIST"));
#endif
                    }

                    if (!tlsCfg.tls13Ciphers().isEmpty()) {
#if LIBCURL_VERSION_NUM >= 0x073d00 /* 7.61.0 */
                        reply->proxySslTls13CiphersBytes = tlsCfg.tls13Ciphers().toUtf8();
                        const CURLcode rc
                            = curlEasySetoptWithTestHook(handle,
                                                         CURLOPT_PROXY_TLS13_CIPHERS,
                                                         "CURLOPT_PROXY_TLS13_CIPHERS",
                                                         reply->proxySslTls13CiphersBytes.constData());
                        if (rc != CURLE_OK) {
                            if (isCapabilityRelatedCurlError(rc)) {
                                const QString msg
                                    = QStringLiteral(
                                          "libcurl 不支持 CURLOPT_PROXY_TLS13_CIPHERS（%1）")
                                          .arg(QString::fromUtf8(curl_easy_strerror(rc)));
                                if (tlsPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                                    reply->setError(NetworkError::InvalidRequest, msg);
                                    return false;
                                }
                                appendCapabilityWarning(reply, msg);
                            } else {
                                reply->setError(NetworkError::InvalidRequest,
                                         QStringLiteral(
                                             "设置 CURLOPT_PROXY_TLS13_CIPHERS 失败（%1）")
                                             .arg(QString::fromUtf8(curl_easy_strerror(rc))));
                                return false;
                            }
                        }
#else
                        if (tlsPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                            reply->setError(NetworkError::InvalidRequest,
                                     QStringLiteral(
                                         "当前构建的 libcurl 不支持 CURLOPT_PROXY_TLS13_CIPHERS"));
                            return false;
                        }
                        appendCapabilityWarning(
                            reply,
                            QStringLiteral(
                                "当前构建的 libcurl 不支持 CURLOPT_PROXY_TLS13_CIPHERS"));
#endif
                    }
                }
#endif
            }
        } else {
            reply->proxyHostBytes.clear();
            reply->proxyUserBytes.clear();
            reply->proxyPasswordBytes.clear();
        }
    } else {
        reply->proxyHostBytes.clear();
        reply->proxyUserBytes.clear();
        reply->proxyPasswordBytes.clear();
    }

    // ==================
    // 连接池配置 (v2.14.0)
    // ==================

    // 先应用连接池的通用配置（DNS/复用等），再由请求级别配置覆盖（例如 HTTP 版本）。
    const QString host = request.url().host();
    Internal::QCNetworkConnectionPoolManagerInternal::configureCurlHandle(handle, host);

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
        reply->setError(NetworkError::InvalidRequest,
                 QStringLiteral("交付门禁 QCURL_REQUIRE_HTTP3=1：运行时 libcurl 不支持 "
                                "HTTP/3（CURL_VERSION_HTTP3 缺失），请求被拒绝"));
        return false;
    }

    if (requestedHttpVer == QCNetworkHttpVersion::Http3Only) {
        if (!runtimeHasHttp3) {
            reply->setError(
                NetworkError::InvalidRequest,
                QStringLiteral(
                    "运行时 libcurl 不支持 HTTP/3（CURL_VERSION_HTTP3 缺失），Http3Only 无法执行"));
            return false;
        }
#if !defined(CURL_HTTP_VERSION_3ONLY)
        appendCapabilityWarning(reply,
                                QStringLiteral(
                                    "当前构建的 libcurl 不支持 CURL_HTTP_VERSION_3ONLY，Http3Only "
                                    "将退化为 Http3（可能发生协议降级）"));
#endif
    } else if (requestedHttpVer == QCNetworkHttpVersion::Http3) {
        if (!runtimeHasHttp3) {
            effectiveHttpVer = QCNetworkHttpVersion::Http2TLS;
            appendCapabilityWarning(
                reply,
                QStringLiteral(
                    "运行时 libcurl 不支持 HTTP/3（CURL_VERSION_HTTP3 缺失），已降级为 HTTP/2TLS"));
        }
    }

    if (effectiveHttpVer != QCNetworkHttpVersion::Http1_1 || request.isHttpVersionExplicit()) {
        long curlVersion = detail::toCurlHttpVersion(effectiveHttpVer);
        curl_easy_setopt(handle, CURLOPT_HTTP_VERSION, curlVersion);
    }


    return true;
}

[[nodiscard]] bool configureReplyTlsOptions(QCNetworkReplyPrivate *reply,
                                            CURL *handle,
                                            const QCNetworkRequest &request)
{
    // ==================
    // 7. SSL 配置（基于 QCNetworkSslConfig）
    // ==================

    const QCNetworkSslConfig sslConfig = request.sslConfig();
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, sslConfig.verifyPeer() ? 1L : 0L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, sslConfig.verifyHost() ? 2L : 0L);

    if (!sslConfig.caCertPath().isEmpty()) {
        reply->sslCaCertPathBytes = sslConfig.caCertPath().toUtf8();
        curl_easy_setopt(handle, CURLOPT_CAINFO, reply->sslCaCertPathBytes.constData());
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
            QFileInfo fi(QLatin1StringView(systemCaPaths[i]));
            if (fi.exists() && fi.isReadable()) {
                curl_easy_setopt(handle, CURLOPT_CAINFO, systemCaPaths[i]);
                caPathSet = true;
                break;
            }
        }

        if (!caPathSet) {
            // 回退到 libcurl 默认行为（使用编译时配置的路径）
            reply->sslCaCertPathBytes.clear();
            curl_easy_setopt(handle, CURLOPT_CAINFO, nullptr);
        }
    }

    if (!sslConfig.clientCertPath().isEmpty()) {
        reply->sslClientCertPathBytes = sslConfig.clientCertPath().toUtf8();
        curl_easy_setopt(handle, CURLOPT_SSLCERT, reply->sslClientCertPathBytes.constData());
    } else {
        reply->sslClientCertPathBytes.clear();
        curl_easy_setopt(handle, CURLOPT_SSLCERT, nullptr);
    }

    if (!sslConfig.clientKeyPath().isEmpty()) {
        reply->sslClientKeyPathBytes = sslConfig.clientKeyPath().toUtf8();
        curl_easy_setopt(handle, CURLOPT_SSLKEY, reply->sslClientKeyPathBytes.constData());
    } else {
        reply->sslClientKeyPathBytes.clear();
        curl_easy_setopt(handle, CURLOPT_SSLKEY, nullptr);
    }

    if (!sslConfig.clientKeyPassword().isEmpty()) {
        reply->sslClientKeyPasswordBytes = sslConfig.clientKeyPassword().toUtf8();
        curl_easy_setopt(handle, CURLOPT_KEYPASSWD, reply->sslClientKeyPasswordBytes.constData());
    } else {
        reply->sslClientKeyPasswordBytes.clear();
        curl_easy_setopt(handle, CURLOPT_KEYPASSWD, nullptr);
    }

    // ==================
    // 7.1 TLS 策略（M5，可选；安全相关能力默认失败，可配置为 warning）
    // ==================

    const QCUnsupportedSecurityOptionPolicy sslPolicy = sslConfig.unsupportedSecurityPolicy();

    if (!sslConfig.pinnedPublicKey().isEmpty()) {
        reply->sslPinnedPublicKeyBytes = sslConfig.pinnedPublicKey().toUtf8();
        const CURLcode rc       = curlEasySetoptWithTestHook(handle,
                                                             CURLOPT_PINNEDPUBLICKEY,
                                                             "CURLOPT_PINNEDPUBLICKEY",
                                                             reply->sslPinnedPublicKeyBytes.constData());
        if (rc != CURLE_OK) {
            if (isCapabilityRelatedCurlError(rc)) {
                const QString msg = QStringLiteral("libcurl 不支持 CURLOPT_PINNEDPUBLICKEY（%1）")
                                        .arg(QString::fromUtf8(curl_easy_strerror(rc)));
                if (sslPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                    reply->setError(NetworkError::InvalidRequest, msg);
                    return false;
                }
                appendCapabilityWarning(reply, msg);
            } else {
                reply->setError(NetworkError::InvalidRequest,
                         QStringLiteral("设置 CURLOPT_PINNEDPUBLICKEY 失败（%1）")
                             .arg(QString::fromUtf8(curl_easy_strerror(rc))));
                return false;
            }
        }
    }

    if (sslConfig.minTlsVersion().has_value()) {
        const std::optional<long> sslVer = toCurlSslVersionMin(sslConfig.minTlsVersion().value());
        if (!sslVer.has_value()) {
            const QString msg = QStringLiteral("不支持的 TLS 版本配置（ssl）");
            if (sslPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                reply->setError(NetworkError::InvalidRequest, msg);
                return false;
            }
            appendCapabilityWarning(reply, msg);
        } else {
#if LIBCURL_VERSION_NUM >= 0x070100 /* 7.1.0 */
            const CURLcode rc = curlEasySetoptWithTestHook(handle,
                                                           CURLOPT_SSLVERSION,
                                                           "CURLOPT_SSLVERSION",
                                                           sslVer.value());
            if (rc != CURLE_OK) {
                if (isCapabilityRelatedCurlError(rc)) {
                    const QString msg = QStringLiteral("libcurl 不支持 CURLOPT_SSLVERSION（%1）")
                                            .arg(QString::fromUtf8(curl_easy_strerror(rc)));
                    if (sslPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                        reply->setError(NetworkError::InvalidRequest, msg);
                        return false;
                    }
                    appendCapabilityWarning(reply, msg);
                } else {
                    reply->setError(NetworkError::InvalidRequest,
                             QStringLiteral("设置 CURLOPT_SSLVERSION 失败（%1）")
                                 .arg(QString::fromUtf8(curl_easy_strerror(rc))));
                    return false;
                }
            }
#else
            const QString msg = QStringLiteral("当前构建的 libcurl 不支持 CURLOPT_SSLVERSION");
            if (sslPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                reply->setError(NetworkError::InvalidRequest, msg);
                return false;
            }
            appendCapabilityWarning(reply, msg);
#endif
        }
    }

    if (!sslConfig.cipherList().isEmpty()) {
#if LIBCURL_VERSION_NUM >= 0x070900 /* 7.9.0 */
        reply->sslCipherListBytes = sslConfig.cipherList().toUtf8();
        const CURLcode rc  = curlEasySetoptWithTestHook(handle,
                                                        CURLOPT_SSL_CIPHER_LIST,
                                                        "CURLOPT_SSL_CIPHER_LIST",
                                                        reply->sslCipherListBytes.constData());
        if (rc != CURLE_OK) {
            if (isCapabilityRelatedCurlError(rc)) {
                const QString msg = QStringLiteral("libcurl 不支持 CURLOPT_SSL_CIPHER_LIST（%1）")
                                        .arg(QString::fromUtf8(curl_easy_strerror(rc)));
                if (sslPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                    reply->setError(NetworkError::InvalidRequest, msg);
                    return false;
                }
                appendCapabilityWarning(reply, msg);
            } else {
                reply->setError(NetworkError::InvalidRequest,
                         QStringLiteral("设置 CURLOPT_SSL_CIPHER_LIST 失败（%1）")
                             .arg(QString::fromUtf8(curl_easy_strerror(rc))));
                return false;
            }
        }
#else
        const QString msg = QStringLiteral("当前构建的 libcurl 不支持 CURLOPT_SSL_CIPHER_LIST");
        if (sslPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
            reply->setError(NetworkError::InvalidRequest, msg);
            return false;
        }
        appendCapabilityWarning(reply, msg);
#endif
    }

    if (!sslConfig.tls13Ciphers().isEmpty()) {
#if LIBCURL_VERSION_NUM >= 0x073d00 /* 7.61.0 */
        reply->sslTls13CiphersBytes = sslConfig.tls13Ciphers().toUtf8();
        const CURLcode rc    = curlEasySetoptWithTestHook(handle,
                                                          CURLOPT_TLS13_CIPHERS,
                                                          "CURLOPT_TLS13_CIPHERS",
                                                          reply->sslTls13CiphersBytes.constData());
        if (rc != CURLE_OK) {
            if (isCapabilityRelatedCurlError(rc)) {
                const QString msg = QStringLiteral("libcurl 不支持 CURLOPT_TLS13_CIPHERS（%1）")
                                        .arg(QString::fromUtf8(curl_easy_strerror(rc)));
                if (sslPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
                    reply->setError(NetworkError::InvalidRequest, msg);
                    return false;
                }
                appendCapabilityWarning(reply, msg);
            } else {
                reply->setError(NetworkError::InvalidRequest,
                         QStringLiteral("设置 CURLOPT_TLS13_CIPHERS 失败（%1）")
                             .arg(QString::fromUtf8(curl_easy_strerror(rc))));
                return false;
            }
        }
#else
        const QString msg = QStringLiteral("当前构建的 libcurl 不支持 CURLOPT_TLS13_CIPHERS");
        if (sslPolicy == QCUnsupportedSecurityOptionPolicy::Fail) {
            reply->setError(NetworkError::InvalidRequest, msg);
            return false;
        }
        appendCapabilityWarning(reply, msg);
#endif
    }


    return true;
}

[[nodiscard]] bool configureReplyTimeoutsAndCallbacks(QCNetworkReplyPrivate *reply,
                                                      CURL *handle,
                                                      const QCNetworkRequest &request)
{
    // ==================
    // 8. 超时配置
    // ==================

    QCNetworkTimeoutConfig timeout = request.timeoutConfig();

    // 连接超时（TCP 三次握手超时）
    if (timeout.connectTimeout().has_value() && timeout.connectTimeout()->count() > 0) {
        curl_easy_setopt(handle,
                         CURLOPT_CONNECTTIMEOUT_MS,
                         static_cast<long>(timeout.connectTimeout()->count()));
    }

    // 总超时（整个请求的最大时间）
    if (timeout.totalTimeout().has_value() && timeout.totalTimeout()->count() > 0) {
        curl_easy_setopt(handle,
                         CURLOPT_TIMEOUT_MS,
                         static_cast<long>(timeout.totalTimeout()->count()));
    }

    // 低速检测：如果在 lowSpeedTime 内速度低于 lowSpeedLimit，则超时
    if (timeout.lowSpeedTime().has_value() && timeout.lowSpeedTime()->count() > 0) {
        curl_easy_setopt(handle,
                         CURLOPT_LOW_SPEED_TIME,
                         static_cast<long>(timeout.lowSpeedTime()->count()));
    }

    if (timeout.lowSpeedLimit().has_value() && *timeout.lowSpeedLimit() > 0) {
        curl_easy_setopt(handle,
                         CURLOPT_LOW_SPEED_LIMIT,
                         static_cast<long>(*timeout.lowSpeedLimit()));
    }

    // ==================
    // 9. 回调函数配置
    // ==================

    // 响应体写回调
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, QCNetworkReplyPrivate::curlWriteCallback);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, reply);

    // 响应头回调
    curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, QCNetworkReplyPrivate::curlHeaderCallback);
    curl_easy_setopt(handle, CURLOPT_HEADERDATA, reply);

    // 注意：READFUNCTION 仅在 M2 请求体来源路径下启用；
    // 旧的 QByteArray body 路径仍使用 CURLOPT_POSTFIELDS（避免默认行为变化）。

    // 定位回调
    curl_easy_setopt(handle, CURLOPT_SEEKFUNCTION, QCNetworkReplyPrivate::curlSeekCallback);
    curl_easy_setopt(handle, CURLOPT_SEEKDATA, reply);

    // 进度回调
    curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, QCNetworkReplyPrivate::curlProgressCallback);
    curl_easy_setopt(handle, CURLOPT_XFERINFODATA, reply);
    curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L); // 启用进度回调

    return true;
}

} // namespace

bool QCNetworkReplyPrivate::configureCurlOptions()
{
    const auto &plan = curlPlan;
    const auto &normalized = curlPlan.normalized;
    const QCNetworkRequest &request = normalized.request;
    const auto &bodySpec = normalized.body;

    CURL *handle = curlManager.handle();
    if (!handle) {
        qCritical() << "QCNetworkReply: curl handle is null";
        return false;
    }

    const auto minimumRuntimeAvailability = CurlFeatureProbe::instance().minimumRuntimeAvailability();
    if (!minimumRuntimeAvailability.supported) {
        setError(NetworkError::InvalidRequest, minimumRuntimeAvailability.reason);
        return false;
    }

    if (!configureReplyBaseAndNetworkOptions(this, handle, normalized, bodySpec)) {
        return false;
    }
    bool hasExplicitAuthorizationHeader = false;
    bool hasSensitiveHeader = false;
    if (!configureReplyMethodHeadersAndLimits(this,
                                             handle,
                                             plan,
                                             normalized,
                                             bodySpec,
                                             &hasExplicitAuthorizationHeader,
                                             &hasSensitiveHeader)) {
        return false;
    }
    if (!configureReplyProxyAndHttpVersion(
            this, handle, normalized, hasExplicitAuthorizationHeader, hasSensitiveHeader)) {
        return false;
    }
    if (!configureReplyTlsOptions(this, handle, request)) {
        return false;
    }
    if (!configureReplyTimeoutsAndCallbacks(this, handle, request)) {
        return false;
    }

    return true;
}

} // namespace QCurl
