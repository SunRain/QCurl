// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#ifndef BENCHMARK_SCHEDULER_HELPERS_H
#define BENCHMARK_SCHEDULER_HELPERS_H

#include "../src/QCNetworkReply.h"
#include "../src/QCNetworkRequestPriority.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHash>
#include <QString>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <vector>

namespace SchedulerBenchmarkHelpers {

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
    double throughput = 0.0;
    std::vector<qint64> latencies;
};

struct PriorityBenchmarkContext {
    BenchmarkResult lowPriorityResult;
    BenchmarkResult highPriorityResult;
    QEventLoop loop;
    QElapsedTimer totalTimer;
    QHash<QCurl::QCNetworkReply *, QElapsedTimer> timers;
    QHash<QCurl::QCNetworkReply *, QCurl::QCNetworkRequestPriority> priorities;
    std::vector<QCurl::QCNetworkRequestPriority> startOrder;
    std::vector<QCurl::QCNetworkRequestPriority> finishOrder;
    int completedCount = 0;
    int totalCount = 0;
    bool finalized = false;

    explicit PriorityBenchmarkContext(int total)
        : totalCount(total)
    {
        lowPriorityResult.testName = QStringLiteral("低优先级请求");
        highPriorityResult.testName = QStringLiteral("高优先级请求");
    }
};

inline QUrl mockUrl(const QString &scope, int index)
{
    return QUrl(QStringLiteral("mock://scheduler/%1/%2").arg(scope).arg(index));
}

inline void calculateStatistics(BenchmarkResult &result)
{
    if (result.latencies.empty()) {
        return;
    }

    std::sort(result.latencies.begin(), result.latencies.end());

    qint64 sum = 0;
    for (auto latency : result.latencies) {
        sum += latency;
    }
    result.avgLatencyMs = static_cast<double>(sum) / result.latencies.size();

    const size_t p50Index = result.latencies.size() * 50 / 100;
    const size_t p95Index = result.latencies.size() * 95 / 100;
    const size_t p99Index = result.latencies.size() * 99 / 100;

    result.p50LatencyMs = result.latencies[p50Index];
    result.p95LatencyMs = result.latencies[p95Index];
    result.p99LatencyMs = result.latencies[p99Index];

    if (result.totalTimeMs > 0) {
        result.throughput = static_cast<double>(result.totalRequests) / result.totalTimeMs * 1000.0;
    }
}

inline void printResult(const BenchmarkResult &result)
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

inline void printComparison(const BenchmarkResult &baseline, const BenchmarkResult &test)
{
    qInfo() << "\n性能对比：";
    printResult(baseline);
    printResult(test);

    qInfo() << "\n对比分析：";
    if (baseline.avgLatencyMs > 0) {
        const double latencyDiff =
            ((test.avgLatencyMs - baseline.avgLatencyMs) / baseline.avgLatencyMs) * 100;
        qInfo() << QString("  延迟变化: %1%2%")
                       .arg(latencyDiff > 0 ? "+" : "")
                       .arg(latencyDiff, 0, 'f', 1);
    }
    if (baseline.throughput > 0) {
        const double throughputDiff =
            ((test.throughput - baseline.throughput) / baseline.throughput) * 100;
        qInfo() << QString("  吞吐量变化: %1%2%")
                       .arg(throughputDiff > 0 ? "+" : "")
                       .arg(throughputDiff, 0, 'f', 1);
    }
}

inline void printSchedulerSummary(const std::vector<BenchmarkResult> &results)
{
    qInfo() << "测试组数:" << results.size();

    if (results.size() < 2) {
        return;
    }

    if (results[0].avgLatencyMs > 0 && results[1].avgLatencyMs > 0) {
        const double improvement =
            (results[0].avgLatencyMs - results[1].avgLatencyMs) / results[0].avgLatencyMs * 100;
        qInfo() << QString("基本延迟：调度器%1 %2%")
                       .arg(improvement > 0 ? "降低" : "增加")
                       .arg(qAbs(improvement), 0, 'f', 1);
    }

    if (results.size() >= 4 && results[2].throughput > 0 && results[3].throughput > 0) {
        const double improvement =
            (results[3].throughput - results[2].throughput) / results[2].throughput * 100;
        qInfo() << QString("并发吞吐量：调度器%1 %2%")
                       .arg(improvement > 0 ? "提升" : "降低")
                       .arg(qAbs(improvement), 0, 'f', 1);
    }
}

} // namespace SchedulerBenchmarkHelpers

#endif // BENCHMARK_SCHEDULER_HELPERS_H
