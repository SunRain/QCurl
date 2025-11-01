// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#ifndef QCNETWORKLOGGER_H
#define QCNETWORKLOGGER_H

#include <QString>
#include <QDateTime>
#include <functional>
#include "QCGlobal.h"

namespace QCurl {

/**
 * @brief HTTP 网络日志级别
 */
enum class NetworkLogLevel {
    Debug,      ///< 调试日志
    Info,       ///< 信息日志
    Warning,    ///< 警告日志
    Error       ///< 错误日志
};

/**
 * @brief 网络日志记录结构体
 */
struct NetworkLogEntry {
    NetworkLogLevel level;      ///< 日志级别
    QString category;           ///< 日志分类（如 "Request", "Response"）
    QString message;            ///< 日志消息
    QDateTime timestamp;        ///< 时间戳
    QString sourceFile;         ///< 源文件名（可选）
    int sourceLine = -1;        ///< 源代码行号（可选）
    
    /**
     * @brief 将日志转换为 JSON 格式
     * @return JSON 格式的日志字符串
     */
    QString toJson() const;
    
    /**
     * @brief 将日志转换为纯文本格式
     * @return 纯文本格式的日志字符串
     */
    QString toPlainText() const;
};

/**
 * @brief HTTP 网络日志抽象基类
 * 
 * 提供统一的日志接口，支持多种输出方式和日志级别。
 * 用户可以继承此类实现自定义日志处理。
 * 
 * 
 * @example
 * @code
 * auto *logger = new QCNetworkDefaultLogger();
 * logger->setMinLogLevel(NetworkLogLevel::Info);
 * logger->enableFileOutput("/tmp/qcurl.log");
 * manager->setLogger(logger);
 * @endcode
 */
class QCURL_EXPORT QCNetworkLogger
{
public:
    virtual ~QCNetworkLogger() = default;
    
    /**
     * @brief 记录日志
     * @param level 日志级别
     * @param category 日志分类
     * @param message 日志消息
     */
    virtual void log(NetworkLogLevel level, const QString &category, const QString &message) = 0;
    
    /**
     * @brief 记录带详细信息的日志
     * @param entry 日志条目
     */
    virtual void logEntry(const NetworkLogEntry &entry) {
        log(entry.level, entry.category, entry.message);
    }
    
    /**
     * @brief 设置最小日志级别
     * @param level 日志级别（低于此级别的日志将被忽略）
     */
    virtual void setMinLogLevel(NetworkLogLevel level) = 0;
    
    /**
     * @brief 获取当前最小日志级别
     */
    virtual NetworkLogLevel minLogLevel() const = 0;
    
    /**
     * @brief 清空日志
     */
    virtual void clear() {}
};

/**
 * @brief 默认的日志实现
 * 
 * 支持多种输出方式：
 * - 控制台输出（qDebug）
 * - 文件输出
 * - 自定义回调
 * 
 */
class QCURL_EXPORT QCNetworkDefaultLogger : public QCNetworkLogger
{
public:
    QCNetworkDefaultLogger();
    ~QCNetworkDefaultLogger() override;
    
    /**
     * @brief 启用控制台输出
     */
    void enableConsoleOutput(bool enable = true);
    
    /**
     * @brief 启用文件输出
     * @param filePath 日志文件路径
     * @param maxSize 单个日志文件的最大大小（字节），0 表示无限制
     * @param backupCount 保留的旧日志文件数量
     */
    void enableFileOutput(const QString &filePath, qint64 maxSize = 0, int backupCount = 5);
    
    /**
     * @brief 禁用文件输出
     */
    void disableFileOutput();
    
    /**
     * @brief 设置自定义日志回调
     * @param callback 回调函数，接收日志条目
     */
    void setCustomCallback(std::function<void(const NetworkLogEntry &)> callback);
    
    /**
     * @brief 设置日志格式
     * @param format 格式字符串，支持以下占位符：
     *   - %{level}   日志级别
     *   - %{time}    时间戳
     *   - %{category} 日志分类
     *   - %{message} 日志消息
     * 
     * 默认格式："%{time} [%{level}] %{category}: %{message}"
     */
    void setLogFormat(const QString &format);
    
    // QCNetworkLogger interface
    void log(NetworkLogLevel level, const QString &category, const QString &message) override;
    void setMinLogLevel(NetworkLogLevel level) override;
    NetworkLogLevel minLogLevel() const override;
    void clear() override;
    
    /**
     * @brief 获取所有日志条目
     * @return 日志条目列表
     */
    QList<NetworkLogEntry> entries() const;

private:
    class Private;
    std::unique_ptr<Private> d_ptr;
};

/**
 * @brief 日志级别转字符串
 */
QCURL_EXPORT QString logLevelToString(NetworkLogLevel level);

/**
 * @brief 字符串转日志级别
 */
QCURL_EXPORT NetworkLogLevel stringToLogLevel(const QString &str);

} // namespace QCurl

#endif // QCNETWORKLOGGER_H
