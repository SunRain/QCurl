// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDebug>
#include <QTimer>
#include <QEventLoop>

#include "../src/QCNetworkAccessManager.h"
#include "../src/QCNetworkHttpMethod.h"
#include "../src/QCNetworkMockHandler.h"
#include "../src/QCNetworkRequest.h"
#include "../src/QCNetworkReply.h"
#include "../src/QCNetworkSchedulerPolicy.h"
#include "../src/QCNetworkRequestPriority.h"
#include "../src/QCNetworkTestSupport.h"
#include "benchmark_scheduler_helpers.h"

using namespace QCurl;
using namespace SchedulerBenchmarkHelpers;

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
    explicit SchedulerBenchmark(QObject *parent = nullptr)
        : QObject(parent)
        , manager(new QCNetworkAccessManager(this))
    {
        TestSupport::setMockHandler(manager, &mockHandler);
    }

    ~SchedulerBenchmark() override
    {
        delete manager;
        manager = nullptr;
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

            SchedulerBenchmarkHelpers::printComparison(results[0], results[1]);
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
            applySchedulerLimits(10, 5);

            auto withSchedulerResult = runConcurrentBenchmark("有调度器（并发）", 50, true);
            results.push_back(withSchedulerResult);

            SchedulerBenchmarkHelpers::printComparison(results[2], results[3]);
        });
    }

    void benchmark3_PriorityScheduling()
    {
        manager->enableRequestScheduler(true);
        mockHandler.clear();
        configurePriorityScheduler();

        PriorityBenchmarkContext context(20);
        context.totalTimer.start();
        const QMetaObject::Connection startedConnection = trackPriorityStarts(context);

        enqueuePriorityRequests(context,
                                 QStringLiteral("low"),
                                QCNetworkLaneKey::background(),
                                QCNetworkRequestPriority::Low,
                                &context.lowPriorityResult);
        enqueuePriorityRequests(context,
                                QStringLiteral("high"),
                                QCNetworkLaneKey::control(),
                                QCNetworkRequestPriority::High,
                                &context.highPriorityResult);

        context.loop.exec();
        disconnect(startedConnection);
    }

    void benchmark4_HighLoad()
    {
        manager->enableRequestScheduler(true);
        applySchedulerLimits(20, 10);

        auto result = runConcurrentBenchmark("高负载测试", 100, true);
        results.push_back(result);

        SchedulerBenchmarkHelpers::printResult(result);
    }

    void printSummary()
    {
        SchedulerBenchmarkHelpers::printSchedulerSummary(results);
        QCoreApplication::quit();
    }

private:
    void configurePriorityScheduler()
    {
        applySchedulerLimits(3, 2);
    }

    void applySchedulerLimits(int maxConcurrentRequests, int maxRequestsPerHost)
    {
        QCNetworkSchedulerPolicy policy = manager->schedulerPolicy();
        policy.setMaxConcurrentRequests(maxConcurrentRequests);
        policy.setMaxRequestsPerHost(maxRequestsPerHost);
        const bool policyApplied = manager->setSchedulerPolicy(policy);
        Q_ASSERT(policyApplied);
    }

    void prepareMockResponse(const QUrl &url, int delayMs = 1)
    {
        mockHandler.setGlobalDelay(delayMs);
        mockHandler.mockResponse(HttpMethod::Get, url, QByteArrayLiteral("ok"), 200);
    }

    QMetaObject::Connection trackPriorityStarts(PriorityBenchmarkContext &context)
    {
        Q_UNUSED(context);
        return {};
    }

    void enqueuePriorityRequests(PriorityBenchmarkContext &context,
                                 const QString &scope,
                                 const QCNetworkLaneKey &lane,
                                 QCNetworkRequestPriority priority,
                                BenchmarkResult *result)
    {
        for (int i = 0; i < 10; ++i) {
            const QUrl url = SchedulerBenchmarkHelpers::mockUrl(scope, i);
            prepareMockResponse(url, 2);

            QCNetworkRequest request(url);
            request.setPriority(priority);
            request.setLane(lane);

            QElapsedTimer timer;
            timer.start();
            auto *reply = manager->get(request);
            context.timers.insert(reply, timer);
            context.priorities.insert(reply, priority);
            connect(reply, &QCNetworkReply::finished, this, [this, reply, &context, result]() {
                recordPriorityReply(context, reply, *result);
            });
        }
    }

    void recordPriorityReply(PriorityBenchmarkContext &context,
                             QCNetworkReply *reply,
                             BenchmarkResult &result)
    {
        const QElapsedTimer timer = context.timers.take(reply);
        result.latencies.push_back(timer.elapsed());
        result.totalRequests++;

        if (reply->error() == NetworkError::NoError && reply->httpStatusCode() == 200) {
            result.successRequests++;
        } else {
            result.failedRequests++;
        }
        context.finishOrder.push_back(context.priorities.value(reply));

        context.completedCount++;
        if (context.completedCount == context.totalCount) {
            finalizePriorityBenchmark(context);
        }

        reply->deleteLater();
    }

    BenchmarkResult runBenchmark(const QString &name, int count, bool useScheduler)
    {
        mockHandler.clear();
        BenchmarkResult result;
        result.testName = name;
        result.totalRequests = count;

        QElapsedTimer totalTimer;
        totalTimer.start();

        QEventLoop loop;
        int completedCount = 0;

        for (int i = 0; i < count; ++i) {
            const QUrl url = SchedulerBenchmarkHelpers::mockUrl(QStringLiteral("basic"), i);
            prepareMockResponse(url);
            QCNetworkRequest request(url);

            QElapsedTimer timer;
            timer.start();

            QCNetworkReply *reply = nullptr;
            if (useScheduler) {
                request.setPriority(QCNetworkRequestPriority::Normal);
                reply = manager->get(request);
            } else {
                reply = manager->get(request);
            }

            connect(reply, &QCNetworkReply::finished, this, [&, reply, timer]() {
                qint64 latency = timer.elapsed();
                result.latencies.push_back(latency);
                
                if (reply->error() == NetworkError::NoError && reply->httpStatusCode() == 200) {
                    result.successRequests++;
                } else {
                    result.failedRequests++;
                }

                completedCount++;
                if (completedCount == count) {
                    loop.quit();
                }

                reply->deleteLater();
            });
        }

        loop.exec();

        result.totalTimeMs = totalTimer.elapsed();
        SchedulerBenchmarkHelpers::calculateStatistics(result);

        return result;
    }

    BenchmarkResult runConcurrentBenchmark(const QString &name, int count, bool useScheduler)
    {
        mockHandler.clear();
        BenchmarkResult result;
        result.testName = name;
        result.totalRequests = count;

        QElapsedTimer totalTimer;
        totalTimer.start();

        QEventLoop loop;
        int completedCount = 0;

        // 并发创建所有请求
        for (int i = 0; i < count; ++i) {
            const QUrl url = SchedulerBenchmarkHelpers::mockUrl(QStringLiteral("concurrent"), i);
            prepareMockResponse(url);
            QCNetworkRequest request(url);

            QElapsedTimer timer;
            timer.start();

            QCNetworkReply *reply = nullptr;
            if (useScheduler) {
                request.setPriority(QCNetworkRequestPriority::Normal);
                reply = manager->get(request);
            } else {
                reply = manager->get(request);
            }

            connect(reply, &QCNetworkReply::finished, this, [&, reply, timer]() {
                qint64 latency = timer.elapsed();
                result.latencies.push_back(latency);
                
                if (reply->error() == NetworkError::NoError && reply->httpStatusCode() == 200) {
                    result.successRequests++;
                } else {
                    result.failedRequests++;
                }

                completedCount++;
                if (completedCount == count) {
                    loop.quit();
                }

                reply->deleteLater();
            });
        }

        loop.exec();

        result.totalTimeMs = totalTimer.elapsed();
        SchedulerBenchmarkHelpers::calculateStatistics(result);

        return result;
    }

    void finalizePriorityBenchmark(PriorityBenchmarkContext &context)
    {
        if (context.finalized) {
            return;
        }
        context.finalized = true;

        context.lowPriorityResult.totalTimeMs = context.totalTimer.elapsed();
        context.highPriorityResult.totalTimeMs = context.lowPriorityResult.totalTimeMs;
        SchedulerBenchmarkHelpers::calculateStatistics(context.lowPriorityResult);
        SchedulerBenchmarkHelpers::calculateStatistics(context.highPriorityResult);

        results.push_back(context.lowPriorityResult);
        results.push_back(context.highPriorityResult);

        qInfo() << QString("优先级延迟：low=%1 ms, high=%2 ms")
                       .arg(context.lowPriorityResult.avgLatencyMs, 0, 'f', 2)
                       .arg(context.highPriorityResult.avgLatencyMs, 0, 'f', 2);
        qInfo() << QString("优先级统计：low=%1/%2 high=%3/%4 starts=%5 finishes=%6")
                       .arg(context.lowPriorityResult.successRequests)
                       .arg(context.lowPriorityResult.totalRequests)
                       .arg(context.highPriorityResult.successRequests)
                       .arg(context.highPriorityResult.totalRequests)
                       .arg(static_cast<int>(context.startOrder.size()))
                       .arg(static_cast<int>(context.finishOrder.size()));
        context.loop.quit();
    }

    QCNetworkAccessManager *manager;
    QCNetworkMockHandler mockHandler;
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
