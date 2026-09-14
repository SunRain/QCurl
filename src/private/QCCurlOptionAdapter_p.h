/**
 * @file
 * @brief 封装 libcurl option 的合同值与单位转换。
 */

#ifndef QCCURLOPTIONADAPTER_P_H
#define QCCURLOPTIONADAPTER_P_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <chrono>
#include <curl/curl.h>
#include <limits>
#include <memory>

#ifdef QCURL_WEBSOCKET_SUPPORT
#include <curl/websockets.h>
#endif

namespace QCurl::Internal::CurlOptions {

struct Option
{
    CURLoption id;
    const char *name;
};

// 保留调用处 token（包括别名）的名称；选项查询接口可以被 libcurl 构建禁用。
#define QCURL_CURL_OPTION(option) (::QCurl::Internal::CurlOptions::Option{option, #option})

/**
 * @brief 释放 libcurl header list 的 RAII deleter。
 */
struct CurlSlistDeleter
{
    void operator()(curl_slist *list) const noexcept
    {
        if (list) {
            curl_slist_free_all(list);
        }
    }
};

using CurlSlistOwner = std::unique_ptr<curl_slist, CurlSlistDeleter>;

// libcurl 使用 long 表达布尔与部分枚举合同值；业务层不得直接写这些数值。
constexpr long kDisabled         = 0L;
constexpr long kEnabled          = 1L;
constexpr long kVerifyHostStrict = 2L;

[[nodiscard]] static inline bool shouldForceMultiFailure(const char *failurePoint)
{
#ifdef QCURL_ENABLE_TEST_HOOKS
    return qgetenv("QCURL_TEST_FORCE_MULTI_FAILURE").trimmed() == failurePoint;
#else
    Q_UNUSED(failurePoint);
    return false;
#endif
}

[[nodiscard]] static inline bool shouldForceSlistAppendFailure(const char *optionName,
                                                               int appendIndex = 1)
{
#ifdef QCURL_ENABLE_TEST_HOOKS
    const QByteArray raw = qgetenv("QCURL_TEST_FORCE_SLIST_APPEND_ERROR").trimmed();
    if (raw == "1" || raw == "all" || raw == optionName) {
        return true;
    }

    const QByteArray prefix = QByteArray(optionName) + ':';
    if (!raw.startsWith(prefix)) {
        return false;
    }
    bool ok               = false;
    const int forcedIndex = raw.mid(prefix.size()).toInt(&ok);
    return ok && forcedIndex == appendIndex;
#else
    Q_UNUSED(optionName);
    Q_UNUSED(appendIndex);
    return false;
#endif
}

[[nodiscard]] static inline bool buildCurlSlist(const QStringList &values,
                                                curl_slist **output,
                                                QString *error,
                                                const char *optionName)
{
    if (!output) {
        if (error) {
            *error = QStringLiteral("%1: slist 输出指针为空")
                         .arg(QString::fromUtf8(optionName ? optionName : "unknown"));
        }
        return false;
    }

    int appendIndex = 1;
    for (const QString &value : values) {
        const QByteArray bytes = value.toUtf8();
        if (shouldForceSlistAppendFailure(optionName, appendIndex)) {
            if (*output) {
                curl_slist_free_all(*output);
                *output = nullptr;
            }
            if (error) {
                *error = QStringLiteral("构造 %1 参数列表失败（测试故障注入）")
                             .arg(QString::fromUtf8(optionName ? optionName : "unknown"));
            }
            return false;
        }

        curl_slist *next = curl_slist_append(*output, bytes.constData());
        if (!next) {
            if (*output) {
                curl_slist_free_all(*output);
                *output = nullptr;
            }
            if (error) {
                *error = QStringLiteral("构造 %1 参数列表失败")
                             .arg(QString::fromUtf8(optionName ? optionName : "unknown"));
            }
            return false;
        }
        *output = next;
        ++appendIndex;
    }

    return true;
}

[[nodiscard]] static inline CURLM *createMultiHandle()
{
    if (shouldForceMultiFailure("init")) {
        return nullptr;
    }
    return curl_multi_init();
}

[[nodiscard]] static inline CURLMcode removeMultiHandle(CURLM *multiHandle, CURL *easyHandle)
{
#ifdef QCURL_ENABLE_TEST_HOOKS
    const QByteArray forced = qgetenv("QCURL_TEST_FORCE_MULTI_REMOVE_ERROR").trimmed();
    if (forced == "recursive" || forced == "recursive-once") {
        static thread_local QByteArray consumedFor;
        static thread_local bool consumed = false;
        if (consumedFor != forced) {
            consumedFor = forced;
            consumed    = false;
        }
        if (forced == "recursive" || !consumed) {
            consumed = true;
            return CURLM_RECURSIVE_API_CALL;
        }
    } else if (forced == "internal") {
        return CURLM_INTERNAL_ERROR;
    } else if (forced == "bad-easy") {
        return CURLM_BAD_EASY_HANDLE;
    } else if (forced == "bad-handle") {
        return CURLM_BAD_HANDLE;
    }
#else
    Q_UNUSED(multiHandle);
    Q_UNUSED(easyHandle);
#endif
    return curl_multi_remove_handle(multiHandle, easyHandle);
}

[[nodiscard]] static inline CURLMcode addMultiHandle(CURLM *multiHandle, CURL *easyHandle)
{
    if (shouldForceMultiFailure("add")) {
        return CURLM_INTERNAL_ERROR;
    }
    return curl_multi_add_handle(multiHandle, easyHandle);
}

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

[[nodiscard]] static inline bool shouldForceSetoptFailure(const char *optionName)
{
#ifdef QCURL_ENABLE_TEST_HOOKS
    const QByteArray raw = qgetenv("QCURL_TEST_FORCE_SETOPT_ERROR").trimmed();
    if (raw.isEmpty()) {
        return false;
    }
    if (raw == "1" || raw == "all" || raw == optionName) {
        return true;
    }
    const QList<QByteArray> options = raw.split(',');
    for (const QByteArray &option : options) {
        if (option.trimmed() == optionName) {
            return true;
        }
    }
#else
    Q_UNUSED(optionName);
#endif
    return false;
}

template<typename T>
[[nodiscard]] static inline CURLcode setWithTestHook(CURL *handle, Option option, T value)
{
#ifdef QCURL_ENABLE_TEST_HOOKS
    if (shouldForceSetoptFailure(option.name)) {
        return CURLE_BAD_FUNCTION_ARGUMENT;
    }
    // 测试环境可按 option 名定向注入 capability 缺失，用于验证降级路径。
    const QByteArray raw = qgetenv("QCURL_TEST_FORCE_CAPABILITY_ERROR");
    if (!raw.isEmpty()) {
        const QByteArray trimmed = raw.trimmed();
        if (trimmed == "1" || trimmed == "all") {
            return CURLE_NOT_BUILT_IN;
        }

        const QList<QByteArray> parts = raw.split(',');
        for (const QByteArray &part : parts) {
            if (part.trimmed() == option.name) {
                return CURLE_NOT_BUILT_IN;
            }
        }
    }
#endif
    return curl_easy_setopt(handle, option.id, value);
}

/**
 * @brief 设置或解除 easy handle 绑定的 HTTP header list。
 * @param handle 目标 easy handle。
 * @param headers 要绑定的 header list；传入 nullptr 表示解除绑定。
 * @return libcurl 设置结果。
 */
[[nodiscard]] static inline CURLcode setHttpHeaderList(CURL *handle, curl_slist *headers)
{
    return setWithTestHook(handle, QCURL_CURL_OPTION(CURLOPT_HTTPHEADER), headers);
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

[[nodiscard]] inline CURLcode setProxySslVerifyPeerWithTestHook(CURL *handle, bool enabled)
{
    return setWithTestHook(handle,
                           QCURL_CURL_OPTION(CURLOPT_PROXY_SSL_VERIFYPEER),
                           enabled ? kEnabled : kDisabled);
}

[[nodiscard]] inline CURLcode setProxySslVerifyHost(CURL *handle, bool enabled)
{
    return setLong(handle, CURLOPT_PROXY_SSL_VERIFYHOST, enabled ? kVerifyHostStrict : kDisabled);
}

[[nodiscard]] inline CURLcode setProxySslVerifyHostWithTestHook(CURL *handle, bool enabled)
{
    return setWithTestHook(handle,
                           QCURL_CURL_OPTION(CURLOPT_PROXY_SSL_VERIFYHOST),
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
