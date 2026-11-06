/**
 * @file
 * @brief 实现 Blocking Extras 的 libcurl 请求配置辅助函数。
 */

#include "QCNetworkHttpVersion.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRequestConfig.h"
#include "QCNetworkSslConfig.h"
#include "QCNetworkTimeoutConfig.h"
#include "private/QCBlockingCurlProtocolSetup_p.h"
#include "private/QCBlockingCurlRequestSetup_p.h"
#include "private/QCCurlOptionAdapter_p.h"
#include "private/QCNetworkHttpVersion_p.h"
#include "private/QCNetworkIpResolve_p.h"

#include <QDebug>
#include <QStringList>

namespace QCurl::Internal {
namespace {

bool failOption(RequestOptionStorage *storage, const char *optionName)
{
    storage->failureMessage = QStringLiteral("Blocking Extras failed to set %1")
                                  .arg(QString::fromUtf8(optionName));
    return false;
}

[[nodiscard]] bool appendStringList(curl_slist **list,
                                    const QStringList &values,
                                    QString *error,
                                    const char *optionName)
{
    return Internal::CurlOptions::buildCurlSlist(values, list, error, optionName);
}

[[nodiscard]] bool configureSlistOption(CURL *handle,
                                        const QStringList &entries,
                                        curl_slist **list,
                                        RequestOptionStorage *storage,
                                        CurlOptions::Option option)
{
    curl_slist *candidate = nullptr;
    if (!appendStringList(&candidate, entries, &storage->failureMessage, option.name)) {
        return false;
    }
    const CURLcode code = CurlOptions::setWithTestHook(handle, option, candidate);
    if (code != CURLE_OK) {
        curl_slist_free_all(candidate);
        storage->failureMessage = QStringLiteral("设置 %1 失败（%2）")
                                      .arg(QString::fromUtf8(option.name),
                                           QString::fromUtf8(curl_easy_strerror(code)));
        return false;
    }
    curl_slist_free_all(*list);
    *list = candidate;
    return true;
}

[[nodiscard]] bool configureIpResolution(CURL *handle,
                                         const QCNetworkRequest &request,
                                         RequestOptionStorage *storage)
{
    const CURLcode code = CurlOptions::setWithTestHook(handle,
                                                       QCURL_CURL_OPTION(CURLOPT_IPRESOLVE),
                                                       curlIpResolveValue(
                                                           request.ipResolve().value_or(
                                                               QCNetworkIpResolve::Any)));
    if (code == CURLE_OK) {
        return true;
    }
    storage->failureMessage = QStringLiteral("设置 CURLOPT_IPRESOLVE 失败（%1）")
                                  .arg(QString::fromUtf8(curl_easy_strerror(code)));
    return false;
}

} // namespace

bool appendRequestHeaders(CURL *handle, const QCNetworkRequest &request, curl_slist **headers)
{
    QString ignoredError;
    return appendRequestHeaders(handle, request, headers, &ignoredError);
}

bool appendRequestHeaders(CURL *handle,
                          const QCNetworkRequest &request,
                          curl_slist **headers,
                          QString *error)
{
    if (!error) {
        return appendRequestHeaders(handle, request, headers);
    }

    const QList<QByteArray> names = request.rawHeaderList();
    for (const QByteArray &name : names) {
        const QByteArray line = name + QByteArrayLiteral(": ") + request.rawHeader(name);
        if (Internal::CurlOptions::shouldForceSlistAppendFailure(
                QCURL_CURL_OPTION(CURLOPT_HTTPHEADER).name)) {
            if (error) {
                *error = QStringLiteral("追加 CURLOPT_HTTPHEADER 失败（测试故障注入）");
            }
            return false;
        }
        curl_slist *next = curl_slist_append(*headers, line.constData());
        if (!next) {
            if (error) {
                *error = QStringLiteral("追加 CURLOPT_HTTPHEADER 失败");
            }
            return false;
        }
        *headers = next;
    }

    if (!*headers) {
        return true;
    }

    const CURLcode rc = CurlOptions::setWithTestHook(handle,
                                                     QCURL_CURL_OPTION(CURLOPT_HTTPHEADER),
                                                     *headers);
    if (rc == CURLE_OK) {
        return true;
    }
    if (error) {
        *error = QStringLiteral("设置 CURLOPT_HTTPHEADER 失败（%1）")
                     .arg(QString::fromUtf8(curl_easy_strerror(rc)));
    }
    return false;
}

[[nodiscard]] bool configureRedirectOptions(CURL *handle,
                                            const RequestPolicy &policy,
                                            RequestOptionStorage *storage)
{
    const auto &redirect = policy.redirect;
    return applyBlockingRequestOption(handle,
                                      storage,
                                      QCURL_CURL_OPTION(CURLOPT_FOLLOWLOCATION),
                                      redirect.follow ? CurlOptions::kEnabled
                                                      : CurlOptions::kDisabled)
           && (!redirect.maximum
               || applyBlockingRequestOption(handle,
                                             storage,
                                             QCURL_CURL_OPTION(CURLOPT_MAXREDIRS),
                                             *redirect.maximum))
           && (!redirect.post
               || applyBlockingRequestOption(handle,
                                             storage,
                                             QCURL_CURL_OPTION(CURLOPT_POSTREDIR),
                                             *redirect.post))
           && (!redirect.autoReferer
               || applyBlockingRequestOption(handle,
                                             storage,
                                             QCURL_CURL_OPTION(CURLOPT_AUTOREFERER),
                                             CurlOptions::kEnabled));
}

[[nodiscard]] bool configureHttpVersion(CURL *handle,
                                        const RequestPolicy &policy,
                                        RequestOptionStorage *storage)
{
    const auto decision = decideHttpVersionPolicy(policy.httpVersion,
                                                  detail::sampleHttpVersionFacts());
    if (!decision.failure.isEmpty()) {
        storage->failureMessage        = decision.failure;
        storage->unsupportedCapability = true;
        return false;
    }
    if (decision.downgraded) {
        qWarning().noquote() << QStringLiteral("运行时 libcurl 不支持 HTTP/3，已降级为 HTTP/2TLS");
    }
    return !decision.nativeVersion
           || applyBlockingRequestOption(handle,
                                         storage,
                                         QCURL_CURL_OPTION(CURLOPT_HTTP_VERSION),
                                         *decision.nativeVersion);
}

[[nodiscard]] bool configureTransportOptions(CURL *handle,
                                             const RequestPolicy &policy,
                                             RequestOptionStorage *storage)
{
    if (!configureHttpVersion(handle, policy, storage)) {
        return false;
    }
    if (CurlOptions::setSslVerifyPeerWithTestHook(handle, policy.endpointTls.verifyPeer)
        != CURLE_OK) {
        return failOption(storage, QCURL_CURL_OPTION(CURLOPT_SSL_VERIFYPEER).name);
    }
    if (CurlOptions::setSslVerifyHostWithTestHook(handle, policy.endpointTls.verifyHost)
        != CURLE_OK) {
        return failOption(storage, QCURL_CURL_OPTION(CURLOPT_SSL_VERIFYHOST).name);
    }
    return !policy.transfer.range
           || applyBlockingRequestOption(handle,
                                         storage,
                                         QCURL_CURL_OPTION(CURLOPT_RANGE),
                                         policy.transfer.range->constData());
}

[[nodiscard]] bool configureTimeoutOptions(CURL *handle,
                                           const RequestPolicy &policy,
                                           RequestOptionStorage *storage)
{
    const auto &timeout = policy.timeout;
    return (!timeout.connectMilliseconds
            || applyBlockingRequestOption(handle,
                                          storage,
                                          QCURL_CURL_OPTION(CURLOPT_CONNECTTIMEOUT_MS),
                                          *timeout.connectMilliseconds))
           && (!timeout.totalMilliseconds
               || applyBlockingRequestOption(handle,
                                             storage,
                                             QCURL_CURL_OPTION(CURLOPT_TIMEOUT_MS),
                                             *timeout.totalMilliseconds));
}

bool configureBasicRequestOptions(CURL *handle,
                                  const QCNetworkRequest &request,
                                  RequestOptionStorage *storage)
{
    const auto &policy = *storage->policy;
    return applyBlockingRequestOption(handle,
                                      storage,
                                      QCURL_CURL_OPTION(CURLOPT_URL),
                                      policy.url.constData())
           && configureRedirectOptions(handle, policy, storage)
           && configureTransportOptions(handle, policy, storage)
           && configureIpResolution(handle, request, storage)
           && configureTimeoutOptions(handle, policy, storage);
}

bool configureAdvancedPathOptions(CURL *handle,
                                  const QCNetworkRequest &request,
                                  RequestOptionStorage *storage)
{
    if (!configureSlistOption(handle,
                              request.resolveOverride().value_or(QStringList{}),
                              &storage->resolveList,
                              storage,
                              QCURL_CURL_OPTION(CURLOPT_RESOLVE))) {
        return false;
    }
#ifdef QCURL_ENABLE_ADVANCED_REQUEST_NETWORK_PATH_API
    if (!configureSlistOption(handle,
                              request.connectTo().value_or(QStringList{}),
                              &storage->connectToList,
                              storage,
                              QCURL_CURL_OPTION(CURLOPT_CONNECT_TO))) {
        return false;
    }
#endif
    return true;
}

bool configureRequestOptions(CURL *handle,
                             const QCNetworkRequest &request,
                             RequestOptionStorage *storage)
{
    Q_ASSERT(storage->policy);
    const auto &policy = *storage->policy;
    if (!configureBasicRequestOptions(handle, request, storage)
        || !configureProtocolOptions(handle, policy, storage)
        || !configureAdvancedPathOptions(handle, request, storage)
        || !configureProxyOptions(handle, policy, storage)
        || !configureTransferOptions(handle, policy, storage)
        || !configureAuthOptions(handle, policy, storage)) {
        return false;
    }

    storage->caInfo = policy.endpointTls.caCertPath.toUtf8();
    return storage->caInfo.isEmpty()
           || applyBlockingRequestOption(handle,
                                         storage,
                                         QCURL_CURL_OPTION(CURLOPT_CAINFO),
                                         storage->caInfo.constData());
}

} // namespace QCurl::Internal
