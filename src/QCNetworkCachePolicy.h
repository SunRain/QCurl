#ifndef QCNETWORKCACHEPOLICY_H
#define QCNETWORKCACHEPOLICY_H

QT_BEGIN_NAMESPACE

namespace QCurl {

/**
 * @brief HTTP 缓存策略
 *
 * 定义请求如何使用缓存数据。
 *
 */
enum class QCNetworkCachePolicy {
    /**
     * @brief 总是使用缓存（忽略 HTTP 缓存头）
     *
     * 如果缓存存在，直接返回缓存数据，不检查是否过期。
     * 如果缓存不存在，发起网络请求并缓存结果。
     */
    AlwaysCache,

    /**
     * @brief 优先使用缓存（默认策略）
     *
     * 如果缓存存在且未过期，返回缓存数据。
     * 如果缓存过期或不存在，发起网络请求并更新缓存。
     */
    PreferCache,

    /**
     * @brief 优先使用网络
     *
     * 总是发起网络请求。
     * 如果网络请求失败，尝试返回缓存数据（即使已过期）。
     */
    PreferNetwork,

    /**
     * @brief 仅使用网络（不使用缓存）
     *
     * 总是发起网络请求，不读取缓存，不写入缓存。
     */
    OnlyNetwork,

    /**
     * @brief 仅使用缓存（不发起网络请求）
     *
     * 如果缓存存在，返回缓存数据（忽略过期时间）。
     * 如果缓存不存在，返回错误。
     */
    OnlyCache
};

} // namespace QCurl

QT_END_NAMESPACE

#endif // QCNETWORKCACHEPOLICY_H
