#include "QCWebSocket.h"
#include "QCWebSocketPool.h"
#include "QCWebSocketTestServer.h"

#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QtTest>

#include <memory>
#include <utility>

using namespace QCurl;

class tst_QCWebSocketPoolReentry : public QObject
{
    Q_OBJECT

public:
    tst_QCWebSocketPoolReentry() = default;

private:
    Q_DISABLE_COPY_MOVE(tst_QCWebSocketPoolReentry)

private Q_SLOTS:
    void initTestCase();
    void failureContinuationClearsPool_data();
    void failureContinuationClearsPool();
    void failureReleasesCapacityBeforeContinuation();
    void terminalPaths_data();
    void terminalPaths();
    void bulkClearPreservesNewConnections();
    void successContinuationInvalidatesLease();
    void continuationDestroysOwner_data();
    void continuationDestroysOwner();
    void preWarmContinuation_data();
    void preWarmContinuation();
    void lateNotificationsKeepReplacementAndOtherUrl();

private:
    QCWebSocketTestServer m_server;
};

void tst_QCWebSocketPoolReentry::initTestCase()
{
    QVERIFY2(m_server.start(QCWebSocketTestServer::Mode::Ws), qPrintable(m_server.skipReason()));
}

void tst_QCWebSocketPoolReentry::failureContinuationClearsPool_data()
{
    QTest::addColumn<bool>("all");
    QTest::newRow("url") << false;
    QTest::newRow("all") << true;
}

void tst_QCWebSocketPoolReentry::failureContinuationClearsPool()
{
    QFETCH(bool, all);
    // 动态保留端口并关闭监听，使用真实本地拒绝连接路径。
    QTcpServer reservation;
    QVERIFY(reservation.listen(QHostAddress::LocalHost, 0));
    const QUrl url(QStringLiteral("ws://127.0.0.1:%1/failure").arg(reservation.serverPort()));
    reservation.close();
    QCWebSocketPool pool;
    int calls         = 0;
    auto future       = pool.acquire(url);
    auto continuation = future.then([&](QCWebSocketAcquireResult result) {
        ++calls;
        const int before = pool.statistics().totalConnections();
        QString error;
        const bool cleared = pool.clearPool(all ? QUrl() : url, &error);
        QCOMPARE(result.status(), QCWebSocketAcquireResult::Status::ConnectionFailed);
        QCOMPARE(before, 0);
        QVERIFY2(cleared, qPrintable(error));
        QCOMPARE(pool.statistics().totalConnections(), 0);
    });
    QTRY_VERIFY_WITH_TIMEOUT(continuation.isFinished(), 10000);
    QCOMPARE(calls, 1);
    QCOMPARE(future.resultCount(), 1);
}

void tst_QCWebSocketPoolReentry::failureReleasesCapacityBeforeContinuation()
{
    QTcpServer reservation;
    QVERIFY(reservation.listen(QHostAddress::LocalHost, 0));
    const QUrl url(QStringLiteral("ws://127.0.0.1:%1/failure").arg(reservation.serverPort()));
    reservation.close();
    QCWebSocketPoolConfig config;
    config.setMaxPoolSize(1);
    QCWebSocketPool pool(config);
    auto first = pool.acquire(url);
    QFuture<QCWebSocketAcquireResult> second;
    auto continuation = first.then([&](QCWebSocketAcquireResult result) {
        QCOMPARE(result.status(), QCWebSocketAcquireResult::Status::ConnectionFailed);
        QCOMPARE(pool.statistics(url).totalConnections(), 0);
        second = pool.acquire(url);
        QVERIFY(!second.isFinished());
        QCOMPARE(pool.statistics(url).totalConnections(), 1);
    });
    QTRY_VERIFY(continuation.isFinished());
    QTRY_VERIFY(second.isFinished());
    QCOMPARE(second.result().status(), QCWebSocketAcquireResult::Status::ConnectionFailed);
    QCOMPARE(second.resultCount(), 1);
    QCOMPARE(first.resultCount(), 1);
    QCOMPARE(pool.statistics().totalConnections(), 0);
}

void tst_QCWebSocketPoolReentry::terminalPaths_data()
{
    QTest::addColumn<QString>("cause");
    QTest::newRow("timeout") << QStringLiteral("timeout");
    QTest::newRow("disconnect") << QStringLiteral("disconnect");
    QTest::newRow("destroyed") << QStringLiteral("destroyed");
}

void tst_QCWebSocketPoolReentry::terminalPaths()
{
    QFETCH(QString, cause);
    QTcpServer stalled;
    QVERIFY(stalled.listen(QHostAddress::LocalHost, 0));
    const QUrl url(QStringLiteral("ws://127.0.0.1:%1/stall").arg(stalled.serverPort()));
    QCWebSocketPool pool;
    auto future        = pool.acquire(url);
    const auto sockets = pool.findChildren<QCWebSocket *>();
    QCOMPARE(sockets.size(), 1);
    QPointer<QCWebSocket> socket(sockets.first());
    int calls         = 0;
    auto continuation = future.then([&](QCWebSocketAcquireResult result) {
        ++calls;
        QCOMPARE(result.status(), QCWebSocketAcquireResult::Status::ConnectionFailed);
        QCOMPARE(pool.statistics(url).totalConnections(), 0);
        QVERIFY(!pool.contains(url));
        QString error;
        QVERIFY2(pool.clearPool(url, &error), qPrintable(error));
    });
    if (cause == "destroyed") {
        QObject owner;
        socket->setParent(&owner);
    } else if (cause == "disconnect") {
        QTRY_COMPARE(socket->state(), QCWebSocket::State::Connecting);
        socket->abort();
    }
    QTRY_VERIFY_WITH_TIMEOUT(continuation.isFinished(), 8000);
    if (cause == "timeout") {
        QVERIFY(future.result().error().contains("timed out"));
    }
    QCOMPARE(calls, 1);
    QCOMPARE(future.resultCount(), 1);
}

void tst_QCWebSocketPoolReentry::bulkClearPreservesNewConnections()
{
    QCWebSocketPool pool;
    const QUrl firstUrl(m_server.urlWithPath("/echo?case=bulk-first"));
    const QUrl secondUrl(m_server.urlWithPath("/echo?case=bulk-second"));
    auto first  = pool.acquire(firstUrl);
    auto second = pool.acquire(secondUrl);
    QFuture<QCWebSocketAcquireResult> replacement;
    bool replaced      = false;
    int calls          = 0;
    const auto reenter = [&](QCWebSocketAcquireResult result) {
        ++calls;
        QCOMPARE(result.status(), QCWebSocketAcquireResult::Status::Cancelled);
        if (!std::exchange(replaced, true)) {
            QCOMPARE(pool.statistics().totalConnections(), 0);
            QVERIFY(!pool.contains(firstUrl) && !pool.contains(secondUrl));
            QString error;
            QVERIFY2(pool.clearPool(QUrl(), &error), qPrintable(error));
            replacement = pool.acquire(secondUrl);
            QCOMPARE(pool.statistics().totalConnections(), 1);
        }
    };
    auto firstDone  = first.then(reenter);
    auto secondDone = second.then(reenter);
    QString error;
    QVERIFY2(pool.clearPool(QUrl(), &error), qPrintable(error));
    QVERIFY(firstDone.isFinished() && secondDone.isFinished());
    QCOMPARE(calls, 2);
    QCOMPARE(first.resultCount(), 1);
    QCOMPARE(second.resultCount(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(replacement.isFinished(), 8000);
    QCOMPARE(replacement.result().status(), QCWebSocketAcquireResult::Status::Success);
    QCOMPARE(pool.statistics().totalConnections(), 1);
    QCWebSocket *socket = nullptr;
    QCOMPARE(pool.resolveLease(replacement.result().leaseId(), &socket),
             QCWebSocketPool::LeaseResult::Success);
}

void tst_QCWebSocketPoolReentry::successContinuationInvalidatesLease()
{
    QCWebSocketPool pool;
    const QUrl url(m_server.urlWithPath("/echo?case=success"));
    const QUrl otherUrl(m_server.urlWithPath("/echo?case=other"));
    auto other = pool.acquire(otherUrl);
    QTRY_VERIFY(other.isFinished());
    auto future       = pool.acquire(url);
    auto continuation = future.then([&](QCWebSocketAcquireResult result) {
        QCOMPARE(result.status(), QCWebSocketAcquireResult::Status::Success);
        QCWebSocket *socket = nullptr;
        QCOMPARE(pool.resolveLease(result.leaseId(), &socket),
                 QCWebSocketPool::LeaseResult::Success);
        QVERIFY(socket);
        QString error;
        QVERIFY2(pool.clearPool(url, &error), qPrintable(error));
        QCOMPARE(pool.resolveLease(result.leaseId(), &socket),
                 QCWebSocketPool::LeaseResult::InactiveLease);
        QCOMPARE(pool.resolveLease(other.result().leaseId(), &socket),
                 QCWebSocketPool::LeaseResult::Success);
    });
    QTRY_VERIFY(continuation.isFinished());
    QCOMPARE(future.resultCount(), 1);
    QCOMPARE(future.result().status(), QCWebSocketAcquireResult::Status::Success);
}

void tst_QCWebSocketPoolReentry::continuationDestroysOwner_data()
{
    QTest::addColumn<bool>("success");
    QTest::newRow("success") << true;
    QTest::newRow("failure") << false;
}

void tst_QCWebSocketPoolReentry::continuationDestroysOwner()
{
    QFETCH(bool, success);
    QTcpServer stalled;
    QVERIFY(stalled.listen(QHostAddress::LocalHost, 0));
    const QUrl pendingUrl(QStringLiteral("ws://127.0.0.1:%1/stall").arg(stalled.serverPort()));
    auto owner = std::make_unique<QObject>();
    auto *pool = new QCWebSocketPool(owner.get());
    QPointer<QCWebSocketPool> safePool(pool);
    auto pending = pool->acquire(pendingUrl);
    const QUrl url(m_server.urlWithPath("/echo?case=owner"));
    auto future = pool->acquire(success ? url : QUrl("invalid://failure"));
    QSignalSpy created(pool, &QCWebSocketPool::connectionCreated);
    auto continuation = future.then([&](QCWebSocketAcquireResult result) {
        QCOMPARE(result.status(),
                 success ? QCWebSocketAcquireResult::Status::Success
                         : QCWebSocketAcquireResult::Status::ConnectionFailed);
        owner.reset();
    });
    QTRY_VERIFY(continuation.isFinished());
    QVERIFY(!safePool);
    QCOMPARE(future.resultCount(), 1);
    QVERIFY(pending.isFinished());
    QCOMPARE(pending.resultCount(), 1);
    QCOMPARE(pending.result().status(), QCWebSocketAcquireResult::Status::PoolDestroyed);
    QCOMPARE(created.count(), 0);
}

void tst_QCWebSocketPoolReentry::preWarmContinuation_data()
{
    QTest::addColumn<bool>("destroy");
    QTest::addColumn<bool>("success");
    QTest::newRow("clear-success") << false << true;
    QTest::newRow("destroy-success") << true << true;
    QTest::newRow("clear-failure") << false << false;
    QTest::newRow("destroy-failure") << true << false;
}

void tst_QCWebSocketPoolReentry::preWarmContinuation()
{
    QFETCH(bool, destroy);
    QFETCH(bool, success);
    QTcpServer stalled;
    QVERIFY(stalled.listen(QHostAddress::LocalHost, 0));
    const QUrl pendingUrl(QStringLiteral("ws://127.0.0.1:%1/stall").arg(stalled.serverPort()));
    auto owner = std::make_unique<QObject>();
    auto *pool = new QCWebSocketPool(owner.get());
    const QUrl url(m_server.urlWithPath("/echo?case=warm"));
    auto pending      = pool->preWarm(pendingUrl, 1);
    auto future       = pool->preWarm(success ? url : QUrl("invalid://failure"), 2);
    int calls         = 0;
    auto continuation = future.then([&](QCWebSocketPreWarmResult result) {
        ++calls;
        QCOMPARE(result.status(),
                 success ? QCWebSocketPreWarmResult::Status::Success
                         : QCWebSocketPreWarmResult::Status::ConnectionFailed);
        QCOMPARE(result.requestedCount(), 2);
        QCOMPARE(result.warmedCount(), success ? 2 : 0);
        if (destroy) {
            owner.reset();
        } else {
            QString error;
            QVERIFY2(pool->clearPool(QUrl(), &error), qPrintable(error));
            QCOMPARE(pool->statistics().totalConnections(), 0);
        }
    });
    QTRY_VERIFY(continuation.isFinished());
    QCOMPARE(future.resultCount(), 1);
    QCOMPARE(calls, 1);
    QTRY_VERIFY(pending.isFinished());
    QCOMPARE(pending.resultCount(), 1);
    QCOMPARE(pending.result().status(),
             destroy ? QCWebSocketPreWarmResult::Status::PoolDestroyed
                     : QCWebSocketPreWarmResult::Status::Cancelled);
}

void tst_QCWebSocketPoolReentry::lateNotificationsKeepReplacementAndOtherUrl()
{
    QCWebSocketPool pool;
    const QUrl url(m_server.urlWithPath("/echo?case=late"));
    const QUrl otherUrl(m_server.urlWithPath("/echo?case=late-other"));
    auto first = pool.acquire(url);
    const QPointer<QCWebSocket> oldSocket(pool.findChild<QCWebSocket *>());
    QVERIFY(oldSocket);
    int calls      = 0;
    auto firstDone = first.then([&](QCWebSocketAcquireResult result) {
        ++calls;
        QCOMPARE(result.status(), QCWebSocketAcquireResult::Status::Cancelled);
    });
    QSignalSpy closed(&pool, &QCWebSocketPool::connectionClosed);
    QString error;
    QVERIFY2(pool.clearPool(url, &error), qPrintable(error));
    auto replacement = pool.acquire(url);
    auto other       = pool.acquire(otherUrl);
    // 旧 socket 尚未执行 DeferredDelete；其真实断开通知不得摘除新的连接。
    oldSocket->abort();
    QTRY_VERIFY(replacement.isFinished() && other.isFinished());
    QCOMPARE(replacement.result().status(), QCWebSocketAcquireResult::Status::Success);
    QCOMPARE(other.result().status(), QCWebSocketAcquireResult::Status::Success);
    // 走过旧连接的五秒 watchdog，验证迟到定时器也不会再次结算。
    QTest::qWait(5200);
    QCOMPARE(calls, 1);
    QCOMPARE(first.resultCount(), 1);
    QVERIFY(firstDone.isFinished());
    QCOMPARE(closed.count(), 1);
    QCOMPARE(pool.statistics().totalConnections(), 2);
    QCWebSocket *socket = nullptr;
    QCOMPARE(pool.resolveLease(replacement.result().leaseId(), &socket),
             QCWebSocketPool::LeaseResult::Success);
    QCOMPARE(pool.resolveLease(other.result().leaseId(), &socket),
             QCWebSocketPool::LeaseResult::Success);
}

QTEST_GUILESS_MAIN(tst_QCWebSocketPoolReentry)
#include "tst_QCWebSocketPoolReentry.moc"
