// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include <QCoreApplication>
#include <QTimer>
#include <QDebug>
#include <QDateTime>

#include "../../src/QCNetworkAccessManager.h"
#include "../../src/QCNetworkRequest.h"
#include "../../src/QCNetworkReply.h"
#include "../../src/QCNetworkRequestScheduler.h"
#include "../../src/QCNetworkRequestPriority.h"

using namespace QCurl;

/**
 * @brief SchedulerDemo - 请求优先级调度器演示程序
 * 
 * 本程序演示 QCNetworkRequestScheduler 的核心功能：
 * 1. 不同优先级的请求执行顺序
 * 2. 并发控制（全局 + 每主机限制）
 * 3. 请求管理（暂停/恢复/取消）
 * 4. 带宽限制
 * 5. 实时统计信息
 * 
 * @note 本程序使用 httpbin.org 作为测试服务器
 */
class SchedulerDemo : public QObject
{
    Q_OBJECT

public:
    explicit SchedulerDemo(QObject *parent = nullptr)
        : QObject(parent)
        , manager(new QCNetworkAccessManager(this))
        , scheduler(QCNetworkRequestScheduler::instance())
    {
        setupScheduler();
        setupSignals();
    }

    void run()
    {
        qInfo() << "\n========================================";
        qInfo() << "QCurl 请求优先级调度器演示程序";
        qInfo() << "========================================\n";

        // 演示 1：优先级排序
        demo1_PriorityOrdering();

        // 2 秒后演示 2：并发控制
        QTimer::singleShot(2000, this, &SchedulerDemo::demo2_ConcurrencyControl);

        // 4 秒后演示 3：暂停恢复
        QTimer::singleShot(4000, this, &SchedulerDemo::demo3_PauseResume);

        // 6 秒后演示 4：统计信息
        QTimer::singleShot(6000, this, &SchedulerDemo::demo4_Statistics);

        // 8 秒后退出
        QTimer::singleShot(8000, this, &SchedulerDemo::finish);
    }

private slots:
    void demo1_PriorityOrdering()
    {
        qInfo() << "\n--- 演示 1：优先级排序 ---";
        qInfo() << "创建 5 个不同优先级的请求，观察执行顺序...\n";

        // 按相反的顺序创建（先 Low，后 Critical）
        createRequest("https://httpbin.org/delay/1", QCNetworkRequestPriority::Low, "低优先级");
        createRequest("https://httpbin.org/delay/1", QCNetworkRequestPriority::Normal, "普通优先级");
        createRequest("https://httpbin.org/delay/1", QCNetworkRequestPriority::High, "高优先级");
        createRequest("https://httpbin.org/delay/1", QCNetworkRequestPriority::VeryHigh, "极高优先级");
        
        // Critical 优先级应该跳过队列立即执行
        createRequest("https://httpbin.org/delay/1", QCNetworkRequestPriority::Critical, "紧急优先级（立即执行）");

        qInfo() << "提示：观察输出，Critical 会立即执行，其他按优先级从高到低执行\n";
    }

    void demo2_ConcurrencyControl()
    {
        qInfo() << "\n--- 演示 2：并发控制 ---";
        qInfo() << "配置：maxConcurrentRequests=3, maxRequestsPerHost=2";
        qInfo() << "创建 5 个请求到同一主机，观察并发限制...\n";

        // 修改配置
        QCNetworkRequestScheduler::Config config = scheduler->config();
        config.maxConcurrentRequests = 3;
        config.maxRequestsPerHost = 2;
        scheduler->setConfig(config);

        // 创建多个请求到同一主机
        for (int i = 0; i < 5; ++i) {
            createRequest("https://httpbin.org/get", 
                         QCNetworkRequestPriority::Normal, 
                         QString("并发请求 #%1").arg(i+1));
        }

        printStatistics();
    }

    void demo3_PauseResume()
    {
        qInfo() << "\n--- 演示 3：暂停/恢复 ---";
        qInfo() << "创建请求，2秒后暂停，再2秒后恢复...\n";

        QCNetworkRequest request(QUrl("https://httpbin.org/delay/2"));
        request.setPriority(QCNetworkRequestPriority::High);
        
        auto *reply = manager->scheduleGet(request);
        pauseResumeDemo = reply;

        connect(reply, &QCNetworkReply::finished, this, [this, reply]() {
            qInfo() << "✓ 暂停/恢复演示请求完成";
            reply->deleteLater();
        });

        // 2 秒后暂停
        QTimer::singleShot(2000, this, [this]() {
            if (pauseResumeDemo) {
                qInfo() << "⏸️  暂停请求...";
                scheduler->pauseRequest(pauseResumeDemo);
            }
        });

        // 4 秒后恢复
        QTimer::singleShot(4000, this, [this]() {
            if (pauseResumeDemo) {
                qInfo() << "▶️  恢复请求...";
                scheduler->resumeRequest(pauseResumeDemo);
            }
        });
    }

    void demo4_Statistics()
    {
        qInfo() << "\n--- 演示 4：实时统计信息 ---\n";
        printStatistics();
    }

    void finish()
    {
        qInfo() << "\n========================================";
        qInfo() << "演示完成！";
        qInfo() << "========================================\n";

        // 最终统计
        printStatistics();

        // 清理所有请求
        scheduler->cancelAllRequests();

        QCoreApplication::quit();
    }

private:
    void setupScheduler()
    {
        // 启用调度器
        manager->enableRequestScheduler(true);

        // 配置调度器
        QCNetworkRequestScheduler::Config config;
        config.maxConcurrentRequests = 3;
        config.maxRequestsPerHost = 2;
        config.maxBandwidthBytesPerSec = 0;  // 无带宽限制
        config.enableThrottling = false;

        scheduler->setConfig(config);

        qInfo() << "✓ 调度器已启用";
        qInfo() << "  - 最大并发请求数:" << config.maxConcurrentRequests;
        qInfo() << "  - 每主机最大并发:" << config.maxRequestsPerHost;
    }

    void setupSignals()
    {
        // 请求加入队列
        connect(scheduler, &QCNetworkRequestScheduler::requestQueued,
                this, [](QCNetworkReply *reply, QCNetworkRequestPriority priority) {
            Q_UNUSED(reply);
            qInfo() << QString("📥 [%1] 请求加入队列").arg(toString(priority));
        });

        // 请求开始执行
        connect(scheduler, &QCNetworkRequestScheduler::requestStarted,
                this, [](QCNetworkReply *reply) {
            qInfo() << QString("🚀 请求开始执行: %1").arg(reply->url().toString());
        });

        // 请求完成
        connect(scheduler, &QCNetworkRequestScheduler::requestFinished,
                this, [](QCNetworkReply *reply) {
            qInfo() << QString("✅ 请求完成: %1").arg(reply->url().toString());
        });

        // 队列已清空
        connect(scheduler, &QCNetworkRequestScheduler::queueEmpty,
                this, []() {
            qInfo() << "ℹ️  队列已清空";
        });
    }

    QCNetworkReply* createRequest(const QString &url, 
                                   QCNetworkRequestPriority priority,
                                   const QString &description)
    {
        QUrl requestUrl(url);
        QCNetworkRequest request(requestUrl);
        request.setPriority(priority);

        auto *reply = manager->scheduleGet(request);

        // 设置属性用于追踪
        reply->setProperty("description", description);

        connect(reply, &QCNetworkReply::finished, this, [this, reply, description]() {
            if (reply->error() == NetworkError::NoError) {
                qInfo() << QString("✓ [%1] 完成").arg(description);
            } else {
                qWarning() << QString("✗ [%1] 失败: %2")
                              .arg(description)
                              .arg(reply->errorString());
            }
            reply->deleteLater();
        });

        return reply;
    }

    void printStatistics()
    {
        auto stats = scheduler->statistics();

        qInfo() << "\n📊 当前统计信息:";
        qInfo() << "  ├─ 等待中:" << stats.pendingRequests << "个";
        qInfo() << "  ├─ 执行中:" << stats.runningRequests << "个";
        qInfo() << "  ├─ 已完成:" << stats.completedRequests << "个";
        qInfo() << "  ├─ 已取消:" << stats.cancelledRequests << "个";
        qInfo() << "  ├─ 总接收:" << stats.totalBytesReceived << "字节";
        qInfo() << "  └─ 平均响应时间:" << QString::number(stats.avgResponseTime, 'f', 2) << "ms\n";
    }

    QCNetworkAccessManager *manager;
    QCNetworkRequestScheduler *scheduler;
    QCNetworkReply *pauseResumeDemo = nullptr;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qInfo() << "QCurl SchedulerDemo v2.6.0";
    qInfo() << "Build time:" << __DATE__ << __TIME__;

    SchedulerDemo demo;
    demo.run();

    return app.exec();
}

#include "main.moc"
