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
 * 该类型是 `QCNetworkLogger` 的默认 Core helper 实现。调用方负责保证 logger
 * 对象生命周期覆盖 manager 使用期。
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

    /// 启用文件输出与轮转。
    void enableFileOutput(const QString &filePath, qint64 maxSize = 0, int backupCount = 5);

    /// 禁用文件输出。
    void disableFileOutput();

    /**
     * @brief 设置自定义日志回调。
     *
     * @note 回调会在 logger 内部互斥锁持有期间同步执行。回调不应重入本
     * logger 的 `log()/entries()/clear()/set*()`，否则可能形成死锁。
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

    /// 记录一条结构化日志。
    void log(const NetworkLogEntry &entry) override;

private:
    Q_DECLARE_PRIVATE(QCNetworkDefaultLogger)
    QScopedPointer<QCNetworkDefaultLoggerPrivate> d_ptr;
};

} // namespace QCurl

#endif // QCNETWORKDEFAULTLOGGER_H
