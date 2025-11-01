// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include <QTest>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QDebug>

#include "QCNetworkAccessManager.h"
#include "QCNetworkRequest.h"
#include "QCNetworkReply.h"
#include "QCNetworkConnectionPoolManager.h"
#include "QCNetworkConnectionPoolConfig.h"

using namespace QCurl;

/**
 * @brief HTTP 连接池性能基准测试
 * 
 * 测试连接池对性能的影响。
 * 
 * @note 需要网络连接才能运行
 */
class BenchmarkConnectionPool : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    
    // 基准测试
    void benchmarkDefaultConfig();
    void benchmarkConservativeConfig();
    void benchmarkAggressiveConfig();
    
private:
    void runRequests(int count, const QString &testName);
    QCNetworkAccessManager *manager = nullptr;
};

void BenchmarkConnectionPool::initTestCase()
{
    manager = new QCNetworkAccessManager(this);
    
    // 测试网络连接
    QUrl testUrl("https://httpbin.org/get");
    QCNetworkRequest testRequest(testUrl);
    testRequest.setFollowLocation(true);
    
    auto *testReply = manager->sendGet(testRequest);
    QSignalSpy testSpy(testReply, &QCNetworkReply::finished);
    
    if (!testSpy.wait(10000)) {
        testReply->deleteLater();
        QSKIP("Network not available, skipping benchmark tests");
        return;
    }
    
    testReply->deleteLater();
    
    qDebug() << "";
    qDebug() << "========================================";
    qDebug() << "HTTP Connection Pool Benchmark";
    qDebug() << "========================================";
    qDebug() << "Testing connection pool performance...";
    qDebug() << "";
}

void BenchmarkConnectionPool::cleanupTestCase()
{
    delete manager;
    manager = nullptr;
    
    qDebug() << "";
    qDebug() << "========================================";
    qDebug() << "Benchmark Complete";
    qDebug() << "========================================";
}

void BenchmarkConnectionPool::runRequests(int count, const QString &testName)
{
    auto *poolManager = QCNetworkConnectionPoolManager::instance();
    poolManager->resetStatistics();
    
    QUrl url("https://httpbin.org/get");
    
    QElapsedTimer timer;
    timer.start();
    
    qint64 totalBytes = 0;
    int successCount = 0;
    
    for (int i = 0; i < count; ++i) {
        QCNetworkRequest request(url);
        request.setFollowLocation(true);
        auto *reply = manager->sendGet(request);
        
        QSignalSpy spy(reply, &QCNetworkReply::finished);
        if (spy.wait(30000)) {
            auto data = reply->readAll();
            if (data.has_value()) {
                totalBytes += data->size();
            }
            successCount++;
        }
        
        reply->deleteLater();
    }
    
    qint64 elapsed = timer.elapsed();
    auto stats = poolManager->statistics();
    
    qDebug() << "";
    qDebug() << "Test:" << testName;
    qDebug() << "  Requests:" << count << "(" << successCount << "successful)";
    qDebug() << "  Total time:" << elapsed << "ms";
    qDebug() << "  Avg per request:" << (successCount > 0 ? elapsed / successCount : 0) << "ms";
    qDebug() << "  Total data:" << totalBytes << "bytes";
    qDebug() << "  Throughput:" << (elapsed > 0 ? (totalBytes * 1000 / elapsed) : 0) << "bytes/s";
    qDebug() << "  Connection stats:";
    qDebug() << "    - Total requests:" << stats.totalRequests;
    qDebug() << "    - Active connections:" << stats.activeConnections;
    qDebug() << "    - Idle connections:" << stats.idleConnections;
}

void BenchmarkConnectionPool::benchmarkDefaultConfig()
{
    // 使用默认配置
    auto *poolManager = QCNetworkConnectionPoolManager::instance();
    poolManager->setConfig(QCNetworkConnectionPoolConfig());
    
    qDebug() << "";
    qDebug() << "----------------------------------------";
    qDebug() << "Benchmark: Default Configuration";
    qDebug() << "----------------------------------------";
    
    auto config = poolManager->config();
    qDebug() << "Config:";
    qDebug() << "  - maxConnectionsPerHost:" << config.maxConnectionsPerHost;
    qDebug() << "  - maxTotalConnections:" << config.maxTotalConnections;
    qDebug() << "  - HTTP/2 multiplexing:" << (config.enableMultiplexing ? "enabled" : "disabled");
    
    QBENCHMARK {
        runRequests(5, "Default Config");
    }
}

void BenchmarkConnectionPool::benchmarkConservativeConfig()
{
    // 使用保守配置
    auto *poolManager = QCNetworkConnectionPoolManager::instance();
    poolManager->setConfig(QCNetworkConnectionPoolConfig::conservative());
    
    qDebug() << "";
    qDebug() << "----------------------------------------";
    qDebug() << "Benchmark: Conservative Configuration";
    qDebug() << "----------------------------------------";
    
    auto config = poolManager->config();
    qDebug() << "Config:";
    qDebug() << "  - maxConnectionsPerHost:" << config.maxConnectionsPerHost;
    qDebug() << "  - maxTotalConnections:" << config.maxTotalConnections;
    qDebug() << "  - HTTP/2 multiplexing:" << (config.enableMultiplexing ? "enabled" : "disabled");
    
    QBENCHMARK {
        runRequests(5, "Conservative Config");
    }
}

void BenchmarkConnectionPool::benchmarkAggressiveConfig()
{
    // 使用激进配置
    auto *poolManager = QCNetworkConnectionPoolManager::instance();
    poolManager->setConfig(QCNetworkConnectionPoolConfig::aggressive());
    
    qDebug() << "";
    qDebug() << "----------------------------------------";
    qDebug() << "Benchmark: Aggressive Configuration";
    qDebug() << "----------------------------------------";
    
    auto config = poolManager->config();
    qDebug() << "Config:";
    qDebug() << "  - maxConnectionsPerHost:" << config.maxConnectionsPerHost;
    qDebug() << "  - maxTotalConnections:" << config.maxTotalConnections;
    qDebug() << "  - HTTP/2 multiplexing:" << (config.enableMultiplexing ? "enabled" : "disabled");
    
    QBENCHMARK {
        runRequests(5, "Aggressive Config");
    }
}

QTEST_MAIN(BenchmarkConnectionPool)
#include "benchmark_connectionpool.moc"
