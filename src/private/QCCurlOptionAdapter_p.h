/**
 * @file
 * @brief 封装 libcurl option 的合同值与单位转换。
 */

#ifndef QCCURLOPTIONADAPTER_P_H
#define QCCURLOPTIONADAPTER_P_H

#include <QByteArray>
#include <QList>

#include <chrono>
#include <curl/curl.h>
#include <limits>

#ifdef QCURL_WEBSOCKET_SUPPORT
#include <curl/websockets.h>
#endif

namespace QCurl::Internal::CurlOptions {

// libcurl 使用 long 表达布尔与部分枚举合同值；业务层不得直接写这些数值。
constexpr long kDisabled         = 0L;
constexpr long kEnabled          = 1L;
constexpr long kVerifyHostStrict = 2L;

#ifdef QCURL_WEBSOCKET_SUPPORT
constexpr long kConnectOnlyWebSocket = 2L;
#endif

[[nodiscard]] inline CURLcode setLong(CURL *handle, CURLoption option, long value)
{
    return curl_easy_setopt(handle, option, value);
}

[[nodiscard]] inline CURLcode setPointer(CURL *handle, CURLoption option, const void *value)
{
    return curl_easy_setopt(handle, option, value);
}

[[nodiscard]] inline CURLcode setString(CURL *handle, CURLoption option, const char *value)
{
    return curl_easy_setopt(handle, option, value);
}

template<typename T>
[[nodiscard]] static inline CURLcode setWithTestHook(CURL *handle,
                                                     CURLoption option,
                                                     const char *optionName,
                                                     T value)
{
#ifdef QCURL_ENABLE_TEST_HOOKS
    // 测试环境可按 option 名定向注入 capability 缺失，用于验证降级路径。
    const QByteArray raw = qgetenv("QCURL_TEST_FORCE_CAPABILITY_ERROR");
    if (!raw.isEmpty()) {
        const QByteArray trimmed = raw.trimmed();
        if (trimmed == "1" || trimmed == "all") {
            return CURLE_NOT_BUILT_IN;
        }

        const QList<QByteArray> parts = raw.split(',');
        for (const QByteArray &part : parts) {
            if (part.trimmed() == optionName) {
                return CURLE_NOT_BUILT_IN;
            }
        }
    }
#else
    Q_UNUSED(optionName);
#endif
    return curl_easy_setopt(handle, option, value);
}

[[nodiscard]] inline CURLcode setEnabled(CURL *handle, CURLoption option, bool enabled)
{
    return setLong(handle, option, enabled ? kEnabled : kDisabled);
}

[[nodiscard]] inline CURLcode setSslVerifyPeer(CURL *handle, bool enabled)
{
    return setEnabled(handle, CURLOPT_SSL_VERIFYPEER, enabled);
}

[[nodiscard]] inline CURLcode setSslVerifyHost(CURL *handle, bool enabled)
{
    return setLong(handle, CURLOPT_SSL_VERIFYHOST, enabled ? kVerifyHostStrict : kDisabled);
}

[[nodiscard]] inline CURLcode setProxySslVerifyPeer(CURL *handle, bool enabled)
{
    return setEnabled(handle, CURLOPT_PROXY_SSL_VERIFYPEER, enabled);
}

[[nodiscard]] inline CURLcode setProxySslVerifyPeerWithTestHook(CURL *handle,
                                                                const char *optionName,
                                                                bool enabled)
{
    return setWithTestHook(handle,
                           CURLOPT_PROXY_SSL_VERIFYPEER,
                           optionName,
                           enabled ? kEnabled : kDisabled);
}

[[nodiscard]] inline CURLcode setProxySslVerifyHost(CURL *handle, bool enabled)
{
    return setLong(handle, CURLOPT_PROXY_SSL_VERIFYHOST, enabled ? kVerifyHostStrict : kDisabled);
}

[[nodiscard]] inline CURLcode setProxySslVerifyHostWithTestHook(CURL *handle,
                                                                const char *optionName,
                                                                bool enabled)
{
    return setWithTestHook(handle,
                           CURLOPT_PROXY_SSL_VERIFYHOST,
                           optionName,
                           enabled ? kVerifyHostStrict : kDisabled);
}

#ifdef QCURL_WEBSOCKET_SUPPORT
[[nodiscard]] inline CURLcode setConnectOnlyWebSocket(CURL *handle)
{
    return setLong(handle, CURLOPT_CONNECT_ONLY, kConnectOnlyWebSocket);
}

[[nodiscard]] inline CURLcode setWebSocketNoAutoPong(CURL *handle)
{
    return setLong(handle, CURLOPT_WS_OPTIONS, CURLWS_NOAUTOPONG);
}
#endif // QCURL_WEBSOCKET_SUPPORT

[[nodiscard]] inline bool tryCurlMilliseconds(std::chrono::milliseconds timeout, long *out) noexcept
{
    if (!out || timeout.count() <= 0) {
        return false;
    }

    constexpr auto kMaxCurlMilliseconds = std::chrono::milliseconds{
        std::numeric_limits<long>::max()};
    if (timeout > kMaxCurlMilliseconds) {
        return false;
    }

    *out = static_cast<long>(timeout.count());
    return true;
}

[[nodiscard]] inline CURLcode setConnectTimeout(CURL *handle, std::chrono::milliseconds timeout)
{
    long timeoutMs = 0;
    if (!tryCurlMilliseconds(timeout, &timeoutMs)) {
        return CURLE_BAD_FUNCTION_ARGUMENT;
    }

    return setLong(handle, CURLOPT_CONNECTTIMEOUT_MS, timeoutMs);
}

[[nodiscard]] inline CURLcode setVerbose(CURL *handle, bool enabled)
{
    return setEnabled(handle, CURLOPT_VERBOSE, enabled);
}

[[nodiscard]] inline CURLcode setTcpKeepAlive(CURL *handle, bool enabled)
{
    return setEnabled(handle, CURLOPT_TCP_KEEPALIVE, enabled);
}

[[nodiscard]] inline CURLcode setTcpKeepInterval(CURL *handle, std::chrono::seconds interval)
{
    return setLong(handle, CURLOPT_TCP_KEEPINTVL, static_cast<long>(interval.count()));
}

[[nodiscard]] inline CURLcode setPipeWait(CURL *handle, bool enabled)
{
    return setEnabled(handle, CURLOPT_PIPEWAIT, enabled);
}

[[nodiscard]] inline CURLMcode wakeupMultiHandle(CURLM *handle)
{
    return curl_multi_wakeup(handle);
}

} // namespace QCurl::Internal::CurlOptions

#endif // QCCURLOPTIONADAPTER_P_H
