// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

/**
 * @file
 * @brief 声明默认日志实现。
 */

#ifndef QCNETWORKDEFAULTLOGGER_H
#define QCNETWORKDEFAULTLOGGER_H

#include "QCNetworkLogger.h"

#include <QList>
#include <QScopedPointer>
#include <QString>

#include <functional>

namespace QCurl {

class QCNetworkDefaultLoggerPrivate;

/**
 * @brief 默认网络日志实现。
 *
 * 该类型是 `QCNetworkLogger` 的默认 Core helper 实现。通过
 * `QCNetworkLoggerHandle::create<QCNetworkDefaultLogger>()` 创建 opaque handle 后，
 * 可通过 `QCNetworkAccessManager::setLogger()` 注入；manager 和已创建的 reply
 * 持有独立 handle snapshot。
 */
class QCURL_EXPORT QCNetworkDefaultLogger : public QCNetworkLogger
{
public:
    QCNetworkDefaultLogger();
    ~QCNetworkDefaultLogger() override;

    Q_DISABLE_COPY_MOVE(QCNetworkDefaultLogger)

    using QCNetworkLogger::log;

    /// 启用或关闭控制台输出。
    void enableConsoleOutput(bool enable = true);

    /**
     * @brief 启用文件输出与轮转。
     * @param filePath 非空日志文件路径。
     * @param maxSize 轮转阈值；0 表示采用默认值，负数无效。
     * @param backupCount 备份文件数量；不得小于 0。
     * @return 配置接受时返回成功；失败时旧文件输出配置保持不变。
     */
    [[nodiscard]] QCNetworkLogResult enableFileOutput(const QString &filePath,
                                                      qint64 maxSize  = 0,
                                                      int backupCount = 5);

    /// 禁用文件输出。
    void disableFileOutput();

    /**
     * @brief 设置自定义日志回调。
     *
     * 回调由串行输出器在不持有 logger 状态锁时调用，可以同步重入本 logger。
     * 重入产生的日志会在当前记录完成后继续输出，避免递归执行外部代码。
     */
    void setCustomCallback(std::function<void(const NetworkLogEntry &)> callback);

    /// 设置输出格式，支持 `%{level}` / `%{time}` / `%{category}` / `%{message}`。
    void setLogFormat(const QString &format);

    /// 设置最小日志级别。
    void setMinLogLevel(NetworkLogLevel level);

    /// 返回当前最小日志级别。
    [[nodiscard]] NetworkLogLevel minLogLevel() const;

    /// 清空当前缓存的日志条目。
    void clear();

    /// 返回当前缓存的日志条目。
    [[nodiscard]] QList<NetworkLogEntry> entries() const;

    /**
     * @brief 记录一条结构化日志。
     * @param entry 要记录的日志值。
     * @return 当前 entry 的过滤、成功或文件输出失败结果。
     *
     * entry 在接受后先写入有界内存记录，再按调用时配置快照串行执行 console、文件和
     * callback。函数不在 logger 状态锁内执行文件 I/O、Qt logging 或用户 callback。
     */
    [[nodiscard]] QCNetworkLogResult log(const NetworkLogEntry &entry) override;

private:
    Q_DECLARE_PRIVATE(QCNetworkDefaultLogger)
    QScopedPointer<QCNetworkDefaultLoggerPrivate> d_ptr;
};

} // namespace QCurl

#endif // QCNETWORKDEFAULTLOGGER_H
