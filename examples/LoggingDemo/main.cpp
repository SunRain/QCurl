// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

/**
 * @file main.cpp
 * @brief Logger 系统演示程序
 *
 * 展示如何使用 QCNetworkLogger 记录网络请求的详细信息。
 *
 * 功能演示:
 * 1. 使用默认 Logger (控制台输出)
 * 2. 启用文件日志
 * 3. 设置日志级别
 * 4. 自定义日志格式
 * 5. 自定义 Logger 实现
 *
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkDefaultLogger.h"
#include "QCNetworkLogger.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"

#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>

using namespace QCurl;

/**
 * @brief 自定义 Logger 示例
 *
 * 统计请求数量和错误数量
 */
class StatisticsLogger : public QCNetworkLogger
{
public:
    int totalRequests = 0;
    int errorRequests = 0;
    using QCNetworkLogger::log;

    QCNetworkLogResult log(const NetworkLogEntry &entry) override
    {
        totalRequests++;

        if (entry.level() == NetworkLogLevel::Error) {
            errorRequests++;
        }

        qDebug().noquote() << QStringLiteral("[%1] %2: %3")
                                  .arg(logLevelToString(entry.level()),
                                       entry.category(),
                                       entry.message());
        return {};
    }

    void printStatistics()
    {
        qDebug() << "\n=== 统计信息 ===";
        qDebug() << "总请求数:" << totalRequests;
        qDebug() << "错误数:" << errorRequests;
        qDebug() << "成功率:"
                 << QString::number((1.0
                                     - static_cast<double>(errorRequests)
                                           / static_cast<double>(totalRequests))
                                        * 100.0,
                                    'f',
                                    2)
                 << "%";
    }
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qDebug() << "=== QCurl Logger 系统演示 ===\n";

    // ========================================================================
    // 示例 1: 使用默认 Logger
    // ========================================================================

    qDebug() << ">>> 示例 1: 使用默认 Logger";

    auto *manager1                                      = new QCNetworkAccessManager();
    QCNetworkDefaultLogger *defaultLoggerImplementation = nullptr;
    auto defaultLogger = QCNetworkLoggerHandle::createWithBorrow(&defaultLoggerImplementation);

    defaultLoggerImplementation->setMinLogLevel(NetworkLogLevel::Info);
    defaultLoggerImplementation->enableConsoleOutput(true);

    manager1->setLogger(defaultLogger);

    qDebug() << "Logger 已设置，最小日志级别:"
             << static_cast<int>(defaultLoggerImplementation->minLogLevel());

    // ========================================================================
    // 示例 2: 启用文件日志
    // ========================================================================

    qDebug() << "\n>>> 示例 2: 启用文件日志";

    auto *manager2                                   = new QCNetworkAccessManager();
    QCNetworkDefaultLogger *fileLoggerImplementation = nullptr;
    auto fileLogger = QCNetworkLoggerHandle::createWithBorrow(&fileLoggerImplementation);

    fileLoggerImplementation->setMinLogLevel(NetworkLogLevel::Debug);
    const QCNetworkLogResult fileConfigResult
        = fileLoggerImplementation->enableFileOutput(QStringLiteral("/tmp/qcurl-demo.log"),
                                                     1024 * 1024,
                                                     3);
    if (!fileConfigResult.isSuccess()) {
        qCritical().noquote() << fileConfigResult.error();
        return 1;
    }

    manager2->setLogger(fileLogger);

    qDebug() << "文件日志已启用: /tmp/qcurl-demo.log";

    // ========================================================================
    // 示例 3: 自定义 Logger
    // ========================================================================

    qDebug() << "\n>>> 示例 3: 自定义 Logger (统计)";

    auto *manager3                     = new QCNetworkAccessManager();
    StatisticsLogger *statisticsLogger = nullptr;
    auto statsLogger                   = QCNetworkLoggerHandle::createWithBorrow(&statisticsLogger);

    manager3->setLogger(statsLogger);

    // 模拟一些日志记录
    static_cast<void>(statsLogger->log(NetworkLogLevel::Info,
                                       QStringLiteral("Request"),
                                       QStringLiteral("GET http://example.com")));
    static_cast<void>(statsLogger->log(NetworkLogLevel::Info,
                                       QStringLiteral("Response"),
                                       QStringLiteral("Status: 200 OK")));
    static_cast<void>(statsLogger->log(NetworkLogLevel::Info,
                                       QStringLiteral("Request"),
                                       QStringLiteral("POST http://api.example.com/users")));
    static_cast<void>(statsLogger->log(NetworkLogLevel::Error,
                                       QStringLiteral("Response"),
                                       QStringLiteral("Status: 404 Not Found")));
    static_cast<void>(statsLogger->log(NetworkLogLevel::Info,
                                       QStringLiteral("Request"),
                                       QStringLiteral("GET http://api.example.com/data")));
    static_cast<void>(statsLogger->log(NetworkLogLevel::Info,
                                       QStringLiteral("Response"),
                                       QStringLiteral("Status: 200 OK")));

    // 打印统计信息
    statisticsLogger->printStatistics();

    // ========================================================================
    // 示例 4: 自定义日志格式
    // ========================================================================

    qDebug() << "\n>>> 示例 4: 自定义日志格式";

    auto *manager4                                        = new QCNetworkAccessManager();
    QCNetworkDefaultLogger *formattedLoggerImplementation = nullptr;
    auto formattedLogger = QCNetworkLoggerHandle::createWithBorrow(&formattedLoggerImplementation);

    formattedLoggerImplementation->setLogFormat(QStringLiteral("[%{time}] %{level} - %{message}"));
    formattedLoggerImplementation->enableConsoleOutput(true);

    manager4->setLogger(formattedLogger);

    qDebug() << "已设置自定义日志格式";

    // ========================================================================
    // 示例 5: 真实网络请求 + 日志记录
    // ========================================================================

    qDebug() << "\n>>> 示例 5: 真实网络请求 + 日志记录";

    auto *manager5                                      = new QCNetworkAccessManager();
    QCNetworkDefaultLogger *requestLoggerImplementation = nullptr;
    auto requestLogger = QCNetworkLoggerHandle::createWithBorrow(&requestLoggerImplementation);

    requestLoggerImplementation->setMinLogLevel(NetworkLogLevel::Debug);
    requestLoggerImplementation->enableConsoleOutput(true);
    const QCNetworkLogResult requestLogConfigResult
        = requestLoggerImplementation->enableFileOutput(QStringLiteral("/tmp/qcurl-requests.log"),
                                                        1024 * 1024,
                                                        3);
    if (!requestLogConfigResult.isSuccess()) {
        qCritical().noquote() << requestLogConfigResult.error();
        return 1;
    }

    manager5->setLogger(requestLogger);

    // 发送真实HTTP请求
    QCNetworkRequest httpbinReq(QUrl("https://httpbin.org/get"));
    httpbinReq.setRawHeader("User-Agent", "QCurl-LoggingDemo/2.15.0");

    auto *reply = manager5->get(httpbinReq);

    qDebug() << "发送 GET 请求到 httpbin.org...";

    // 等待响应
    QEventLoop loop;
    QObject::connect(reply, &QCNetworkReply::finished, &loop, &QEventLoop::quit);

    // 设置5秒超时
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);

    loop.exec();

    if (reply->isFinished()) {
        if (reply->error() == NetworkError::NoError) {
            auto data = reply->readAll();
            qDebug() << "✅ 请求成功! 响应大小:" << data->size() << "bytes";
            qDebug() << "📝 日志已记录到: /tmp/qcurl-requests.log";
        } else {
            qDebug() << "❌ 请求失败:" << reply->errorString();
        }
    } else {
        qDebug() << "⏱️ 请求超时（可能网络不可用）";
    }

    reply->deleteLater();
    delete manager5;

    // ========================================================================
    // 清理
    // ========================================================================

    qDebug() << "\n=== 演示完成 ===";

    delete manager1;
    delete manager2;
    delete manager3;
    delete manager4;

    return 0;
}
