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

#include <memory>
#include <type_traits>
#include <utility>

namespace QCurl {

/**
 * @brief HTTP 网络日志级别
 */
enum class NetworkLogLevel : quint8 {
    Debug,   ///< 调试日志
    Info,    ///< 信息日志
    Warning, ///< 警告日志
    Error,   ///< 错误日志
};

class NetworkLogEntryData;
class QCNetworkLogResultData;
class QCNetworkLogger;
class QCNetworkLoggerHandlePrivate;

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
 * @brief 表示一次日志操作的结构化结果。
 *
 * 结果仅描述当前调用。`Filtered` 表示按最小级别有意忽略；`Queued` 表示同步重入时
 * 已进入串行队列，调用栈为避免递归执行外部 callback 而不等待最终文件结果。该重入
 * entry 的最终文件失败不会同步回传给已经返回的内层调用。
 */
class QCURL_EXPORT QCNetworkLogResult
{
public:
    /**
     * @brief 日志操作状态。
     */
    enum class Status {
        Success,         ///< 当前日志调用已完整成功。
        Filtered,        ///< 当前 entry 低于最小日志级别，未执行输出。
        Queued,          ///< 同步重入日志已排队；最终文件失败不会同步回传给内层调用。
        InvalidArgument, ///< 文件路径、大小或备份数量等输入无效。
        OpenFailed,      ///< 无法打开日志文件。
        RemoveFailed,    ///< 轮转时无法删除当前或目标备份文件。
        RenameFailed,    ///< 轮转时无法重命名日志或备份文件。
        WriteFailed,     ///< 写入或 flush 日志文件失败。
    };

    /// 构造成功结果。
    QCNetworkLogResult();

    /// 复制结果并保持 implicit-sharing 值语义。
    QCNetworkLogResult(const QCNetworkLogResult &other);

    /// 移动结果并保持 implicit-sharing 值语义。
    QCNetworkLogResult(QCNetworkLogResult &&other) noexcept;

    /// out-of-line 析构，隐藏结果数据布局。
    ~QCNetworkLogResult();

    /// 复制赋值。
    QCNetworkLogResult &operator=(const QCNetworkLogResult &other);

    /// 移动赋值。
    QCNetworkLogResult &operator=(QCNetworkLogResult &&other) noexcept;

    /**
     * @brief 创建指定状态的结果。
     * @param status 当前操作状态。
     * @param error 失败诊断；成功、过滤或排队状态应为空。
     * @return 本次日志操作结果。
     */
    [[nodiscard]] static QCNetworkLogResult fromStatus(Status status, const QString &error = {});

    /// 返回当前结构化状态。
    [[nodiscard]] Status status() const noexcept;

    /// 判断当前调用是否完整成功。
    [[nodiscard]] bool isSuccess() const noexcept;

    /// 返回本次失败诊断；成功、过滤或排队状态为空。
    [[nodiscard]] QString error() const;

private:
    QSharedDataPointer<QCNetworkLogResultData> d;
};

/**
 * @brief Core 级网络日志抽象基类。
 *
 * 自定义实现通过 `QCNetworkLoggerHandle::create<LoggerType>()` 创建，确保最终删除发生在
 * QCurl 库内。所有可能失败的输出使用 `QCNetworkLogResult` 表达；调用方必须显式处理或
 * 使用 `static_cast<void>(...)` 声明有意丢弃。
 */
class QCURL_EXPORT QCNetworkLogger
{
public:
    virtual ~QCNetworkLogger() = default;

    /**
     * @brief 记录一条结构化日志。
     * @param entry 要记录的日志值。
     * @return 当前调用的结构化结果；实现不得仅以诊断文本替代状态。
     */
    [[nodiscard]] virtual QCNetworkLogResult log(const NetworkLogEntry &entry) = 0;

    /**
     * @brief 以当前 UTC 时间构造 entry 并转发给虚函数。
     * @return 当前调用的结构化结果。
     */
    [[nodiscard]] QCNetworkLogResult log(NetworkLogLevel level,
                                         const QString &category,
                                         const QString &message);
};

/**
 * @brief 跨 QCurl 公共 ABI 保持 logger 共享生命周期的不透明值句柄。
 *
 * 句柄的共享控制块、allocator、deleter、复制和析构均在 QCurl 库内实现，公共 ABI
 * 不暴露 owning smart pointer。`get()` 与 `operator->()` 返回非 owning 借用；借用仅在
 * 当前句柄或同一控制块的其他句柄存活期间有效。
 *
 * @note `QCNetworkAccessManager` 保存当前句柄，创建 reply 时复制快照；替换 manager 的
 * 当前 logger 不会改变既有 reply 的 snapshot。
 * @note 句柄或借用不得跨越提供具体 logger 类型的动态模块卸载；卸载前必须释放所有
 * 句柄，因为类型析构函数与 `create()` 记录的 deleter 代码都属于该模块。
 */
class QCURL_EXPORT QCNetworkLoggerHandle
{
public:
    /// 构造空句柄。
    QCNetworkLoggerHandle();

    /// 复制句柄并共享同一个库内控制块。
    QCNetworkLoggerHandle(const QCNetworkLoggerHandle &other);

    /// 移动句柄，不访问 logger implementation。
    QCNetworkLoggerHandle(QCNetworkLoggerHandle &&other) noexcept;

    /// 在 QCurl 库内释放当前控制块引用。
    ~QCNetworkLoggerHandle();

    /// 复制赋值并共享同一个库内控制块。
    QCNetworkLoggerHandle &operator=(const QCNetworkLoggerHandle &other);

    /// 移动赋值，不访问 logger implementation。
    QCNetworkLoggerHandle &operator=(QCNetworkLoggerHandle &&other) noexcept;

    /**
     * @brief 在 QCurl 库内创建并持有指定 logger 类型。
     * @tparam LoggerType `QCNetworkLogger` 的具体派生类型。
     * @tparam Args 构造 logger 所需的参数类型。
     * @param args 转发给 logger 构造函数的参数。
     * @return 持有新 logger 的句柄；logger 的最终删除发生在 QCurl 库内。
     */
    template<typename LoggerType, typename... Args>
    [[nodiscard]] static QCNetworkLoggerHandle create(Args &&...args)
    {
        static_assert(std::is_base_of_v<QCNetworkLogger, LoggerType>,
                      "LoggerType must derive from QCNetworkLogger");
        return adopt(new LoggerType(std::forward<Args>(args)...), &destroy<LoggerType>);
    }

    /**
     * @brief 创建 logger handle，并返回受 handle 生命周期约束的具体类型借用。
     * @tparam LoggerType `QCNetworkLogger` 的具体派生类型。
     * @tparam Args 构造 logger 所需的参数类型。
     * @param loggerOut 成功后写入具体 logger 的非 owning 借用；不得为 `nullptr`。
     * @param args 转发给 logger 构造函数的参数。
     * @return 持有新 logger 的 opaque 句柄。
     *
     * `loggerOut` 仅用于配置或查询具体实现，借用在返回句柄及其副本全部销毁后立即失效。
     */
    template<typename LoggerType, typename... Args>
    [[nodiscard]] static QCNetworkLoggerHandle createWithBorrow(LoggerType **loggerOut,
                                                                Args &&...args)
    {
        static_assert(std::is_base_of_v<QCNetworkLogger, LoggerType>,
                      "LoggerType must derive from QCNetworkLogger");
        Q_ASSERT(loggerOut);
        auto *logger = new LoggerType(std::forward<Args>(args)...);
        if (loggerOut) {
            *loggerOut = logger;
        }
        return adopt(logger, &destroy<LoggerType>);
    }

    /**
     * @brief 返回当前 logger 的非 owning 借用。
     * @return 空句柄返回 `nullptr`；非空时借用在共享控制块存活期间有效。
     */
    [[nodiscard]] QCNetworkLogger *get() const noexcept;

    /**
     * @brief 返回当前 logger 的非 owning 指针以调用日志接口。
     * @return 语义与 `get()` 相同；调用前必须保证句柄非空。
     */
    [[nodiscard]] QCNetworkLogger *operator->() const noexcept;

    /// 判断当前句柄是否持有 logger。
    [[nodiscard]] explicit operator bool() const noexcept;

    /// 判断两个句柄是否引用同一个 logger implementation。
    friend QCURL_EXPORT bool operator==(const QCNetworkLoggerHandle &left,
                                        const QCNetworkLoggerHandle &right) noexcept;

    /// 判断两个句柄是否引用不同 logger implementation。
    friend QCURL_EXPORT bool operator!=(const QCNetworkLoggerHandle &left,
                                        const QCNetworkLoggerHandle &right) noexcept;

private:
    using DestroyFunction = void (*)(QCNetworkLogger *) noexcept;

    template<typename LoggerType>
    static void destroy(QCNetworkLogger *logger) noexcept
    {
        delete static_cast<LoggerType *>(logger);
    }

    explicit QCNetworkLoggerHandle(QCNetworkLoggerHandlePrivate *d);
    [[nodiscard]] static QCNetworkLoggerHandle adopt(QCNetworkLogger *logger,
                                                     DestroyFunction destroy);

    std::unique_ptr<QCNetworkLoggerHandlePrivate> d_ptr;
};

/**
 * @brief 日志级别转字符串
 */
QCURL_EXPORT QString logLevelToString(NetworkLogLevel level);

} // namespace QCurl

#endif // QCNETWORKLOGGER_H
