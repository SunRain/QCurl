// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

/**
 * @file main.cpp
 * @brief 统一策略 Middleware + Logger（纯离线）演示程序
 *
 * 目标：
 * - 展示上层如何显式启用：setLogger() + addMiddleware()
 * - 默认纯离线：使用 QCNetworkMockHandler 回放，不访问外网
 * - 展示标准 middleware：
 *   - QCUnifiedRetryPolicyMiddleware（默认重试注入，显式优先）
 *   - QCRedactingLoggingMiddleware（脱敏日志，不输出 body 明文）
 *   - QCObservabilityMiddleware（结构化观测事件，脱敏 URL）
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkLogger.h"
#include "QCNetworkMiddleware.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRetryPolicy.h"

#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>

#include <chrono>

using namespace QCurl;

static bool waitForFinished(QCNetworkReply *reply, int timeoutMs)
{
    if (!reply) {
        return false;
    }

    if (reply->isFinished()) {
        return true;
    }

    QEventLoop loop;
    QObject::connect(reply, &QCNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    return reply->isFinished();
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qDebug() << "=== QCurl UnifiedPolicyMiddlewareOfflineDemo ===";
    qDebug() << "说明：该示例默认纯离线，使用 MockHandler 回放，不访问外网。";

    QCNetworkAccessManager manager;

    // 1) Logger：显式启用（库默认行为不变）
    QCNetworkDefaultLogger logger;
    logger.setMinLogLevel(NetworkLogLevel::Debug);
    logger.enableConsoleOutput(true);
    manager.setLogger(&logger);

    // 2) MockHandler：显式启用（纯离线）
    QCNetworkMockHandler mock;
    mock.clear();
    mock.setGlobalDelay(0);
    manager.setMockHandler(&mock);

    // 3) Middleware：显式注入（库默认行为不变）
    QCNetworkRetryPolicy defaultPolicy;
    defaultPolicy.maxRetries        = 1;
    defaultPolicy.initialDelay      = std::chrono::milliseconds(10);
    defaultPolicy.backoffMultiplier = 1.0;
    defaultPolicy.maxDelay          = std::chrono::milliseconds(100);

    QCUnifiedRetryPolicyMiddleware retryPolicyMw(defaultPolicy);
    QCRedactingLoggingMiddleware redactingLogMw;
    QCObservabilityMiddleware observabilityMw;

    manager.addMiddleware(&retryPolicyMw);
    manager.addMiddleware(&redactingLogMw);
    manager.addMiddleware(&observabilityMw);

    // --------------------------------------------------------------------
    // 场景 1：未显式 setRetryPolicy()，由 middleware 注入默认重试策略（503 → 200）
    // - URL 含 ?token=... 用于验证脱敏（示例自身不打印敏感值）
    // - Authorization header 仅用于模拟上层注入（示例自身不打印敏感值）
    // --------------------------------------------------------------------

    const QByteArray secretToken("DEMO_SECRET_TOKEN");
    const QUrl urlRetry(QStringLiteral("http://example.com/offline/demo/retry?token=%1")
                            .arg(QString::fromLatin1(secretToken)));

    mock.enqueueResponse(HttpMethod::Get, urlRetry, QByteArray("fail"), 503);
    mock.enqueueResponse(HttpMethod::Get, urlRetry, QByteArray("ok"), 200);

    QCNetworkRequest requestRetry(urlRetry);
    requestRetry.setRawHeader("Authorization", QByteArray("Bearer ") + secretToken);

    auto *replyRetry = manager.sendGet(requestRetry);
    int retryCount   = 0;
    QObject::connect(replyRetry,
                     &QCNetworkReply::retryAttempt,
                     replyRetry,
                     [&retryCount](int, NetworkError) { retryCount++; });

    if (!waitForFinished(replyRetry, 2000)) {
        qWarning() << "场景1：请求超时（不应发生于纯离线路径）";
    } else {
        qDebug() << "场景1：finished，retryCount=" << retryCount
                 << "error=" << static_cast<int>(replyRetry->error())
                 << "httpStatusCode=" << replyRetry->httpStatusCode();
    }
    replyRetry->deleteLater();

    // --------------------------------------------------------------------
    // 场景 2：显式 setRetryPolicy(noRetry())，不应被默认策略覆盖（500 → 200）
    // --------------------------------------------------------------------

    const QUrl urlNoRetry(QStringLiteral("http://example.com/offline/demo/no_retry?token=%1")
                              .arg(QString::fromLatin1(secretToken)));

    mock.enqueueResponse(HttpMethod::Get, urlNoRetry, QByteArray("fail"), 500);
    mock.enqueueResponse(HttpMethod::Get, urlNoRetry, QByteArray("ok"), 200);

    QCNetworkRequest requestNoRetry(urlNoRetry);
    requestNoRetry.setRetryPolicy(QCNetworkRetryPolicy::noRetry()); // 显式禁用重试

    auto *replyNoRetry = manager.sendGet(requestNoRetry);
    int retryCount2    = 0;
    QObject::connect(replyNoRetry,
                     &QCNetworkReply::retryAttempt,
                     replyNoRetry,
                     [&retryCount2](int, NetworkError) { retryCount2++; });

    if (!waitForFinished(replyNoRetry, 2000)) {
        qWarning() << "场景2：请求超时（不应发生于纯离线路径）";
    } else {
        qDebug() << "场景2：finished，retryCount=" << retryCount2
                 << "error=" << static_cast<int>(replyNoRetry->error())
                 << "httpStatusCode=" << replyNoRetry->httpStatusCode();
    }
    replyNoRetry->deleteLater();

    manager.setMockHandler(nullptr);
    manager.setLogger(nullptr);

    qDebug() << "=== 演示完成 ===";
    return 0;
}
