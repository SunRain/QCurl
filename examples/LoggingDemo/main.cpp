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

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include <QUrl>
#include <QEventLoop>

#include "QCNetworkAccessManager.h"
#include "QCNetworkRequest.h"
#include "QCNetworkReply.h"
#include "QCNetworkLogger.h"

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
    NetworkLogLevel m_minLevel = NetworkLogLevel::Info;

    void log(NetworkLogLevel level, const QString &category, const QString &message) override {
        totalRequests++;

        if (level == NetworkLogLevel::Error) {
            errorRequests++;
        }

        // 打印日志
        QString levelStr;
        switch (level) {
        case NetworkLogLevel::Debug:   levelStr = "DEBUG"; break;
        case NetworkLogLevel::Info:    levelStr = "INFO"; break;
        case NetworkLogLevel::Warning: levelStr = "WARNING"; break;
        case NetworkLogLevel::Error:   levelStr = "ERROR"; break;
        }

        qDebug().noquote() << QString("[%1] %2: %3").arg(levelStr, category, message);
    }

    void setMinLogLevel(NetworkLogLevel level) override {
        m_minLevel = level;
    }

    NetworkLogLevel minLogLevel() const override {
        return m_minLevel;
    }

    void printStatistics() {
        qDebug() << "\n=== 统计信息 ===";
        qDebug() << "总请求数:" << totalRequests;
        qDebug() << "错误数:" << errorRequests;
        qDebug() << "成功率:" << QString::number((1.0 - (double)errorRequests / totalRequests) * 100, 'f', 2) << "%";
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

    auto *manager1 = new QCNetworkAccessManager();
    auto *defaultLogger = new QCNetworkDefaultLogger();

    defaultLogger->setMinLogLevel(NetworkLogLevel::Info);
    defaultLogger->enableConsoleOutput(true);

    manager1->setLogger(defaultLogger);

    qDebug() << "Logger 已设置，最小日志级别:" << (int)defaultLogger->minLogLevel();

    // ========================================================================
    // 示例 2: 启用文件日志
    // ========================================================================

    qDebug() << "\n>>> 示例 2: 启用文件日志";

    auto *manager2 = new QCNetworkAccessManager();
    auto *fileLogger = new QCNetworkDefaultLogger();

    fileLogger->setMinLogLevel(NetworkLogLevel::Debug);
    fileLogger->enableFileOutput("/tmp/qcurl-demo.log", 1024 * 1024, 3);  // 1MB, 3 个备份

    manager2->setLogger(fileLogger);

    qDebug() << "文件日志已启用: /tmp/qcurl-demo.log";

    // ========================================================================
    // 示例 3: 自定义 Logger
    // ========================================================================

    qDebug() << "\n>>> 示例 3: 自定义 Logger (统计)";

    auto *manager3 = new QCNetworkAccessManager();
    auto *statsLogger = new StatisticsLogger();

    manager3->setLogger(statsLogger);

    // 模拟一些日志记录
    statsLogger->log(NetworkLogLevel::Info, "Request", "GET http://example.com");
    statsLogger->log(NetworkLogLevel::Info, "Response", "Status: 200 OK");
    statsLogger->log(NetworkLogLevel::Info, "Request", "POST http://api.example.com/users");
    statsLogger->log(NetworkLogLevel::Error, "Response", "Status: 404 Not Found");
    statsLogger->log(NetworkLogLevel::Info, "Request", "GET http://api.example.com/data");
    statsLogger->log(NetworkLogLevel::Info, "Response", "Status: 200 OK");

    // 打印统计信息
    statsLogger->printStatistics();

    // ========================================================================
    // 示例 4: 自定义日志格式
    // ========================================================================

    qDebug() << "\n>>> 示例 4: 自定义日志格式";

    auto *manager4 = new QCNetworkAccessManager();
    auto *formattedLogger = new QCNetworkDefaultLogger();

    formattedLogger->setLogFormat("[%{time}] %{level} - %{message}");
    formattedLogger->enableConsoleOutput(true);

    manager4->setLogger(formattedLogger);

    qDebug() << "已设置自定义日志格式";

    // ========================================================================
    // 示例 5: 真实网络请求 + 日志记录
    // ========================================================================

    qDebug() << "\n>>> 示例 5: 真实网络请求 + 日志记录";

    auto *manager5 = new QCNetworkAccessManager();
    auto *requestLogger = new QCNetworkDefaultLogger();

    requestLogger->setMinLogLevel(NetworkLogLevel::Debug);
    requestLogger->enableConsoleOutput(true);
    requestLogger->enableFileOutput("/tmp/qcurl-requests.log", 1024*1024, 3);

    manager5->setLogger(requestLogger);

    // 发送真实HTTP请求
    QCNetworkRequest httpbinReq(QUrl("https://httpbin.org/get"));
    httpbinReq.setRawHeader("User-Agent", "QCurl-LoggingDemo/2.15.0");

    auto *reply = manager5->sendGet(httpbinReq);

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
    delete requestLogger;

    // ========================================================================
    // 清理
    // ========================================================================

    qDebug() << "\n=== 演示完成 ===";

    delete manager1;
    delete defaultLogger;
    delete manager2;
    delete fileLogger;
    delete manager3;
    delete statsLogger;
    delete manager4;
    delete formattedLogger;

    return 0;
}
