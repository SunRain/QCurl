#include "QCNetworkAccessManager.h"
#include "QCNetworkConnectionPoolManager.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "qcurl_http_script_server.h"

#include <QScopeGuard>
#include <QSignalSpy>
#include <QThread>
#include <QtTest>

using namespace QCurl;

class PoolWorker final : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(PoolWorker)

public:
    PoolWorker() = default;

    void runBatch(const QUrl &url)
    {
        auto *manager = new QCNetworkAccessManager(this);
        m_remaining   = 3;
        m_errors      = 0;
        for (int i = 0; i < 3; ++i) {
            QCNetworkRequest request(url);
            request.setHttpVersion(QCNetworkHttpVersion::Http1_1);
            auto *reply = manager->get(request);
            connect(reply, &QCNetworkReply::finished, manager, [this, manager, reply]() {
                m_errors += reply->error() != NetworkError::NoError;
                if (--m_remaining == 0) {
                    manager->deleteLater();
                    Q_EMIT done(m_errors);
                }
            });
        }
    }

Q_SIGNALS:
    void done(int errors);

private:
    int m_remaining = 0;
    int m_errors    = 0;
};

class WorkerThread final
{
public:
    WorkerThread()
        : worker(new PoolWorker)
    {
        worker->moveToThread(&thread);
        QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
        thread.start();
    }

    ~WorkerThread()
    {
        thread.quit();
        thread.wait();
    }

    void runBatch(const QUrl &url)
    {
        QMetaObject::invokeMethod(
            worker, [this, url]() { worker->runBatch(url); }, Qt::QueuedConnection);
    }

    QThread thread;
    PoolWorker *worker;
};

class tst_QCNetworkPoolContract : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(tst_QCNetworkPoolContract)

public:
    tst_QCNetworkPoolContract() = default;

private Q_SLOTS:
    void cleanup();
    void independentThreadsAndClear();
    void changeDuringTransfer();
    void terminalAccounting_data();
    void terminalAccounting();
};

void tst_QCNetworkPoolContract::cleanup()
{
    QCOMPARE(QCNetworkConnectionPoolManager::instance()->setConfig({}),
             QCNetworkConnectionPoolManager::UpdateResult::Applied);
    QTRY_COMPARE(QCNetworkConnectionPoolManager::instance()->statistics().activeRequests(), 0);
}

void tst_QCNetworkPoolContract::independentThreadsAndClear()
{
    auto response    = HttpScriptServer::response(200, "ok");
    response.delayMs = 80;
    HttpScriptServer server1({response});
    HttpScriptServer server2({response});
    QVERIFY(server1.start());
    QVERIFY(server2.start());
    WorkerThread first;
    WorkerThread second;
    auto *pool = QCNetworkConnectionPoolManager::instance();
    QCNetworkConnectionPoolConfig config;
    config.setMultiMaxTotalConnections(1);
    config.setMultiMaxHostConnections(1);
    QCOMPARE(pool->setConfig(config), QCNetworkConnectionPoolManager::UpdateResult::Applied);
    QList<int> firstDone;
    QList<int> secondDone;
    QList<int> newDone;
    QObject receiver;
    // QSignalSpy 的直连回调会跨线程修改 QList；在本线程的 context 中收集结果。
    connect(first.worker, &PoolWorker::done, &receiver, [&](int errors) { firstDone.append(errors); });
    connect(second.worker, &PoolWorker::done, &receiver, [&](int errors) { secondDone.append(errors); });
    first.runBatch(server1.url());
    second.runBatch(server2.url());
    QTRY_COMPARE(firstDone.size(), 1);
    QTRY_COMPARE(secondDone.size(), 1);
    QCOMPARE(firstDone.at(0), 0);
    QCOMPARE(secondDone.at(0), 0);
    QCOMPARE(server1.peakRequests(), 1);
    QCOMPARE(server2.peakRequests(), 1);
    QCOMPARE(pool->statistics().activeRequests(), 0);

    WorkerThread newlyCreated;
    connect(newlyCreated.worker, &PoolWorker::done, &receiver, [&](int errors) { newDone.append(errors); });
    newlyCreated.runBatch(server1.url());
    QTRY_COMPARE(newDone.size(), 1);
    QCOMPARE(newDone.at(0), 0);
    QCOMPARE(server1.peakRequests(), 1);
    QCOMPARE(pool->setConfig({}), QCNetworkConnectionPoolManager::UpdateResult::Applied);
    first.runBatch(server1.url());
    second.runBatch(server2.url());
    QTRY_COMPARE(firstDone.size(), 2);
    QTRY_COMPARE(secondDone.size(), 2);
    QCOMPARE(firstDone.at(1), 0);
    QCOMPARE(secondDone.at(1), 0);
    QCOMPARE(server1.peakRequests(), 3);
    QCOMPARE(server2.peakRequests(), 3);
}

void tst_QCNetworkPoolContract::changeDuringTransfer()
{
    auto response    = HttpScriptServer::response(200, "ok");
    response.delayMs = 200;
    HttpScriptServer server({response});
    QVERIFY(server.start());
    auto *pool = QCNetworkConnectionPoolManager::instance();
    QCNetworkConnectionPoolConfig config;
    config.setMultiMaxTotalConnections(1);
    QCOMPARE(pool->setConfig(config), QCNetworkConnectionPoolManager::UpdateResult::Applied);
    QCNetworkAccessManager manager;
    auto *first = manager.get(QCNetworkRequest(server.url()));
    QSignalSpy firstDone(first, &QCNetworkReply::finished);
    QTRY_COMPARE(server.activeRequests(), 1);
    QCOMPARE(pool->statistics().activeRequests(), 1);
    pool->resetStatistics();
    QCOMPARE(pool->statistics().activeRequests(), 1);
    QCOMPARE(pool->setConfig({}), QCNetworkConnectionPoolManager::UpdateResult::Applied);
    auto *second = manager.get(QCNetworkRequest(server.url()));
    QSignalSpy secondDone(second, &QCNetworkReply::finished);
    QTRY_COMPARE(server.peakRequests(), 2);
    QTRY_COMPARE(firstDone.size(), 1);
    QTRY_COMPARE(secondDone.size(), 1);
    QCOMPARE(first->error(), NetworkError::NoError);
    QCOMPARE(second->error(), NetworkError::NoError);
    QCOMPARE(pool->statistics().activeRequests(), 0);
    QCOMPARE(pool->statistics().totalRequests(), 2);
}

void tst_QCNetworkPoolContract::terminalAccounting_data()
{
    QTest::addColumn<int>("mode");
    QTest::newRow("success") << 0;
    QTest::newRow("http-error") << 1;
    QTest::newRow("cancel") << 2;
    QTest::newRow("destroy") << 3;
}

void tst_QCNetworkPoolContract::terminalAccounting()
{
    QFETCH(int, mode);
    auto response    = HttpScriptServer::response(mode == 1 ? 503 : 200, "body");
    response.delayMs = mode >= 2 ? 1000 : 0;
    HttpScriptServer server({response});
    QVERIFY(server.start());
    auto *pool = QCNetworkConnectionPoolManager::instance();
    pool->resetStatistics();
    QCNetworkAccessManager manager;
    auto *reply = manager.get(QCNetworkRequest(server.url()));
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    QSignalSpy cancelled(reply, &QCNetworkReply::cancelled);
    QSignalSpy failed(reply, qOverload<NetworkError>(&QCNetworkReply::error));
    QTRY_COMPARE(server.requests().size(), 1);
    if (mode == 2) {
        reply->cancel();
    } else if (mode == 3) {
        delete reply;
    }
    if (mode != 3) {
        QTRY_COMPARE(finished.size(), 1);
        const auto expectedError = mode == 0   ? NetworkError::NoError
                                   : mode == 1 ? NetworkError::HttpServiceUnavailable
                                               : NetworkError::OperationCancelled;
        QCOMPARE(reply->error(), expectedError);
        QCOMPARE(reply->state(),
                 mode == 0   ? ReplyState::Finished
                 : mode == 1 ? ReplyState::Error
                             : ReplyState::Cancelled);
        QCOMPARE(cancelled.size(), mode == 2 ? 1 : 0);
        QCOMPARE(failed.size(), mode == 1 ? 1 : 0);
    }
    QCOMPARE(pool->statistics().activeRequests(), 0);
    QCOMPARE(pool->statistics().totalRequests(), 1);
    QCOMPARE(pool->statistics().reusedConnections(), 0);
}

QTEST_GUILESS_MAIN(tst_QCNetworkPoolContract)
#include "tst_QCNetworkPoolContract.moc"
