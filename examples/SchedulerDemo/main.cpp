// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "../../src/QCNetworkAccessManager.h"
#include "../../src/QCNetworkLaneKey.h"
#include "../../src/QCNetworkReply.h"
#include "../../src/QCNetworkRequest.h"
#include "../../src/QCNetworkRequestPriority.h"
#include "../../src/QCNetworkSchedulerPolicy.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QTimer>

using namespace QCurl;

/**
 * @brief SchedulerDemo - 请求优先级调度器演示程序
 *
 * `QCNetworkRequestScheduler` 属于 QCurl Core install surface；本示例按公开 contract 演示用法。
 *
 * 本程序演示 QCNetworkRequestScheduler 的核心功能：
 * 1. 不同优先级的请求执行顺序
 * 2. 并发控制（全局 + 每主机限制）
 * 3. 请求管理（defer/undefer/取消）
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
    {
        setupScheduler();
        setupSignals();
    }

    void run()
    {
        qInfo() << "\n========================================";
        qInfo() << "QCurl 请求优先级调度器演示程序";
        qInfo() << "========================================\n";

        // 四个演示串行排队，避免前一个 demo 遗留的 scheduler 状态污染后一个结论。
        // 演示 1：优先级排序
        demo1_PriorityOrdering();

        // 2 秒后演示 2：并发控制
        QTimer::singleShot(2000, this, &SchedulerDemo::demo2_ConcurrencyControl);

        // 4 秒后演示 3：暂停恢复
        QTimer::singleShot(4000, this, &SchedulerDemo::demo3_DeferUndefer);

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
        createRequest("https://httpbin.org/delay/1",
                      QCNetworkRequestPriority::VeryHigh,
                      "极高优先级");

        // Critical 会在 lane 内优先调度，但仍受硬上限约束
        createRequest("https://httpbin.org/delay/1",
                      QCNetworkRequestPriority::Critical,
                      "紧急优先级（受硬上限约束）");

        qInfo() << "提示：观察输出，Critical 会优先出队，但不会突破并发/每主机限制\n";
    }

    void demo2_ConcurrencyControl()
    {
        qInfo() << "\n--- 演示 2：并发控制 ---";
        qInfo() << "配置：maxConcurrentRequests=3, maxRequestsPerHost=2";
        qInfo() << "创建 5 个请求到同一主机，观察并发限制...\n";

        applySchedulerLimits(3, 2);

        // 创建多个请求到同一主机
        for (int i = 0; i < 5; ++i) {
            createRequest("https://httpbin.org/get",
                          QCNetworkRequestPriority::Normal,
                          QString("并发请求 #%1").arg(i + 1));
        }

        printStatistics();
    }

    void demo3_DeferUndefer()
    {
        qInfo() << "\n--- 演示 3：manager-level lane cancel ---";
        qInfo() << "先启动一个耗时请求占用并发槽位，再通过 cancelLaneRequests 取消同 lane 的 pending 请求...\n";

        // 先清空前两个 demo 遗留的 reply，确保此处观察到的都是 defer/undefer 合同本身。
        const QCNetworkLaneCancelResult cleanupResult = manager->cancelLaneRequests(
            QCNetworkLaneKey::defaultLane(),
            QCNetworkAccessManager::SchedulerCancelScope::PendingAndRunning);
        Q_UNUSED(cleanupResult);

        applySchedulerLimits(1, 1);

        // blocker：占用唯一并发槽位
        QCNetworkRequest blocker(QUrl("https://httpbin.org/delay/4"));
        blocker.setPriority(QCNetworkRequestPriority::Normal);
        auto *blockingReply = manager->get(blocker);
        connect(blockingReply, &QCNetworkReply::finished, this, [blockingReply]() {
            qInfo() << "✓ blocker 请求完成";
            blockingReply->deleteLater();
        });

        // 目标请求：在 blocker 运行期间保持 Pending
        QCNetworkRequest request(QUrl("https://httpbin.org/get"));
        request.setPriority(QCNetworkRequestPriority::High);

        auto *reply     = manager->get(request);
        pauseResumeDemo = reply;

        connect(reply, &QCNetworkReply::finished, this, [reply]() {
            qInfo() << "✓ 目标请求完成";
            reply->deleteLater();
        });

        QTimer::singleShot(300, this, [this]() {
            if (!pauseResumeDemo) {
                return;
            }
            qInfo() << "⏹️  cancelLaneRequests(default, PendingOnly)...";
            const QCNetworkLaneCancelResult cancelResult = manager->cancelLaneRequests(
                QCNetworkLaneKey::defaultLane(),
                QCNetworkAccessManager::SchedulerCancelScope::PendingOnly);
            qInfo() << "cancelled pending replies =" << cancelResult.cancelledRequests();
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
        const QCNetworkLaneCancelResult cleanupResult = manager->cancelLaneRequests(
            QCNetworkLaneKey::defaultLane(),
            QCNetworkAccessManager::SchedulerCancelScope::PendingAndRunning);
        Q_UNUSED(cleanupResult);

        QCoreApplication::quit();
    }

private:
    void setupScheduler()
    {
        // 启用调度器
        manager->enableRequestScheduler(true);

        applySchedulerLimits(3, 2);

        qInfo() << "✓ 调度器已启用";
        qInfo() << "  - 最大并发请求数:" << manager->schedulerPolicy().maxConcurrentRequests();
        qInfo() << "  - 每主机最大并发:" << manager->schedulerPolicy().maxRequestsPerHost();
    }

    void setupSignals()
    {
        qInfo() << "✓ 使用 manager-level schedulerStatistics() 观察调度状态";
    }

    void applySchedulerLimits(int maxConcurrentRequests, int maxRequestsPerHost)
    {
        QCNetworkSchedulerPolicy policy = manager->schedulerPolicy();
        policy.setMaxConcurrentRequests(maxConcurrentRequests);
        policy.setMaxRequestsPerHost(maxRequestsPerHost);
        policy.setMaxBandwidthBytesPerSec(0);
        policy.setThrottlingEnabled(false);
        const bool policyApplied = manager->setSchedulerPolicy(policy);
        Q_ASSERT(policyApplied);
    }

    QCNetworkReply *createRequest(const QString &url,
                                  QCNetworkRequestPriority priority,
                                  const QString &description)
    {
        QUrl requestUrl(url);
        QCNetworkRequest request(requestUrl);
        request.setPriority(priority);
        // 示例默认都走 default lane，便于聚焦优先级/并发合同本身。

        auto *reply = manager->get(request);

        // 设置属性用于追踪
        reply->setProperty("description", description);

        connect(reply, &QCNetworkReply::finished, this, [this, reply, description]() {
            if (reply->error() == NetworkError::NoError) {
                qInfo() << QString("✓ [%1] 完成").arg(description);
            } else {
                qWarning() << QString("✗ [%1] 失败: %2").arg(description).arg(reply->errorString());
            }
            reply->deleteLater();
        });

        return reply;
    }

    void printStatistics()
    {
        auto stats = manager->schedulerStatistics();

        qInfo() << "\n📊 当前统计信息:";
        qInfo() << "  ├─ 等待中:" << stats.pendingRequests() << "个";
        qInfo() << "  ├─ 执行中:" << stats.runningRequests() << "个";
        qInfo() << "  ├─ 已完成:" << stats.completedRequests() << "个";
        qInfo() << "  ├─ 已取消:" << stats.cancelledRequests() << "个";
        qInfo() << "  ├─ 总接收:" << stats.totalBytesReceived() << "字节";
        qInfo() << "  └─ 平均响应时间:" << QString::number(stats.avgResponseTime(), 'f', 2) << "ms\n";
    }

    QCNetworkAccessManager *manager;
    QCNetworkReply *pauseResumeDemo = nullptr;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qInfo() << "QCurl SchedulerDemo 1.0.0 Core / Stable";
    qInfo() << "Build time:" << __DATE__ << __TIME__;

    SchedulerDemo demo;
    demo.run();

    return app.exec();
}

#include "main.moc"
