/**
 * @file
 * @brief 声明 Cookie/share required option 的结构化适配器。
 */

#ifndef QCCURLREQUIREDOPTIONADAPTER_P_H
#define QCCURLREQUIREDOPTIONADAPTER_P_H

#include "QCCurlOptionAdapter_p.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

#include <curl/curl.h>

namespace QCurl::Internal {

enum class CookieOptionStage {
    Setup,
    Snapshot,
    Apply,
    Rollback,
    Persist,
    Clear,
    Export,
};

struct RequiredOptionResult
{
    CURLcode curlCode    = CURLE_OK;
    CURLSHcode shareCode = CURLSHE_OK;
    QByteArray optionName;
    QString policyCode;
    QString message;

    [[nodiscard]] bool isSuccess() const noexcept
    {
        return curlCode == CURLE_OK && shareCode == CURLSHE_OK;
    }
};

/**
 * @brief 封装 TLS 详细验证结果查询。
 */
class CurlInfoAdapter final
{
public:
    /**
     * @brief 查询 TLS 证书验证结果。
     * @param handle 目标 easy handle。
     * @param verifyResult 用于接收验证结果码的输出指针。
     * @return libcurl 查询结果；查询失败时不改变主请求错误码。
     */
    [[nodiscard]] static CURLcode getSslVerifyResult(CURL *handle, long *verifyResult)
    {
#ifdef QCURL_ENABLE_TEST_HOOKS
        const QByteArray forced = qgetenv("QCURL_TEST_FORCE_GETINFO_ERROR").trimmed();
        if (forced == "1" || forced == "all" || forced == "CURLINFO_SSL_VERIFYRESULT") {
            return CURLE_BAD_FUNCTION_ARGUMENT;
        }
#endif
        return curl_easy_getinfo(handle, CURLINFO_SSL_VERIFYRESULT, verifyResult);
    }
};

/**
 * 每次 Cookie 操作创建一个实例，使定点故障计数限定在单次事务内。
 * 错误消息只包含 option 和 libcurl 错误，不接触 option value。
 */
class CookieOptionAdapter final
{
public:
    template<typename T>
    [[nodiscard]] RequiredOptionResult setEasy(CURL *handle,
                                               QCurl::Internal::CurlOptions::Option option,
                                               CookieOptionStage stage,
                                               T value)
    {
        const QByteArray name(option.name);
        const int occurrence = ++m_occurrences[stageKey(stage, name)];
        const CURLcode code  = shouldFailEasy(stage, name, occurrence)
                                   ? CURLE_BAD_FUNCTION_ARGUMENT
                                   : curl_easy_setopt(handle, option.id, value);
        return easyResult(code, name);
    }

    template<typename T>
    [[nodiscard]] static RequiredOptionResult setShare(CURLSH *handle,
                                                       CURLSHoption option,
                                                       const char *optionName,
                                                       const char *detailName,
                                                       T value)
    {
        const QByteArray name(optionName);
        const QByteArray detail(detailName ? detailName : "");
        const CURLSHcode code = shouldFailShare(name, detail)
                                    ? CURLSHE_BAD_OPTION
                                    : curl_share_setopt(handle, option, value);
        return shareResult(code, name, detail);
    }

    [[nodiscard]] RequiredOptionResult getCookieList(CURL *handle,
                                                     CookieOptionStage stage,
                                                     curl_slist **list)
    {
        const QByteArray name("CURLINFO_COOKIELIST");
        const int occurrence = ++m_occurrences[stageKey(stage, name)];
        const CURLcode code  = shouldFailEasy(stage, name, occurrence)
                                   ? CURLE_BAD_FUNCTION_ARGUMENT
                                   : curl_easy_getinfo(handle, CURLINFO_COOKIELIST, list);
        return easyResult(code, name);
    }

private:
    [[nodiscard]] static QByteArray stageName(CookieOptionStage stage);
    [[nodiscard]] static QByteArray stageKey(CookieOptionStage stage, const QByteArray &name);
    [[nodiscard]] static bool shouldFailEasy(CookieOptionStage stage,
                                             const QByteArray &name,
                                             int occurrence);
    [[nodiscard]] static bool shouldFailShare(const QByteArray &name, const QByteArray &detail);
    [[nodiscard]] static RequiredOptionResult easyResult(CURLcode code, const QByteArray &name);
    [[nodiscard]] static RequiredOptionResult shareResult(CURLSHcode code,
                                                          const QByteArray &name,
                                                          const QByteArray &detail);

    QHash<QByteArray, int> m_occurrences;
};

} // namespace QCurl::Internal

#endif // QCCURLREQUIREDOPTIONADAPTER_P_H
