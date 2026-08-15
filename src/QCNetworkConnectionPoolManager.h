// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

/**
 * @file
 * @brief 声明连接池状态管理器。
 */

#ifndef QCNETWORKCONNECTIONPOOLMANAGER_H
#define QCNETWORKCONNECTIONPOOLMANAGER_H

#include "QCGlobal.h"
#include "QCNetworkConnectionPoolConfig.h"

#include <QScopedPointer>
#include <QSharedDataPointer>

namespace QCurl {

namespace Internal {
class QCNetworkConnectionPoolManagerInternal;
}

class QCNetworkConnectionPoolManagerPrivate;
class QCNetworkConnectionPoolStatisticsData;

/// 连接池统计快照，按值返回给调用方读取。
class QCURL_EXPORT QCNetworkConnectionPoolStatistics
{
public:
    QCNetworkConnectionPoolStatistics();
    QCNetworkConnectionPoolStatistics(const QCNetworkConnectionPoolStatistics &other);
    QCNetworkConnectionPoolStatistics(QCNetworkConnectionPoolStatistics &&other) noexcept;
    ~QCNetworkConnectionPoolStatistics();

    QCNetworkConnectionPoolStatistics &operator=(const QCNetworkConnectionPoolStatistics &other);
    QCNetworkConnectionPoolStatistics &operator=(QCNetworkConnectionPoolStatistics &&other) noexcept;

    /// 自进程启动或上次 resetStatistics() 以来完成的请求数。
    [[nodiscard]] qint64 totalRequests() const;
    /// 复用已有连接完成的请求数。
    [[nodiscard]] qint64 reusedConnections() const;
    /// 连接复用率，单位为百分比。
    [[nodiscard]] double reuseRate() const;
    /// 当前记录为活跃状态的连接数量。
    [[nodiscard]] int activeConnections() const;
    /// 按全局连接上限估算的空闲连接数量。
    [[nodiscard]] int idleConnections() const;

private:
    explicit QCNetworkConnectionPoolStatistics(qint64 totalRequests,
                                               qint64 reusedConnections,
                                               int activeConnections,
                                               int idleConnections);

    QSharedDataPointer<QCNetworkConnectionPoolStatisticsData> d;

    friend class QCNetworkConnectionPoolManager;
};

/**
 * @brief 基于 libcurl 连接缓存提供统一配置和统计的全局管理器。
 *
 * @note 错误生命周期：`setConfig()` 的返回枚举只描述本次同步提交；成功没有诊断文本，
 * 失败保持旧配置和 multi 状态。管理器不保存可查询的“最近一次错误”。
 */
class QCURL_EXPORT QCNetworkConnectionPoolManager
{
public:
    /**
     * @brief 表示一次连接池配置提交的同步结果。
     *
     * `Applied` 表示合法配置已写入线程安全快照；`InvalidArgument` 表示候选配置无效，
     * 原配置与 multi 状态保持不变。
     */
    enum class UpdateResult {
        Applied,
        InvalidArgument,
    };

    /**
     * @brief 获取全局单例
     *
     * @return 连接池管理器实例
     *
     * @note 线程安全
     */
    static QCNetworkConnectionPoolManager *instance();

    /**
     * @brief 设置连接池配置
     *
     * 新配置将应用到后续创建的所有 curl handle。
     *
     * @param config 连接池配置
     * @return 合法候选返回 `Applied`；非法候选返回 `InvalidArgument`，既有配置快照和
     *         libcurl multi 状态保持不变。
     *
     * @note 线程安全
     * @note 提交是同步的；已存在连接不会被主动关闭，后续 handle 才读取新快照。
     */
    [[nodiscard]] UpdateResult setConfig(const QCNetworkConnectionPoolConfig &config);

    /**
     * @brief 获取当前配置
     *
     * @return 连接池配置的副本
     *
     * @note 线程安全
     */
    QCNetworkConnectionPoolConfig config() const;

    /**
     * @brief 获取统计信息
     *
     * @return 连接池统计信息
     *
     * @note 线程安全
     */
    QCNetworkConnectionPoolStatistics statistics() const;

    /**
     * @brief 重置统计信息
     *
     * 将所有统计计数器归零。
     *
     * @note 线程安全
     */
    void resetStatistics();

private:
    friend class Internal::QCNetworkConnectionPoolManagerInternal;

    /**
     * @brief 私有构造函数（单例模式）
     */
    QCNetworkConnectionPoolManager();

    /**
     * @brief 析构函数
     */
    ~QCNetworkConnectionPoolManager();

    // 全局单例只允许通过 instance() 获取。
    QCNetworkConnectionPoolManager(const QCNetworkConnectionPoolManager &)            = delete;
    QCNetworkConnectionPoolManager &operator=(const QCNetworkConnectionPoolManager &) = delete;

    QScopedPointer<QCNetworkConnectionPoolManagerPrivate> d_ptr;
};

} // namespace QCurl

#endif // QCNETWORKCONNECTIONPOOLMANAGER_H
