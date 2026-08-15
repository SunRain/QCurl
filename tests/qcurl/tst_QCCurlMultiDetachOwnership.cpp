#include "QCCurlMultiManager.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCurlRuntime.h"
#include "private/QCCurlMultiManagerTestAccess_p.h"
#include "private/QCurlRuntimeState_p.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QSemaphore>
#include <QString>
#include <QThread>
#include <QtTest>

#include <atomic>
#include <thread>
#include <utility>

using namespace QCurl;

class tst_QCCurlMultiDetachOwnership final : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void detachOwnershipContract();

private:
    /// 通过真实 add-reply 路径验证 share rollback poison 的 admission 合同。
    void verifyAddReplyRollbackPoison();
    /// 通过独立线程析构 manager 验证 quarantine registry 的并发追加合同。
    void verifyConcurrentQuarantineAppend();
    /**
     * @brief 验证错误线程的 reply 管理命令同步拒绝且不产生延迟副作用。
     *
     * 仅处理排队调用事件，以发现旧的 queued fallback，同时避免运行合法的 curl timer。
     */
    void verifyReplyCommandsRejectWrongThread();
};

void tst_QCCurlMultiDetachOwnership::verifyReplyCommandsRejectWrongThread()
{
    QCurlRuntime runtime;
    QVERIFY(runtime.isProcessOwner());

    QCCurlMultiManager *manager = QCCurlMultiManager::instance();
    QVERIFY(manager->isReady());
    const int initialActive  = manager->activeRepliesCountForTest();
    const int initialRunning = manager->runningRequestsCount();

    QCNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:1/owner-thread-contract")));
    auto *reply = new QCNetworkReply(QCNetworkReply::TestOnlyKey{}, request, HttpMethod::Get);
    QCOMPARE(reply->thread(), manager->thread());

    std::thread addWorker([manager, reply]() { manager->addReply(reply); });
    addWorker.join();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCOMPARE(manager->activeRepliesCountForTest(), initialActive);
    QCOMPARE(manager->runningRequestsCount(), initialRunning);

    manager->addReply(reply);
    QCOMPARE(manager->activeRepliesCountForTest(), initialActive + 1);
    QCOMPARE(manager->runningRequestsCount(), initialRunning + 1);
    // 清除 owner-thread add 产生的合法唤醒，只保留后续错误线程命令的排队副作用观察窗口。
    QCoreApplication::removePostedEvents(manager, QEvent::MetaCall);

    std::thread removeWorker([manager, reply]() { manager->removeReply(reply); });
    removeWorker.join();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCOMPARE(manager->activeRepliesCountForTest(), initialActive + 1);
    QCOMPARE(manager->runningRequestsCount(), initialRunning + 1);

    manager->removeReply(reply);
    QTRY_COMPARE_WITH_TIMEOUT(manager->activeRepliesCountForTest(), initialActive, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(manager->runningRequestsCount(), initialRunning, 1000);
    delete reply;
}

void tst_QCCurlMultiDetachOwnership::verifyConcurrentQuarantineAppend()
{
    qunsetenv("QCURL_TEST_FORCE_MULTI_FAILURE");
    qputenv("QCURL_TEST_FORCE_MULTI_REMOVE_ERROR", "internal");

    QCurlRuntime runtime;
    QVERIFY(runtime.isProcessOwner());

    constexpr int kWorkerCount  = 8;
    const int initialGraphCount = QCCurlMultiManager::quarantinedGraphCountForTest();
    std::atomic<int> failures{0};
    QSemaphore ready;
    QSemaphore start;
    QList<QThread *> workers;
    workers.reserve(kWorkerCount);

    for (int i = 0; i < kWorkerCount; ++i) {
        QThread *worker = QThread::create([&failures, &ready, &start]() {
            QCCurlMultiManager *manager             = QCCurlMultiManager::instance();
            QCCurlMultiManager::TransferToken token = 0;
            QString error;
            const bool prepared = manager->addTransferForTest(&token, &error) && token != 0;
            if (!prepared) {
                ++failures;
            }
            ready.release();
            start.acquire();
            if (!prepared) {
                return;
            }

            manager->removeTransfer(token);
            if (!manager->isPoisonedForTest()) {
                ++failures;
            }
        });
        worker->setParent(this);
        workers.append(worker);
        worker->start();
    }

    QVERIFY(ready.tryAcquire(kWorkerCount, 5000));
    start.release(kWorkerCount);
    for (QThread *worker : std::as_const(workers)) {
        QVERIFY(worker->wait(5000));
    }

    QCOMPARE(failures.load(), 0);
    QCOMPARE(QCCurlMultiManager::quarantinedGraphCountForTest(), initialGraphCount + kWorkerCount);
    QVERIFY(QCCurlMultiManager::quarantineIsNonCallableForTest());
    QCOMPARE(runtime.state(), QCurlRuntimeState::Failed);
    QCOMPARE(Internal::runtimeCleanupCountForTest(), 0);
}

void tst_QCCurlMultiDetachOwnership::verifyAddReplyRollbackPoison()
{
    QCurlRuntime runtime;
    QVERIFY(runtime.isProcessOwner());

    QCCurlMultiManager *manager = QCCurlMultiManager::instance();
    QVERIFY(manager->isReady());

    const int initialPendingTransfers = QCCurlMultiManager::quarantinedPendingTransfersForTest();

    QCNetworkAccessManager accessManager;
    QCNetworkAccessManager::ShareHandleConfig shareConfig;
    shareConfig.setShareCookies(true);
    accessManager.setShareHandleConfig(shareConfig);
    qputenv("QCURL_TEST_FORCE_MULTI_FAILURE", "add");

    QCNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:1/rollback-poison")));
    QCNetworkReply *reply = accessManager.get(request);
    QVERIFY(reply);

    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);

    QVERIFY(manager->isPoisonedForTest());
    QVERIFY(!manager->isReady());
    QCOMPARE(runtime.state(), QCurlRuntimeState::Failed);
    QCOMPARE(Internal::runtimeCleanupCountForTest(), 0);
    QCOMPARE(manager->activeRepliesCountForTest(), 0);
    QCOMPARE(manager->runningRequestsCount(), 0);
    QCOMPARE(manager->socketActionCountForTest(), 0);
    QCOMPARE(QCCurlMultiManagerTestAccess::activeShareBindings(manager), 1);
    QCOMPARE(QCCurlMultiManagerTestAccess::activeShareContexts(manager), 1);
    QCOMPARE(QCCurlMultiManagerTestAccess::activeShareContextsWithHandle(manager), 1);
    QCOMPARE(QCCurlMultiManagerTestAccess::activeShareUsers(manager), 1);
    QCOMPARE(QCCurlMultiManager::quarantinedPendingTransfersForTest(), initialPendingTransfers + 1);
    QVERIFY(QCCurlMultiManager::quarantineIsNonCallableForTest());
    QVERIFY(reply->errorString().contains(QStringLiteral("poisoned")));

    QCoreApplication::processEvents();
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(reply->isFinished());
    reply->deleteLater();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void tst_QCCurlMultiDetachOwnership::detachOwnershipContract()
{
    const QByteArray scenario = qgetenv("QCURL_TEST_MULTI_OWNERSHIP_CASE").trimmed();
    QVERIFY2(!scenario.isEmpty(), "QCURL_TEST_MULTI_OWNERSHIP_CASE is required");

    if (scenario == "rollback-failure") {
        qputenv("QCURL_TEST_FORCE_COOKIE_OPTION_ERROR",
                "setup:CURLOPT_COOKIEFILE,rollback:CURLOPT_SHARE");
        verifyAddReplyRollbackPoison();
        return;
    }
    if (scenario == "concurrent-quarantine") {
        verifyConcurrentQuarantineAppend();
        return;
    }
    if (scenario == "owner-thread-rejection") {
        verifyReplyCommandsRejectWrongThread();
        return;
    }

    QCCurlMultiManager *manager = QCCurlMultiManager::instance();
    QVERIFY(manager->isReady());

    QCCurlMultiManager::TransferToken token = 0;
    QString error;
    QVERIFY2(manager->addTransferForTest(&token, &error), qPrintable(error));
    QVERIFY(token != 0);
    QCOMPARE(manager->activeRepliesCountForTest(), 1);
    QCOMPARE(manager->runningRequestsCount(), 1);

    if (scenario == "detach-failure") {
        qputenv("QCURL_TEST_FORCE_COOKIE_OPTION_ERROR", "rollback:CURLOPT_SHARE");
    } else if (scenario == "cleanup-in-use") {
        qputenv("QCURL_TEST_FORCE_SHARE_CLEANUP_ERROR", "cleanup-in-use");
    } else if (scenario == "shutdown-cleanup-failure") {
        qputenv("QCURL_TEST_FORCE_SHARE_CLEANUP_ERROR", "shutdown-cleanup-failure");
    }

    if (scenario == "detach-failure" || scenario == "cleanup-in-use"
        || scenario == "shutdown-cleanup-failure") {
        QVERIFY2(QCCurlMultiManagerTestAccess::prepareShareDetach(manager, token, &error),
                 qPrintable(error));
    }

    if (scenario == "shutdown-cleanup-failure") {
        manager->shutdownForTest();
    } else if (scenario == "unknown-done") {
        manager->processUnknownDoneForTest();
    } else {
        manager->removeTransfer(token);
    }

    if (scenario == "recursive-once") {
        QCOMPARE(manager->activeRepliesCountForTest(), 1);
        QCOMPARE(manager->runningRequestsCount(), 1);
        QCOMPARE(manager->completionCountForTest(), 0);
        QVERIFY(!manager->isPoisonedForTest());

        QTRY_COMPARE_WITH_TIMEOUT(manager->activeRepliesCountForTest(), 0, 1000);
        QCOMPARE(manager->runningRequestsCount(), 0);
        QCOMPARE(manager->completionCountForTest(), 1);
        QVERIFY(manager->completionHadHandleForTest());
        QVERIFY(!manager->isPoisonedForTest());
        QVERIFY(manager->isReady());
        return;
    }

    if (scenario == "shutdown-cleanup-failure") {
        QVERIFY(manager->isPoisonedForTest());
        QVERIFY(!manager->isReady());
        QCOMPARE(manager->activeRepliesCountForTest(), 0);
        QCOMPARE(manager->runningRequestsCount(), 0);
        QCOMPARE(manager->completionCountForTest(), 1);
        QVERIFY(manager->completionHadHandleForTest());
        QCOMPARE(manager->socketActionCountForTest(), 0);
        QCOMPARE(QCCurlMultiManagerTestAccess::activeShareBindings(manager), 0);
        QCOMPARE(QCCurlMultiManagerTestAccess::activeShareContexts(manager), 1);
        QCOMPARE(QCCurlMultiManagerTestAccess::activeShareContextsWithHandle(manager), 1);
        QCOMPARE(QCCurlMultiManagerTestAccess::activeShareUsers(manager), 0);

        QCCurlMultiManager::TransferToken rejectedToken = 0;
        QString rejectedError;
        QVERIFY(!manager->addTransferForTest(&rejectedToken, &rejectedError));
        QCOMPARE(rejectedToken, QCCurlMultiManager::TransferToken{0});
        QVERIFY(rejectedError.contains(QStringLiteral("poisoned")));

        QCoreApplication::processEvents();
        QCOMPARE(manager->activeRepliesCountForTest(), 0);
        QCOMPARE(manager->runningRequestsCount(), 0);
        QCOMPARE(manager->completionCountForTest(), 1);
        return;
    }

    QVERIFY2(manager->isPoisonedForTest(), qPrintable(scenario));
    QVERIFY(!manager->isReady());
    QCOMPARE(manager->activeRepliesCountForTest(), 1);
    QCOMPARE(manager->runningRequestsCount(), 1);
    QCOMPARE(manager->completionCountForTest(), 0);
    QCOMPARE(manager->socketActionCountForTest(), 0);

    if (scenario == "detach-failure") {
        QCOMPARE(QCCurlMultiManagerTestAccess::activeShareBindings(manager), 1);
        QCOMPARE(QCCurlMultiManagerTestAccess::activeShareContexts(manager), 1);
        QCOMPARE(QCCurlMultiManagerTestAccess::activeShareContextsWithHandle(manager), 1);
        QCOMPARE(QCCurlMultiManagerTestAccess::activeShareUsers(manager), 1);
    } else if (scenario == "cleanup-in-use") {
        QCOMPARE(QCCurlMultiManagerTestAccess::activeShareBindings(manager), 0);
        QCOMPARE(QCCurlMultiManagerTestAccess::activeShareContexts(manager), 1);
        QCOMPARE(QCCurlMultiManagerTestAccess::activeShareContextsWithHandle(manager), 1);
        QCOMPARE(QCCurlMultiManagerTestAccess::activeShareUsers(manager), 0);
    }

    QCCurlMultiManager::TransferToken rejectedToken = 0;
    QString rejectedError;
    QVERIFY(!manager->addTransferForTest(&rejectedToken, &rejectedError));
    QCOMPARE(rejectedToken, QCCurlMultiManager::TransferToken{0});
    QVERIFY(rejectedError.contains(QStringLiteral("poisoned")));

    QCoreApplication::processEvents();
    QCOMPARE(manager->activeRepliesCountForTest(), 1);
    QCOMPARE(manager->runningRequestsCount(), 1);
    QCOMPARE(manager->completionCountForTest(), 0);
    QCOMPARE(manager->socketActionCountForTest(), 0);
}

QTEST_MAIN(tst_QCCurlMultiDetachOwnership)

#include "tst_QCCurlMultiDetachOwnership.moc"
