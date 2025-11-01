// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

/**
 * @file main.cpp
 * @brief CancelToken 取消令牌演示程序
 *
 * 展示如何使用取消令牌批量管理和取消网络请求。
 *
 * 功能演示:
 * 1. 批量请求 + 一键取消
 * 2. 自动超时取消
 * 3. 取消信号监听
 * 4. 请求完成信号
 *
 */

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include <QEventLoop>

#include "QCNetworkAccessManager.h"
#include "QCNetworkCancelToken.h"
#include "QCNetworkRequest.h"
#include "QCNetworkReply.h"

using namespace QCurl;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qDebug() << "=== QCurl CancelToken 演示 ===\n";

    // ========================================================================
    // 示例 1: 批量请求 + 一键取消
    // ========================================================================

    qDebug() << ">>> 示例 1: 创建5个延迟请求，2秒后取消所有";

    auto *manager = new QCNetworkAccessManager();
    auto *token = new QCNetworkCancelToken();

    QList<QCNetworkReply*> replies;

    // 创建5个延迟请求
    for (int i = 1; i <= 5; ++i) {
        QUrl url(QString("https://httpbin.org/delay/%1").arg(i));
        QCNetworkRequest req(url);
        auto *reply = manager->sendGet(req);

        token->attach(reply);
        replies.append(reply);

        qDebug() << QString("  请求 %1 已创建: delay/%2秒").arg(i).arg(i);
    }

    qDebug() << "Token 附加请求数:" << token->attachedCount();

    // 监听取消信号
    QObject::connect(token, &QCNetworkCancelToken::cancelled, []() {
        qDebug() << "\n🔔 Token cancelled 信号触发!";
    });

    // 2秒后取消所有请求
    QTimer::singleShot(2000, [token]() {
        qDebug() << "\n⏱️  2秒到，执行 cancel()...";
        token->cancel();
        qDebug() << "Token 状态: isCancelled =" << token->isCancelled();
        qDebug() << "Token 剩余请求数:" << token->attachedCount();
    });

    // ========================================================================
    // 示例 2: 自动超时取消
    // ========================================================================

    QTimer::singleShot(5000, [&]() {
        qDebug() << "\n>>> 示例 2: 自动超时取消（3秒后自动取消）";

        auto *token2 = new QCNetworkCancelToken();
        token2->setAutoTimeout(3000);  // 3秒后自动取消

        // 监听取消信号
        QObject::connect(token2, &QCNetworkCancelToken::cancelled, []() {
            qDebug() << "🔔 自动超时触发，所有请求已取消!";
        });

        // 创建3个长时间请求
        for (int i = 1; i <= 3; ++i) {
            QUrl url(QString("https://httpbin.org/delay/%1").arg(i * 2));
            auto *reply = manager->sendGet(QCNetworkRequest(url));
            token2->attach(reply);
            qDebug() << QString("  请求 %1: delay/%2秒").arg(i).arg(i * 2);
        }

        qDebug() << "已创建3个请求，将在3秒后自动取消...";
    });

    // ========================================================================
    // 等待演示完成
    // ========================================================================

    // 10秒后退出
    QTimer::singleShot(10000, [&]() {
        qDebug() << "\n=== 演示完成 ===";
        app.quit();
    });

    return app.exec();
}
