// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

/**
 * @file main.cpp
 * @brief canonical request API 演示程序
 *
 * 展示如何使用 QCNetworkRequest + QCNetworkAccessManager::send* 配置请求。
 *
 * 功能演示:
 * 1. 基础 GET 请求
 * 2. POST JSON 数据
 * 3. 复杂请求构建（查询参数 + Header）
 * 4. canonical API 用法
 *
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"

#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrlQuery>

#include <chrono>

using namespace QCurl;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qDebug() << "=== QCurl canonical request API 演示 ===\n";

    auto *manager = new QCNetworkAccessManager(&app);

    // ========================================================================
    // 示例 1: 基础 GET 请求
    // ========================================================================

    qDebug() << ">>> 示例 1: 基础 GET 请求";

    QCNetworkRequest request1(QUrl("https://httpbin.org/get"));
    request1.setRawHeader("User-Agent", "QCurl/2.16.0").setTimeout(std::chrono::milliseconds(30000));
    auto *reply1 = manager->sendGet(request1);

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
    json["features"] = "CanonicalRequest";

    QByteArray jsonData = QJsonDocument(json).toJson(QJsonDocument::Compact);

    QCNetworkRequest request2(QUrl("https://httpbin.org/post"));
    request2.setRawHeader("Content-Type", "application/json")
        .setRawHeader("User-Agent", "QCurl/2.16.0")
        .setTimeout(std::chrono::milliseconds(30000));
    auto *reply2 = manager->sendPost(request2, jsonData);

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

    QUrl url3("https://httpbin.org/get");
    QUrlQuery query3(url3);
    query3.addQueryItem("page", "1");
    query3.addQueryItem("limit", "20");
    query3.addQueryItem("sort", "desc");
    url3.setQuery(query3);

    QCNetworkRequest request3(url3);
    request3.setRawHeader("Authorization", "Bearer fake-token-123")
        .setRawHeader("Accept", "application/json")
        .setFollowLocation(true)
        .setTimeout(std::chrono::milliseconds(15000));
    auto *reply3 = manager->sendGet(request3);

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
    qDebug() << "\n💡 提示：请改用 QCNetworkRequest + QCNetworkAccessManager::send* canonical API";

    return 0;
}
