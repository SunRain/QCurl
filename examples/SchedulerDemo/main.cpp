// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkAccessManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"

#include <QCoreApplication>
#include <QDebug>
#include <QPointer>
#include <QTimer>

using namespace QCurl;

// 使用 httpbin.org 演示 manager 的正式入口；调度控制不等于传输 pause/resume。
class SchedulerDemo : public QObject
{
    Q_OBJECT

public:
    explicit SchedulerDemo(QObject *parent = nullptr)
        : QObject(parent)
    {
        m_manager.enableRequestScheduler(true);
        auto policy = m_manager.schedulerPolicy();
        policy.setMaxConcurrentRequests(1);
        policy.setMaxRequestsPerHost(1);
        policy.setAdmissionByteBudget(0);
        QString error;
        if (!m_manager.setSchedulerPolicy(policy, &error)) {
            qFatal("%s", qPrintable(error));
        }
        observeScheduling();
    }

    void run()
    {
        createRequest(QStringLiteral("https://httpbin.org/delay/2"), QCNetworkRequestPriority::Low);
        const QPointer<QCNetworkReply> pending = createRequest(QStringLiteral(
                                                                   "https://httpbin.org/get"),
                                                               QCNetworkRequestPriority::Normal);
        report(
            m_manager.setScheduledRequestPriority(pending.data(), QCNetworkRequestPriority::High));
        report(m_manager.deferScheduledRequest(pending.data()));
        QTimer::singleShot(300, this, [this, pending]() {
            if (pending) {
                report(m_manager.undeferScheduledRequest(pending.data()));
            }
        });
        createRequest(QStringLiteral("https://httpbin.org/anything/cancel-before-start"),
                      QCNetworkRequestPriority::Critical);
        QTimer::singleShot(8000, this, &SchedulerDemo::finish);
    }

private:
    Q_DISABLE_COPY_MOVE(SchedulerDemo)

    static void report(SchedulerCommandResult result)
    {
        if (result != SchedulerCommandResult::Applied
            && result != SchedulerCommandResult::NoChange) {
            qWarning() << "调度命令被拒绝，分类：" << static_cast<int>(result);
        }
    }

    QCNetworkReply *createRequest(const QString &url, QCNetworkRequestPriority priority)
    {
        QCNetworkRequest request{QUrl(url)};
        request.setPriority(priority);
        auto *reply = m_manager.get(request);
        if (!reply) {
            return nullptr;
        }
        connect(reply, &QCNetworkReply::finished, this, [guard = QPointer<QCNetworkReply>(reply)]() {
            if (!guard) {
                return;
            }
            qInfo() << "请求终态：" << guard->url() << guard->errorString();
            guard->deleteLater();
        });
        return reply;
    }

    void observeScheduling()
    {
        connect(&m_manager,
                &QCNetworkAccessManager::schedulerRequestQueued,
                this,
                [](QCNetworkReply *,
                   const QCNetworkLaneKey &lane,
                   const QString &origin,
                   QCNetworkRequestPriority priority) {
                    qInfo() << "入队：" << lane.name() << origin << toString(priority);
                });
        connect(&m_manager,
                &QCNetworkAccessManager::schedulerRequestPriorityChanged,
                this,
                [](QCNetworkReply *,
                   const QCNetworkLaneKey &,
                   const QString &,
                   QCNetworkRequestPriority priority) {
                    qInfo() << "优先级变化：" << toString(priority);
                });
        connect(&m_manager,
                &QCNetworkAccessManager::schedulerRequestAboutToStart,
                this,
                [this](QCNetworkReply *reply) {
                    // 同线程同步窗口中的取消能够阻止本次 execute；queued 槽不具有该合同。
                    if (reply->url().path().endsWith(QStringLiteral("cancel-before-start"))) {
                        report(m_manager.cancelScheduledRequest(reply));
                    }
                });
        connect(&m_manager,
                &QCNetworkAccessManager::schedulerRequestStarted,
                this,
                [](QCNetworkReply *, const QCNetworkLaneKey &, const QString &origin) {
                    qInfo() << "execute 已提交：" << origin;
                });
        connect(&m_manager, &QCNetworkAccessManager::schedulerPendingQueueEmpty, this, []() {
            qInfo() << "Pending 已清空；不表示 Running/Deferred 或传输已结束";
        });
    }

    void finish()
    {
        const auto result = m_manager.cancelLaneRequests(
            QCNetworkLaneKey::defaultLane(),
            QCNetworkAccessManager::SchedulerCancelScope::PendingAndRunning);
        if (!result.isSuccess()) {
            qWarning() << result.error();
        }
        const auto stats = m_manager.schedulerStatistics();
        qInfo() << "已完成：" << stats.completedRequests() << "已取消："
                << stats.cancelledRequests() << "平均调度响应时间（ms）："
                << stats.avgResponseTime();
        QCoreApplication::quit();
    }

    QCNetworkAccessManager m_manager{this};
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    SchedulerDemo demo;
    demo.run();
    return app.exec();
}

#include "main.moc"
