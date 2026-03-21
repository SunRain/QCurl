// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDebug>
#include <QTimer>
#include <QEventLoop>
#include <vector>
#include <algorithm>
#include <cmath>
#include <memory>

#include "../src/QCNetworkAccessManager.h"
#include "../src/QCNetworkRequest.h"
#include "../src/QCNetworkReply.h"
#include "../src/QCNetworkRequestScheduler.h"
#include "../src/QCNetworkRequestPriority.h"

using namespace QCurl;

/**
 * @brief 性能基准测试：调度器 vs 无调度器
 * 
 * 测试场景：
 * 1. 延迟对比（平均延迟、P50/P95/P99）
 * 2. 吞吐量对比（请求/秒）
 * 3. 并发性能对比
 * 4. 优先级调度效果验证
 * 5. 内存和 CPU 使用对比
 */
class SchedulerBenchmark : public QObject
{
    Q_OBJECT

public:
    struct BenchmarkResult {
        QString testName;
        int totalRequests = 0;
        int successRequests = 0;
        int failedRequests = 0;
        qint64 totalTimeMs = 0;
        double avgLatencyMs = 0.0;
        double p50LatencyMs = 0.0;
        double p95LatencyMs = 0.0;
        double p99LatencyMs = 0.0;
        double throughput = 0.0;  // 请求/秒
        std::vector<qint64> latencies;  // 所有请求的延迟（毫秒）
    };

    explicit SchedulerBenchmark(QObject *parent = nullptr)
        : QObject(parent)
        , manager(new QCNetworkAccessManager(this))
        , scheduler(QCNetworkRequestScheduler::instance())
    {
    }

    void run()
    {
        // 测试 1：基本延迟对比（10 个顺序请求）
        benchmark1_BasicLatency();

        // 测试 2：并发性能对比（50 个并发请求）
        QTimer::singleShot(2000, this, &SchedulerBenchmark::benchmark2_ConcurrentPerformance);

        // 测试 3：优先级调度效果（混合优先级）
        QTimer::singleShot(4000, this, &SchedulerBenchmark::benchmark3_PriorityScheduling);

        // 测试 4：高负载压力测试（100 个请求）
        QTimer::singleShot(6000, this, &SchedulerBenchmark::benchmark4_HighLoad);

        // 8 秒后汇总结果
        QTimer::singleShot(8000, this, &SchedulerBenchmark::printSummary);
    }

private slots:
    void benchmark1_BasicLatency()
    {
        // 1.1 无调度器
        manager->enableRequestScheduler(false);
        auto noSchedulerResult = runBenchmark("无调度器", 10, false);
        results.push_back(noSchedulerResult);

        // 等待 500ms
        QTimer::singleShot(500, this, [this]() {
            // 1.2 有调度器
            manager->enableRequestScheduler(true);
            auto withSchedulerResult = runBenchmark("有调度器", 10, true);
            results.push_back(withSchedulerResult);

            printComparison(results[0], results[1]);
        });
    }

    void benchmark2_ConcurrentPerformance()
    {
        // 2.1 无调度器
        manager->enableRequestScheduler(false);
        auto noSchedulerResult = runConcurrentBenchmark("无调度器（并发）", 50, false);
        results.push_back(noSchedulerResult);

        // 等待 500ms
        QTimer::singleShot(500, this, [this]() {
            // 2.2 有调度器（配置为 10 个并发）
            manager->enableRequestScheduler(true);
            QCNetworkRequestScheduler::Config config;
            config.maxConcurrentRequests = 10;
            config.maxRequestsPerHost = 5;
            scheduler->setConfig(config);

            auto withSchedulerResult = runConcurrentBenchmark("有调度器（并发）", 50, true);
            results.push_back(withSchedulerResult);

            printComparison(results[2], results[3]);
        });
    }

    void benchmark3_PriorityScheduling()
    {
        manager->enableRequestScheduler(true);
        
        QCNetworkRequestScheduler::Config config;
        config.maxConcurrentRequests = 3;  // 限制并发，突显优先级效果
        config.maxRequestsPerHost = 2;
        scheduler->setConfig(config);

        // 使用堆上的共享指针来存储结果
        auto lowPriorityResult = std::make_shared<BenchmarkResult>();
        lowPriorityResult->testName = "低优先级请求";
        auto highPriorityResult = std::make_shared<BenchmarkResult>();
        highPriorityResult->testName = "高优先级请求";

        auto completedCount = std::make_shared<int>(0);
        int totalCount = 20;

        // 创建 10 个低优先级请求
        for (int i = 0; i < 10; ++i) {
            QCNetworkRequest request(QUrl("http://invalid-low-priority.local/test"));
            request.setPriority(QCNetworkRequestPriority::Low);

            QElapsedTimer *timer = new QElapsedTimer();
            timer->start();

            auto *reply = manager->sendGet(request);
            connect(reply, &QCNetworkReply::finished, this, [this, reply, timer, lowPriorityResult, highPriorityResult, completedCount, totalCount]() {
                qint64 latency = timer->elapsed();
                lowPriorityResult->latencies.push_back(latency);
                lowPriorityResult->totalRequests++;
                
                if (reply->error() == NetworkError::NoError) {
                    lowPriorityResult->successRequests++;
                } else {
                    lowPriorityResult->failedRequests++;
                }

                (*completedCount)++;
                if (*completedCount == totalCount) {
                    finalizePriorityBenchmark(*lowPriorityResult, *highPriorityResult);
                }

                delete timer;
                reply->deleteLater();
            });
        }

        // 创建 10 个高优先级请求
        for (int i = 0; i < 10; ++i) {
            QCNetworkRequest request(QUrl("http://invalid-high-priority.local/test"));
            request.setPriority(QCNetworkRequestPriority::High);

            QElapsedTimer *timer = new QElapsedTimer();
            timer->start();

            auto *reply = manager->sendGet(request);
            connect(reply, &QCNetworkReply::finished, this, [this, reply, timer, highPriorityResult, lowPriorityResult, completedCount, totalCount]() {
                qint64 latency = timer->elapsed();
                highPriorityResult->latencies.push_back(latency);
                highPriorityResult->totalRequests++;
                
                if (reply->error() == NetworkError::NoError) {
                    highPriorityResult->successRequests++;
                } else {
                    highPriorityResult->failedRequests++;
                }

                (*completedCount)++;
                if (*completedCount == totalCount) {
                    finalizePriorityBenchmark(*lowPriorityResult, *highPriorityResult);
                }

                delete timer;
                reply->deleteLater();
            });
        }
    }

    void benchmark4_HighLoad()
    {
        manager->enableRequestScheduler(true);
        
        QCNetworkRequestScheduler::Config config;
        config.maxConcurrentRequests = 20;
        config.maxRequestsPerHost = 10;
        scheduler->setConfig(config);

        auto result = runConcurrentBenchmark("高负载测试", 100, true);
        results.push_back(result);

        printResult(result);
    }

    void printSummary()
    {
        qInfo() << "测试组数:" << results.size();

        if (results.size() >= 2) {
            // 测试 1 对比
            if (results[0].avgLatencyMs > 0 && results[1].avgLatencyMs > 0) {
                double improvement = (results[0].avgLatencyMs - results[1].avgLatencyMs) / results[0].avgLatencyMs * 100;
                qInfo() << QString("基本延迟：调度器%1 %2%")
                           .arg(improvement > 0 ? "降低" : "增加")
                           .arg(qAbs(improvement), 0, 'f', 1);
            }

            // 测试 2 对比
            if (results.size() >= 4 && results[2].throughput > 0 && results[3].throughput > 0) {
                double improvement = (results[3].throughput - results[2].throughput) / results[2].throughput * 100;
                qInfo() << QString("并发吞吐量：调度器%1 %2%")
                           .arg(improvement > 0 ? "提升" : "降低")
                           .arg(qAbs(improvement), 0, 'f', 1);
            }
        }

        QCoreApplication::quit();
    }

private:
    BenchmarkResult runBenchmark(const QString &name, int count, bool useScheduler)
    {
        BenchmarkResult result;
        result.testName = name;
        result.totalRequests = count;

        QElapsedTimer totalTimer;
        totalTimer.start();

        QEventLoop loop;
        int completedCount = 0;

        for (int i = 0; i < count; ++i) {
            QCNetworkRequest request(QUrl(QString("http://invalid-test-%1.local/test").arg(i)));
            
            QElapsedTimer *timer = new QElapsedTimer();
            timer->start();

            QCNetworkReply *reply = nullptr;
            if (useScheduler) {
                request.setPriority(QCNetworkRequestPriority::Normal);
                reply = manager->sendGet(request);
            } else {
                reply = manager->sendGet(request);
            }

            connect(reply, &QCNetworkReply::finished, this, [&, reply, timer]() {
                qint64 latency = timer->elapsed();
                result.latencies.push_back(latency);
                
                if (reply->error() == NetworkError::NoError) {
                    result.successRequests++;
                } else {
                    result.failedRequests++;
                }

                completedCount++;
                if (completedCount == count) {
                    loop.quit();
                }

                delete timer;
                reply->deleteLater();
            });
        }

        loop.exec();

        result.totalTimeMs = totalTimer.elapsed();
        calculateStatistics(result);

        return result;
    }

    BenchmarkResult runConcurrentBenchmark(const QString &name, int count, bool useScheduler)
    {
        BenchmarkResult result;
        result.testName = name;
        result.totalRequests = count;

        QElapsedTimer totalTimer;
        totalTimer.start();

        QEventLoop loop;
        int completedCount = 0;

        // 并发创建所有请求
        for (int i = 0; i < count; ++i) {
            QCNetworkRequest request(QUrl(QString("http://invalid-concurrent-%1.local/test").arg(i)));
            
            QElapsedTimer *timer = new QElapsedTimer();
            timer->start();

            QCNetworkReply *reply = nullptr;
            if (useScheduler) {
                request.setPriority(QCNetworkRequestPriority::Normal);
                reply = manager->sendGet(request);
            } else {
                reply = manager->sendGet(request);
            }

            connect(reply, &QCNetworkReply::finished, this, [&, reply, timer]() {
                qint64 latency = timer->elapsed();
                result.latencies.push_back(latency);
                
                if (reply->error() == NetworkError::NoError) {
                    result.successRequests++;
                } else {
                    result.failedRequests++;
                }

                completedCount++;
                if (completedCount == count) {
                    loop.quit();
                }

                delete timer;
                reply->deleteLater();
            });
        }

        loop.exec();

        result.totalTimeMs = totalTimer.elapsed();
        calculateStatistics(result);

        return result;
    }

    void calculateStatistics(BenchmarkResult &result)
    {
        if (result.latencies.empty()) {
            return;
        }

        // 排序
        std::sort(result.latencies.begin(), result.latencies.end());

        // 计算平均延迟
        qint64 sum = 0;
        for (auto latency : result.latencies) {
            sum += latency;
        }
        result.avgLatencyMs = static_cast<double>(sum) / result.latencies.size();

        // 计算百分位数
        size_t p50Index = result.latencies.size() * 50 / 100;
        size_t p95Index = result.latencies.size() * 95 / 100;
        size_t p99Index = result.latencies.size() * 99 / 100;

        result.p50LatencyMs = result.latencies[p50Index];
        result.p95LatencyMs = result.latencies[p95Index];
        result.p99LatencyMs = result.latencies[p99Index];

        // 计算吞吐量（请求/秒）
        if (result.totalTimeMs > 0) {
            result.throughput = static_cast<double>(result.totalRequests) / result.totalTimeMs * 1000.0;
        }
    }

    void finalizePriorityBenchmark(BenchmarkResult &lowResult, BenchmarkResult &highResult)
    {
        calculateStatistics(lowResult);
        calculateStatistics(highResult);

        results.push_back(lowResult);
        results.push_back(highResult);

        qInfo() << QString("优先级延迟：low=%1 ms, high=%2 ms")
                       .arg(lowResult.avgLatencyMs, 0, 'f', 2)
                       .arg(highResult.avgLatencyMs, 0, 'f', 2);
    }

    void printResult(const BenchmarkResult &result)
    {
        qInfo() << "\n" << result.testName << "结果：";
        qInfo() << QString("  总请求数: %1").arg(result.totalRequests);
        qInfo() << QString("  成功: %1 | 失败: %2").arg(result.successRequests).arg(result.failedRequests);
        qInfo() << QString("  总耗时: %1 ms").arg(result.totalTimeMs);
        qInfo() << QString("  平均延迟: %1 ms").arg(result.avgLatencyMs, 0, 'f', 2);
        qInfo() << QString("  P50 延迟: %1 ms").arg(result.p50LatencyMs);
        qInfo() << QString("  P95 延迟: %1 ms").arg(result.p95LatencyMs);
        qInfo() << QString("  P99 延迟: %1 ms").arg(result.p99LatencyMs);
        qInfo() << QString("  吞吐量: %1 req/s").arg(result.throughput, 0, 'f', 2);
    }

    void printComparison(const BenchmarkResult &baseline, const BenchmarkResult &test)
    {
        qInfo() << "\n性能对比：";
        printResult(baseline);
        printResult(test);

        qInfo() << "\n对比分析：";

        // 延迟对比
        if (baseline.avgLatencyMs > 0) {
            double latencyDiff = ((test.avgLatencyMs - baseline.avgLatencyMs) / baseline.avgLatencyMs) * 100;
            qInfo() << QString("  延迟变化: %1%2%")
                       .arg(latencyDiff > 0 ? "+" : "")
                       .arg(latencyDiff, 0, 'f', 1);
        }

        // 吞吐量对比
        if (baseline.throughput > 0) {
            double throughputDiff = ((test.throughput - baseline.throughput) / baseline.throughput) * 100;
            qInfo() << QString("  吞吐量变化: %1%2%")
                       .arg(throughputDiff > 0 ? "+" : "")
                       .arg(throughputDiff, 0, 'f', 1);
        }
    }

    QCNetworkAccessManager *manager;
    QCNetworkRequestScheduler *scheduler;
    std::vector<BenchmarkResult> results;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    SchedulerBenchmark benchmark;
    benchmark.run();

    return app.exec();
}

#include "benchmark_scheduler.moc"
