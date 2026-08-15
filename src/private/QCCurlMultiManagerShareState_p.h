/**
 * @file
 * @brief 声明 curl multi manager 的 share 状态记录。
 */

#ifndef QCCURLMULTIMANAGERSHARESTATE_P_H
#define QCCURLMULTIMANAGERSHARESTATE_P_H

#include "private/QCCookieStoreResult_p.h"

#include <QMutex>
#include <QString>

#include <curl/curl.h>
#include <optional>

namespace QCurl {

class QCNetworkAccessManager;

namespace Internal {

/**
 * @brief 描述一个 share handle 需要共享的 libcurl 数据类型。
 */
struct QCCurlMultiManagerShareConfig final
{
    bool dnsCache   = false; ///< 是否共享 DNS 缓存
    bool cookies    = false; ///< 是否共享 Cookie
    bool sslSession = false; ///< 是否共享 SSL session

    /** @return 至少启用一种共享数据时返回 true */
    [[nodiscard]] bool enabled() const noexcept { return dnsCache || cookies || sslSession; }

    /** @return 两个配置的全部共享开关相同时返回 true */
    bool operator==(const QCCurlMultiManagerShareConfig &other) const noexcept
    {
        return dnsCache == other.dnsCache && cookies == other.cookies
               && sslSession == other.sslSession;
    }

    /** @return 两个配置存在任一不同开关时返回 true */
    bool operator!=(const QCCurlMultiManagerShareConfig &other) const noexcept
    {
        return !(*this == other);
    }
};

/**
 * @brief 保存 manager scope 的 share handle、事务状态和回调互斥量。
 */
struct QCCurlMultiManagerShareContext final
{
    const QCNetworkAccessManager *scopeKey = nullptr;     ///< 所属 access manager
    CURLSH *share                          = nullptr;     ///< 受事务管理的 share handle
    QCCurlMultiManagerShareConfig applied;                ///< 已成功应用的配置
    std::optional<QCCurlMultiManagerShareConfig> pending; ///< 等待用户归零后的配置

    QCCurlMultiManagerShareConfig lastInitAttempt; ///< 最近一次初始化配置
    bool lastInitFailed = false;                   ///< 最近一次初始化是否失败
    QString lastInitError;                         ///< 最近一次初始化诊断
    CookieStoreResult lastInitResult;              ///< 最近一次结构化初始化结果

    bool scopeDestroyed      = false; ///< access manager 是否已经销毁
    bool pendingDelete       = false; ///< 用户归零后是否应清理
    bool cookieStorePoisoned = false; ///< Cookie 状态是否已经不可证明
    int activeUsers          = 0;     ///< 已成功绑定该 handle 的 easy 数量

    QMutex dnsMutex;    ///< 保护共享 DNS 数据
    QMutex cookieMutex; ///< 保护共享 Cookie 数据
    QMutex sslMutex;    ///< 保护共享 SSL session 数据
    QMutex otherMutex;  ///< 保护其他 libcurl 共享数据
};

} // namespace Internal
} // namespace QCurl

#endif // QCCURLMULTIMANAGERSHARESTATE_P_H
