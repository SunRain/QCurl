// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

/**
 * @file
 * @brief 声明 Core 级网络日志 contract。
 */

#ifndef QCNETWORKLOGGER_H
#define QCNETWORKLOGGER_H

#include "QCGlobal.h"

#include <QDateTime>
#include <QSharedDataPointer>
#include <QString>
#include <QtGlobal>

namespace QCurl {

/**
 * @brief HTTP 网络日志级别
 */
enum class NetworkLogLevel : quint8 {
    Debug,   ///< 调试日志
    Info,    ///< 信息日志
    Warning, ///< 警告日志
    Error    ///< 错误日志
};

class NetworkLogEntryData;

/**
 * @brief Network log entry 的 accessor-only 值类型。
 */
class QCURL_EXPORT NetworkLogEntry
{
public:
    /// 构造一条默认日志；时间戳默认使用当前 UTC 时间。
    NetworkLogEntry();

    /// 使用显式字段构造日志；`timestampUtc` 会被规范化为 UTC。
    NetworkLogEntry(NetworkLogLevel level,
                    const QString &category,
                    const QString &message,
                    const QDateTime &timestampUtc);

    /// 复制构造，保持 implicit-sharing 语义。
    NetworkLogEntry(const NetworkLogEntry &other);

    /// 移动构造，保持 implicit-sharing 语义。
    NetworkLogEntry(NetworkLogEntry &&other) noexcept;

    /// 析构函数 out-of-line，避免不完整类型删除风险。
    ~NetworkLogEntry();

    /// 复制赋值。
    NetworkLogEntry &operator=(const NetworkLogEntry &other);

    /// 移动赋值。
    NetworkLogEntry &operator=(NetworkLogEntry &&other) noexcept;

    /// 返回日志级别。
    [[nodiscard]] NetworkLogLevel level() const;

    /// 返回日志分类。
    [[nodiscard]] QString category() const;

    /// 返回日志消息。
    [[nodiscard]] QString message() const;

    /// 返回 UTC 时间戳。
    [[nodiscard]] QDateTime timestampUtc() const;

    /// 设置日志级别。
    void setLevel(NetworkLogLevel level);

    /// 设置日志分类。
    void setCategory(const QString &category);

    /// 设置日志消息。
    void setMessage(const QString &message);

    /// 设置 UTC 时间戳；非 UTC 输入会被转换为 UTC。
    void setTimestampUtc(const QDateTime &timestampUtc);

private:
    QSharedDataPointer<NetworkLogEntryData> d;
};

/**
 * @brief Core 级网络日志抽象基类。
 */
class QCURL_EXPORT QCNetworkLogger
{
public:
    virtual ~QCNetworkLogger() = default;

    /// 记录一条结构化日志。
    virtual void log(const NetworkLogEntry &entry) = 0;

    /// 便利重载：以当前 UTC 时间构造 entry 并转发给虚函数。
    void log(NetworkLogLevel level, const QString &category, const QString &message);
};

/**
 * @brief 日志级别转字符串
 */
QCURL_EXPORT QString logLevelToString(NetworkLogLevel level);

} // namespace QCurl

#endif // QCNETWORKLOGGER_H
