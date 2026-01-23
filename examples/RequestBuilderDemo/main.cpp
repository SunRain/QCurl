// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

/**
 * @file main.cpp
 * @brief RequestBuilder 流式构建器演示程序
 *
 * 展示如何使用流式 API 简化请求配置。
 *
 * 功能演示:
 * 1. 基础 GET 请求
 * 2. POST JSON 数据
 * 3. 复杂请求构建（查询参数 + Header）
 * 4. 流式 API vs 传统 API 对比
 *
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRequestBuilder.h"

#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

using namespace QCurl;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qDebug() << "=== QCurl RequestBuilder 流式API演示 ===\n";

    auto *manager = new QCNetworkAccessManager(&app);

    // ========================================================================
    // 示例 1: 基础 GET 请求
    // ========================================================================

    qDebug() << ">>> 示例 1: 基础 GET 请求";

    auto *reply1 = manager->newRequest(QUrl("https://httpbin.org/get"))
                       .withHeader("User-Agent", "QCurl/2.16.0")
                       .withTimeout(30000)
                       .sendGet();

    qDebug() << "请求已发送，等待响应...";

    QEventLoop loop1;
    QObject::connect(reply1, &QCNetworkReply::finished, [&]() {
        if (reply1->error() == NetworkError::NoError) {
            auto data = reply1->readAll();
            qDebug() << "✅ 请求成功! 响应大小:" << data->size() << "bytes";
        } else {
            qDebug() << "❌ 请求失败:" << reply1->errorString();
        }
        loop1.quit();
    });

    QTimer::singleShot(5000, &loop1, &QEventLoop::quit);
    loop1.exec();

    reply1->deleteLater();

    // ========================================================================
    // 示例 2: POST JSON 数据
    // ========================================================================

    qDebug() << "\n>>> 示例 2: POST JSON 数据";

    QJsonObject json;
    json["name"]     = "QCurl";
    json["version"]  = "2.16.0";
    json["features"] = "RequestBuilder";

    QByteArray jsonData = QJsonDocument(json).toJson(QJsonDocument::Compact);

    auto *reply2 = manager->newRequest(QUrl("https://httpbin.org/post"))
                       .withHeader("Content-Type", "application/json")
                       .withHeader("User-Agent", "QCurl/2.16.0")
                       .withTimeout(30000)
                       .sendPost(jsonData);

    qDebug() << "发送 JSON POST 请求...";

    QEventLoop loop2;
    QObject::connect(reply2, &QCNetworkReply::finished, [&]() {
        if (reply2->error() == NetworkError::NoError) {
            qDebug() << "✅ JSON POST 成功!";
        } else {
            qDebug() << "❌ POST 失败:" << reply2->errorString();
        }
        loop2.quit();
    });

    QTimer::singleShot(5000, &loop2, &QEventLoop::quit);
    loop2.exec();

    reply2->deleteLater();

    // ========================================================================
    // 示例 3: 复杂请求 - 查询参数 + Header 链式调用
    // ========================================================================

    qDebug() << "\n>>> 示例 3: 复杂请求构建";

    auto *reply3 = manager->newRequest(QUrl("https://httpbin.org/get"))
                       .withQueryParam("page", "1")
                       .withQueryParam("limit", "20")
                       .withQueryParam("sort", "desc")
                       .withHeader("Authorization", "Bearer fake-token-123")
                       .withHeader("Accept", "application/json")
                       .withFollowLocation(true)
                       .withTimeout(15000)
                       .sendGet();

    qDebug() << "发送复杂请求...";

    QEventLoop loop3;
    QObject::connect(reply3, &QCNetworkReply::finished, [&]() {
        if (reply3->error() == NetworkError::NoError) {
            qDebug() << "✅ 复杂请求成功!";
            qDebug() << "最终URL:" << reply3->url().toString();
        } else {
            qDebug() << "❌ 请求失败:" << reply3->errorString();
        }
        loop3.quit();
    });

    QTimer::singleShot(5000, &loop3, &QEventLoop::quit);
    loop3.exec();

    reply3->deleteLater();

    qDebug() << "\n=== 演示完成 ===";
    qDebug() << "\n💡 提示：流式 API 可以大幅简化代码，提高可读性";

    return 0;
}
