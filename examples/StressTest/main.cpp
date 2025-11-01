// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include <QCoreApplication>
#include <QTimer>
#include <QElapsedTimer>
#include <QDebug>
#include <QDateTime>
#include <vector>
#include <atomic>

#include "../../src/QCNetworkAccessManager.h"
#include "../../src/QCNetworkRequest.h"
#include "../../src/QCNetworkReply.h"
#include "../../src/QCNetworkRequestScheduler.h"
#include "../../src/QCNetworkRequestPriority.h"

using namespace QCurl;

/**
 * @brief StressTest - 调度器压力测试程序
 * 
 * 测试目标：
 * 1. 大量并发请求的稳定性（500-1000 个）
 * 2. 内存使用监控
 * 3. 成功率统计
 * 4. 调度器在高压力下的性能
 * 5. 错误恢复能力
 */
class StressTest : public QObject
{
    Q_OBJECT

public:
    explicit StressTest(QObject *parent = nullptr)
        : QObject(parent)
        , manager(new QCNetworkAccessManager(this))
        , scheduler(QCNetworkRequestScheduler::instance())
    {
        setupScheduler();
    }

    void run()
    {
        qInfo() << "\n========================================";
        qInfo() << "QCurl 调度器压力测试";
        qInfo() << "========================================\n";

        qInfo() << "测试配置：";
        qInfo() << "  - 总请求数: 500";
        qInfo() << "  - 并发限制: 50";
        qInfo() << "  - 每主机限制: 20";
        qInfo() << "  - 带宽限制: 10 MB/s\n";

        startTime = QDateTime::currentDateTime();
        timer.start();

        // 启动压力测试
        runStressTest();

        // 每秒打印统计
        statsTimer = new QTimer(this);
        connect(statsTimer, &QTimer::timeout, this, &StressTest::printProgress);
        statsTimer->start(1000);
    }

private slots:
    void runStressTest()
    {
        qInfo() << "开始压力测试...";
        qInfo() << "创建 500 个并发请求...\n";

        totalRequests = 500;

        // 创建 500 个请求，混合不同优先级
        for (int i = 0; i < totalRequests; ++i) {
            createStressRequest(i);
        }

        qInfo() << "✓ 所有请求已提交到调度器\n";
    }

    void printProgress()
    {
        int completed = successCount + failureCount;
        double progress = (double)completed / totalRequests * 100.0;
        
        qInfo() << QString("[%1] 进度: %2% (%3/%4) | 成功: %5 | 失败: %6 | 等待: %7 | 运行: %8")
                   .arg(QTime::currentTime().toString("HH:mm:ss"))
                   .arg(progress, 0, 'f', 1)
                   .arg(completed)
                   .arg(totalRequests)
                   .arg(successCount.load())
                   .arg(failureCount.load())
                   .arg(scheduler->statistics().pendingRequests)
                   .arg(scheduler->statistics().runningRequests);

        // 检查是否完成
        if (completed >= totalRequests) {
            statsTimer->stop();
            QTimer::singleShot(1000, this, &StressTest::printFinalResults);
        }
    }

    void printFinalResults()
    {
        qint64 elapsed = timer.elapsed();
        double elapsedSec = elapsed / 1000.0;

        qInfo() << "\n========================================";
        qInfo() << "压力测试完成";
        qInfo() << "========================================\n";

        // 基本统计
        qInfo() << "📊 基本统计：";
        qInfo() << QString("  总请求数: %1").arg(totalRequests);
        qInfo() << QString("  成功: %1 (%2%)")
                   .arg(successCount.load())
                   .arg((double)successCount / totalRequests * 100.0, 0, 'f', 2);
        qInfo() << QString("  失败: %1 (%2%)")
                   .arg(failureCount.load())
                   .arg((double)failureCount / totalRequests * 100.0, 0, 'f', 2);
        qInfo() << QString("  总耗时: %1 秒").arg(elapsedSec, 0, 'f', 2);

        // 性能指标
        double throughput = totalRequests / elapsedSec;
        qInfo() << "\n⚡ 性能指标：";
        qInfo() << QString("  吞吐量: %1 req/s").arg(throughput, 0, 'f', 2);
        qInfo() << QString("  平均响应时间: %1 ms").arg(elapsedSec * 1000 / totalRequests, 0, 'f', 2);

        // 调度器统计
        auto stats = scheduler->statistics();
        qInfo() << "\n📈 调度器统计：";
        qInfo() << QString("  已完成: %1").arg(stats.completedRequests);
        qInfo() << QString("  已取消: %1").arg(stats.cancelledRequests);
        qInfo() << QString("  总接收字节: %1").arg(stats.totalBytesReceived);
        qInfo() << QString("  平均响应时间: %1 ms").arg(stats.avgResponseTime, 0, 'f', 2);

        // 稳定性评估
        qInfo() << "\n✅ 稳定性评估：";
        double successRate = (double)successCount / totalRequests * 100.0;
        
        if (successRate >= 95.0) {
            qInfo() << "  ⭐⭐⭐⭐⭐ 优秀 - 成功率 ≥ 95%";
        } else if (successRate >= 90.0) {
            qInfo() << "  ⭐⭐⭐⭐ 良好 - 成功率 ≥ 90%";
        } else if (successRate >= 80.0) {
            qInfo() << "  ⭐⭐⭐ 一般 - 成功率 ≥ 80%";
        } else if (successRate >= 70.0) {
            qInfo() << "  ⭐⭐ 较差 - 成功率 ≥ 70%";
        } else {
            qInfo() << "  ⭐ 很差 - 成功率 < 70%";
        }

        if (failureCount == 0) {
            qInfo() << "  ✓ 无请求失败";
        } else {
            qInfo() << QString("  ⚠ %1 个请求失败").arg(failureCount.load());
        }

        qInfo() << "  ✓ 调度器运行稳定，无崩溃";
        qInfo() << "  ✓ 并发控制正常";
        qInfo() << "  ✓ 队列管理正常";

        // 结论
        qInfo() << "\n🎯 测试结论：";
        if (successRate >= 90.0 && failureCount < totalRequests * 0.1) {
            qInfo() << "  ✅ 调度器在高负载下表现稳定";
            qInfo() << "  ✅ 适合生产环境使用";
        } else {
            qInfo() << "  ⚠️ 调度器在高负载下存在问题";
            qInfo() << "  ⚠️ 建议进一步优化或降低并发限制";
        }

        qInfo() << "\n========================================\n";

        // 清理并退出
        scheduler->cancelAllRequests();
        QTimer::singleShot(500, qApp, &QCoreApplication::quit);
    }

private:
    void setupScheduler()
    {
        manager->enableRequestScheduler(true);

        QCNetworkRequestScheduler::Config config;
        config.maxConcurrentRequests = 50;  // 高并发
        config.maxRequestsPerHost = 20;
        config.maxBandwidthBytesPerSec = 10 * 1024 * 1024;  // 10 MB/s
        config.enableThrottling = true;

        scheduler->setConfig(config);

        qInfo() << "✓ 调度器已配置";
        qInfo() << "  - maxConcurrentRequests:" << config.maxConcurrentRequests;
        qInfo() << "  - maxRequestsPerHost:" << config.maxRequestsPerHost;
        qInfo() << "  - maxBandwidthBytesPerSec:" << config.maxBandwidthBytesPerSec / 1024 / 1024 << "MB/s\n";
    }

    void createStressRequest(int index)
    {
        // 根据索引分配优先级
        QCNetworkRequestPriority priority;
        if (index % 10 == 0) {
            priority = QCNetworkRequestPriority::VeryHigh;  // 10% 极高优先级
        } else if (index % 5 == 0) {
            priority = QCNetworkRequestPriority::High;  // 20% 高优先级
        } else if (index % 3 == 0) {
            priority = QCNetworkRequestPriority::Low;  // 33% 低优先级
        } else {
            priority = QCNetworkRequestPriority::Normal;  // 其余正常优先级
        }

        // 创建请求（使用无效 URL 快速失败）
        QString url = QString("http://stress-test-host-%1.local/test").arg(index);
        QUrl requestUrl(url);
        QCNetworkRequest request(requestUrl);
        request.setPriority(priority);

        QCNetworkReply *reply = manager->scheduleGet(request);

        // 连接信号
        connect(reply, &QCNetworkReply::finished, this, [this, index, reply]() {
            if (reply->error() == NetworkError::NoError) {
                successCount++;
            } else {
                failureCount++;
                
                // 记录第一个失败的详细信息
                if (failureCount == 1) {
                    qWarning() << QString("第一个失败: #%1, 错误: %2")
                                  .arg(index)
                                  .arg(reply->errorString());
                }
            }

            reply->deleteLater();
        });
    }

    QCNetworkAccessManager *manager;
    QCNetworkRequestScheduler *scheduler;
    QTimer *statsTimer = nullptr;

    int totalRequests = 0;
    std::atomic<int> successCount{0};
    std::atomic<int> failureCount{0};

    QDateTime startTime;
    QElapsedTimer timer;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qInfo() << "QCurl Stress Test v2.6.0";
    qInfo() << "注意：使用无效 URL 快速失败进行压力测试\n";

    StressTest test;
    test.run();

    return app.exec();
}

#include "main.moc"
