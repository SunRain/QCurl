/**
 * @file
 * @brief 在 Blocking 既有插点应用共同代理、传输及认证策略。
 */
#include "private/QCBlockingCurlRequestSetup_p.h"

namespace QCurl::Internal {

bool configureProxyOptions(CURL *handle, const RequestPolicy &policy, RequestOptionStorage *storage)
{
    if (!policy.proxy) {
        return applyBlockingRequestOption(handle, storage, QCURL_CURL_OPTION(CURLOPT_PROXY), "");
    }
    const auto &proxy = *policy.proxy;
    return applyBlockingRequestOption(handle,
                                      storage,
                                      QCURL_CURL_OPTION(CURLOPT_PROXY),
                                      proxy.host.constData())
           && applyBlockingRequestOption(handle,
                                         storage,
                                         QCURL_CURL_OPTION(CURLOPT_PROXYPORT),
                                         proxy.port)
           && applyBlockingRequestOption(handle,
                                         storage,
                                         QCURL_CURL_OPTION(CURLOPT_PROXYTYPE),
                                         proxy.type)
           && applyBlockingRequestOption(handle, storage, QCURL_CURL_OPTION(CURLOPT_NOPROXY), "")
           && (!proxy.user
               || applyBlockingRequestOption(handle,
                                             storage,
                                             QCURL_CURL_OPTION(CURLOPT_PROXYUSERNAME),
                                             proxy.user->constData()))
           && (!proxy.password
               || applyBlockingRequestOption(handle,
                                             storage,
                                             QCURL_CURL_OPTION(CURLOPT_PROXYPASSWORD),
                                             proxy.password->constData()))
           && (!proxy.negotiateHttpAuth
               || applyBlockingRequestOption(handle,
                                             storage,
                                             QCURL_CURL_OPTION(CURLOPT_PROXYAUTH),
                                             static_cast<long>(CURLAUTH_ANY)));
}

bool configureTransferOptions(CURL *handle,
                              const RequestPolicy &policy,
                              RequestOptionStorage *storage)
{
    const auto &headers  = policy.headers;
    const auto &transfer = policy.transfer;
    return (!headers.referer
            || applyBlockingRequestOption(handle,
                                          storage,
                                          QCURL_CURL_OPTION(CURLOPT_REFERER),
                                          headers.referer->constData()))
           && (!headers.acceptEncoding
               || applyBlockingRequestOption(handle,
                                             storage,
                                             QCURL_CURL_OPTION(CURLOPT_ACCEPT_ENCODING),
                                             headers.acceptEncoding->constData()))
           && (!transfer.downloadBytesPerSecond
               || applyBlockingRequestOption(handle,
                                             storage,
                                             QCURL_CURL_OPTION(CURLOPT_MAX_RECV_SPEED_LARGE),
                                             *transfer.downloadBytesPerSecond))
           && (!transfer.uploadBytesPerSecond
               || applyBlockingRequestOption(handle,
                                             storage,
                                             QCURL_CURL_OPTION(CURLOPT_MAX_SEND_SPEED_LARGE),
                                             *transfer.uploadBytesPerSecond));
}

bool configureAuthOptions(CURL *handle, const RequestPolicy &policy, RequestOptionStorage *storage)
{
    const auto &headers = policy.headers;
    if (headers.credentials) {
        const auto &credentials = *headers.credentials;
        if (!applyBlockingRequestOption(handle,
                                        storage,
                                        QCURL_CURL_OPTION(CURLOPT_USERNAME),
                                        credentials.user.constData())
            || !applyBlockingRequestOption(handle,
                                           storage,
                                           QCURL_CURL_OPTION(CURLOPT_PASSWORD),
                                           credentials.password.constData())
            || !applyBlockingRequestOption(handle,
                                           storage,
                                           QCURL_CURL_OPTION(CURLOPT_HTTPAUTH),
                                           credentials.method)) {
            return false;
        }
    }
    return !headers.unrestrictedAuth
           || applyBlockingRequestOption(handle,
                                         storage,
                                         QCURL_CURL_OPTION(CURLOPT_UNRESTRICTED_AUTH),
                                         CurlOptions::kEnabled);
}

} // namespace QCurl::Internal
