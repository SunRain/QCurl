// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

/**
 * @file main.cpp
 * @brief Middleware 中间件演示程序
 *
 * 展示如何使用中间件拦截和处理网络请求/响应。
 *
 * 功能演示:
 * 1. 添加日志中间件
 * 2. 添加错误处理中间件
 * 3. 中间件链式执行
 * 4. 自定义中间件
 * 5. 中间件管理（添加/移除/清空）
 *
 */

#include <QCoreApplication>
#include <QDebug>
#include <QUrl>
#include <QEventLoop>
#include <QTimer>

#include "QCNetworkAccessManager.h"
#include "QCNetworkRequest.h"
#include "QCNetworkReply.h"
#include "QCNetworkMiddleware.h"

using namespace QCurl;

/**
 * @brief 自定义中间件：请求计时
 */
class TimingMiddleware : public QCNetworkMiddleware
{
public:
    void onRequestPreSend(QCNetworkRequest &request) override {
        qDebug() << "[Timing] 请求开始:" << request.url().toString();
        startTime = QDateTime::currentMSecsSinceEpoch();
    }

    void onResponseReceived(QCNetworkReply *reply) override {
        qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - startTime;
        qDebug() << "[Timing] 请求完成:" << reply->url().toString()
                 << "耗时:" << elapsed << "ms";
    }

    QString name() const override {
        return "TimingMiddleware";
    }

private:
    qint64 startTime = 0;
};

/**
 * @brief 自定义中间件：请求统计
 */
class StatisticsMiddleware : public QCNetworkMiddleware
{
public:
    int totalRequests = 0;
    int successCount = 0;
    int errorCount = 0;

    void onRequestPreSend(QCNetworkRequest &request) override {
        Q_UNUSED(request);
        totalRequests++;
    }

    void onResponseReceived(QCNetworkReply *reply) override {
        if (reply->error() == NetworkError::NoError) {
            successCount++;
        } else {
            errorCount++;
        }
    }

    QString name() const override {
        return "StatisticsMiddleware";
    }

    void printStats() {
        qDebug() << "\n=== 请求统计 ===";
        qDebug() << "总请求数:" << totalRequests;
        qDebug() << "成功:" << successCount;
        qDebug() << "失败:" << errorCount;
        if (totalRequests > 0) {
            qDebug() << "成功率:" << QString::number((double)successCount / totalRequests * 100, 'f', 2) << "%";
        }
    }
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qDebug() << "=== QCurl Middleware 系统演示 ===\n";

    // ========================================================================
    // 示例 1: 使用内置中间件
    // ========================================================================

    qDebug() << ">>> 示例 1: 使用内置中间件";

    auto *manager1 = new QCNetworkAccessManager();
    auto *loggingMiddleware = new QCLoggingMiddleware();
    auto *errorMiddleware = new QCErrorHandlingMiddleware();

    manager1->addMiddleware(loggingMiddleware);
    manager1->addMiddleware(errorMiddleware);

    qDebug() << "已添加" << manager1->middlewares().size() << "个中间件:";
    for (auto *mw : manager1->middlewares()) {
        qDebug() << "  -" << mw->name();
    }

    // ========================================================================
    // 示例 2: 中间件链式执行
    // ========================================================================

    qDebug() << "\n>>> 示例 2: 中间件链式执行";

    auto *manager2 = new QCNetworkAccessManager();
    auto *timing = new TimingMiddleware();
    auto *logging = new QCLoggingMiddleware();

    manager2->addMiddleware(timing);
    manager2->addMiddleware(logging);

    qDebug() << "中间件执行顺序:";
    qDebug() << "  1." << timing->name();
    qDebug() << "  2." << logging->name();

    // ========================================================================
    // 示例 3: 自定义统计中间件
    // ========================================================================

    qDebug() << "\n>>> 示例 3: 自定义统计中间件";

    auto *manager3 = new QCNetworkAccessManager();
    auto *stats = new StatisticsMiddleware();

    manager3->addMiddleware(stats);

    // 模拟一些请求
    QCNetworkRequest req1(QUrl("http://example.com/1"));
    QCNetworkRequest req2(QUrl("http://example.com/2"));
    QCNetworkRequest req3(QUrl("http://example.com/3"));

    stats->onRequestPreSend(req1);
    stats->onRequestPreSend(req2);
    stats->onRequestPreSend(req3);

    // 模拟响应（不实际发送请求）
    qDebug() << "模拟了" << stats->totalRequests << "个请求";

    stats->printStats();

    // ========================================================================
    // 示例 4: 中间件管理
    // ========================================================================

    qDebug() << "\n>>> 示例 4: 中间件管理";

    auto *manager4 = new QCNetworkAccessManager();
    auto *m1 = new QCLoggingMiddleware();
    auto *m2 = new QCErrorHandlingMiddleware();

    manager4->addMiddleware(m1);
    manager4->addMiddleware(m2);
    qDebug() << "添加后中间件数量:" << manager4->middlewares().size();

    manager4->removeMiddleware(m1);
    qDebug() << "移除一个后中间件数量:" << manager4->middlewares().size();

    manager4->clearMiddlewares();
    qDebug() << "清空后中间件数量:" << manager4->middlewares().size();

    // ========================================================================
    // 示例 5: 真实网络请求 + 中间件拦截
    // ========================================================================

    qDebug() << "\n>>> 示例 5: 真实网络请求 + 中间件";

    auto *manager5 = new QCNetworkAccessManager();
    auto *timingMw5 = new TimingMiddleware();
    auto *statsMw5 = new StatisticsMiddleware();

    manager5->addMiddleware(timingMw5);
    manager5->addMiddleware(statsMw5);

    qDebug() << "已添加计时和统计中间件，发送3个真实请求...";

    // 发送3个真实请求
    QList<QCNetworkRequest> requests;
    requests.append(QCNetworkRequest(QUrl("https://httpbin.org/get")));
    requests.append(QCNetworkRequest(QUrl("https://httpbin.org/status/200")));
    requests.append(QCNetworkRequest(QUrl("https://httpbin.org/delay/1")));

    QEventLoop loop;
    int pendingRequests = requests.size();
    QList<QCNetworkReply*> activeReplies;

    for (const QCNetworkRequest &req : requests) {
        auto *r = manager5->sendGet(req);
        activeReplies.append(r);

        QObject::connect(r, &QCNetworkReply::finished, r, [r, &pendingRequests, &loop]() {
            pendingRequests--;
            qDebug() << "请求完成:" << r->url().toString();
            if (pendingRequests == 0) {
                loop.quit();
            }
        });
    }

    // 设置15秒总超时
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);

    loop.exec();

    if (pendingRequests == 0) {
        qDebug() << "\n✅ 所有请求完成!";
    } else {
        qDebug() << "\n⏱️ 部分请求超时，剩余:" << pendingRequests;
    }

    // 打印统计
    statsMw5->printStats();

    // 清理 replies
    for (auto *r : activeReplies) {
        r->deleteLater();
    }

    delete manager5;
    delete timingMw5;
    delete statsMw5;

    // ========================================================================
    // 清理
    // ========================================================================

    qDebug() << "\n=== 演示完成 ===";

    delete manager1;
    delete loggingMiddleware;
    delete errorMiddleware;

    delete manager2;
    delete timing;
    delete logging;

    delete manager3;
    delete stats;

    delete manager4;
    delete m1;
    delete m2;

    return 0;
}
