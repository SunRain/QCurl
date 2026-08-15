// SPDX-License-Identifier: MIT
// Copyright (c) 2026 QCurl Project

#include "../src/QCCurlMultiManager.h"
#include "../src/QCNetworkAccessManager.h"
#include "../src/QCNetworkReply.h"
#include "../src/QCNetworkRequest.h"
#include "test_wait_utils.h"

#include <QAbstractEventDispatcher>
#include <QFuture>
#include <QHostAddress>
#include <QPointer>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QtTest>

#include <atomic>

using namespace QCurl;

namespace {

int runningRequestsCountOnOwnerThread(QCNetworkAccessManager *manager)
{
    if (!manager) {
        return -1;
    }

    if (QThread::currentThread() == manager->thread()) {
        return QCCurlMultiManager::instance()->runningRequestsCount();
    }

    int count          = -1;
    const bool invoked = QMetaObject::invokeMethod(
        manager,
        [&count]() { count = QCCurlMultiManager::instance()->runningRequestsCount(); },
        Qt::BlockingQueuedConnection);
    return invoked ? count : -1;
}

struct MultiRegistrationFailureResult
{
    bool replyCreated        = false;
    bool replyOwnedByManager = false;
    QPointer<QCNetworkReply> reply;
    ReplyState state        = ReplyState::Idle;
    NetworkError error      = NetworkError::NoError;
    int errorSignalCount    = 0;
    int finishedSignalCount = 0;
    int activeReplyCount    = -1;
    int runningRequestCount = -1;
};

bool scheduleDeferredDeleteOnOwnerThread(QObject *object)
{
    if (!object) {
        return true;
    }

    return QMetaObject::invokeMethod(
        object,
        [guard = QPointer<QObject>(object)]() {
            if (guard) {
                guard->deleteLater();
            }
        },
        Qt::QueuedConnection);
}

MultiRegistrationFailureResult runMultiRegistrationFailureScenario(QCNetworkAccessManager *manager)
{
    MultiRegistrationFailureResult result;
    if (!manager) {
        return result;
    }

    const bool invoked = QMetaObject::invokeMethod(
        manager,
        [manager, &result]() {
            QCNetworkRequest request(
                QUrl(QStringLiteral("http://127.0.0.1:9/multi-registration-failure")));
            QCNetworkReply *reply = manager->get(request);
            result.replyCreated   = reply != nullptr;
            if (!reply) {
                return;
            }
            result.replyOwnedByManager = reply->parent() == manager;
            result.reply               = reply;

            const auto errorSignal = static_cast<void (QCNetworkReply::*)(NetworkError)>(
                &QCNetworkReply::error);
            QSignalSpy errorSpy(reply, errorSignal);
            QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
            TestWaitUtils::waitForSpyCount(finishedSpy, 1, 1000);
            TestWaitUtils::waitForNoAdditionalSignal(finishedSpy, 100);

            result.state               = reply->state();
            result.error               = reply->error();
            result.errorSignalCount    = errorSpy.count();
            result.finishedSignalCount = finishedSpy.count();
            auto *multiManager         = QCCurlMultiManager::instance();
            result.activeReplyCount    = multiManager->activeRepliesCountForTest();
            result.runningRequestCount = multiManager->runningRequestsCount();
        },
        Qt::BlockingQueuedConnection);

    if (!invoked) {
        result.replyCreated = false;
    }
    return result;
}

QCNetworkReply *getOnOwnerThread(QCNetworkAccessManager *manager, const QCNetworkRequest &request)
{
    if (!manager) {
        return nullptr;
    }

    if (QThread::currentThread() == manager->thread()) {
        return manager->get(request);
    }

    QCNetworkReply *reply = nullptr;
    const bool invoked    = QMetaObject::invokeMethod(
        manager,
        [manager, request, &reply]() { reply = manager->get(request); },
        Qt::BlockingQueuedConnection);
    return invoked ? reply : nullptr;
}

bool waitForEventDispatcher(QThread *thread, int timeoutMs = 1000)
{
    if (!thread) {
        return false;
    }

    return TestWaitUtils::waitUntil(
        [thread]() { return QAbstractEventDispatcher::instance(thread) != nullptr; }, timeoutMs);
}

bool hasCookie(const QList<QCCookie> &cookies, const QByteArray &name, const QByteArray &value)
{
    for (const QCCookie &cookie : cookies) {
        if (cookie.name() == name && cookie.value() == value) {
            return true;
        }
    }
    return false;
}

void enableSharedCookies(QCNetworkAccessManager *manager)
{
    QCNetworkAccessManager::ShareHandleConfig config;
    config.setShareCookies(true);
    manager->setShareHandleConfig(config);
}

class DelayedHttpServer
{
public:
    explicit DelayedHttpServer(int responseDelayMs,
                               QByteArray responseBody = QByteArrayLiteral("ok"))
        : m_responseDelayMs(responseDelayMs)
        , m_responseBody(std::move(responseBody))
    {
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this]() {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                m_lastSocket = socket;
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
                    m_requestBuffer.append(socket->readAll());
                    if (m_responseScheduled || !m_requestBuffer.contains("\r\n\r\n")) {
                        return;
                    }

                    m_responseScheduled = true;
                    QPointer<QTcpSocket> safeSocket(socket);
                    QTimer::singleShot(m_responseDelayMs, &m_server, [this, safeSocket]() {
                        if (!safeSocket) {
                            return;
                        }

                        const QByteArray response = QByteArrayLiteral(
                                                        "HTTP/1.1 200 OK\r\nContent-Length: ")
                                                    + QByteArray::number(m_responseBody.size())
                                                    + QByteArrayLiteral(
                                                        "\r\nConnection: close\r\n\r\n")
                                                    + m_responseBody;
                        safeSocket->write(response);
                        safeSocket->flush();
                        safeSocket->disconnectFromHost();
                    });
                });
                QObject::connect(socket,
                                 &QTcpSocket::disconnected,
                                 socket,
                                 &QTcpSocket::deleteLater);
            }
        });
    }

    bool start() { return m_server.listen(QHostAddress::LocalHost, 0); }

    [[nodiscard]] QString errorString() const { return m_server.errorString(); }

    [[nodiscard]] QUrl url(const QString &path) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_server.serverPort()).arg(path));
    }

    void stop()
    {
        m_server.close();
        if (m_lastSocket) {
            m_lastSocket->disconnectFromHost();
        }
    }

private:
    QTcpServer m_server;
    QPointer<QTcpSocket> m_lastSocket;
    QByteArray m_requestBuffer;
    int m_responseDelayMs = 0;
    QByteArray m_responseBody;
    bool m_responseScheduled = false;
};

} // namespace

class tst_QCNetworkActorThreadModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testMultiRegistrationFailureTerminatesReply_data();
    void testMultiRegistrationFailureTerminatesReply();
    void testCrossThreadSubmitAndCancel();
    void testNoEventLoopFailFast();
    void testCrossThreadCookiesApis();
    void testCookieFutureFailureStates();
    void testCookieFutureCancellation();
    void testCookieFutureManagerDestroyed();
    void testCookieQFutureAsyncBridge();
    void testRunningRequestCountDropsOnFinish();
    void testCrossThreadSendFailsFast();
};

void tst_QCNetworkActorThreadModel::testMultiRegistrationFailureTerminatesReply_data()
{
    QTest::addColumn<QByteArray>("failurePoint");

    QTest::newRow("multi-init") << QByteArrayLiteral("init");
    QTest::newRow("multi-callbacks") << QByteArrayLiteral("callbacks");
    QTest::newRow("multi-add") << QByteArrayLiteral("add");
}

void tst_QCNetworkActorThreadModel::testMultiRegistrationFailureTerminatesReply()
{
    QFETCH(QByteArray, failurePoint);

    const QByteArray previousFailurePoint = qgetenv("QCURL_TEST_FORCE_MULTI_FAILURE");
    const auto restoreFailurePoint        = qScopeGuard([previousFailurePoint]() {
        if (previousFailurePoint.isNull()) {
            qunsetenv("QCURL_TEST_FORCE_MULTI_FAILURE");
        } else {
            qputenv("QCURL_TEST_FORCE_MULTI_FAILURE", previousFailurePoint);
        }
    });
    qputenv("QCURL_TEST_FORCE_MULTI_FAILURE", failurePoint);

    QThread actorThread;
    actorThread.start();
    QVERIFY(waitForEventDispatcher(&actorThread));

    auto *manager = new QCNetworkAccessManager();
    manager->moveToThread(&actorThread);
    QPointer<QCNetworkAccessManager> managerGuard(manager);
    const auto cleanup = qScopeGuard([&]() {
        if (managerGuard) {
            scheduleDeferredDeleteOnOwnerThread(managerGuard);
            TestWaitUtils::waitUntil([&managerGuard]() { return managerGuard.isNull(); }, 1000);
        }
        actorThread.quit();
        actorThread.wait();
    });

    const MultiRegistrationFailureResult result = runMultiRegistrationFailureScenario(manager);
    QVERIFY(result.replyCreated);
    QVERIFY(result.replyOwnedByManager);
    QVERIFY(!result.reply.isNull());
    QCOMPARE(result.state, ReplyState::Error);
    QCOMPARE(result.error, NetworkError::InvalidRequest);
    QCOMPARE(result.errorSignalCount, 1);
    QCOMPARE(result.finishedSignalCount, 1);
    QCOMPARE(result.activeReplyCount, 0);
    QCOMPARE(result.runningRequestCount, 0);

    QPointer<QCNetworkReply> replyGuard = result.reply;
    std::atomic_bool managerDestroyedOnOwnerThread{false};
    std::atomic_bool replyDestroyedOnOwnerThread{false};
    QObject::connect(
        manager,
        &QObject::destroyed,
        this,
        [&actorThread, &managerDestroyedOnOwnerThread]() {
            managerDestroyedOnOwnerThread.store(QThread::currentThread() == &actorThread);
        },
        Qt::DirectConnection);
    QObject::connect(
        result.reply,
        &QObject::destroyed,
        this,
        [&actorThread, &replyDestroyedOnOwnerThread]() {
            replyDestroyedOnOwnerThread.store(QThread::currentThread() == &actorThread);
        },
        Qt::DirectConnection);

    QVERIFY(scheduleDeferredDeleteOnOwnerThread(manager));
    QVERIFY(TestWaitUtils::waitUntil(
        [&managerGuard, &replyGuard]() { return managerGuard.isNull() && replyGuard.isNull(); },
        1000));
    QVERIFY(managerDestroyedOnOwnerThread.load());
    QVERIFY(replyDestroyedOnOwnerThread.load());
}

void tst_QCNetworkActorThreadModel::testCrossThreadSubmitAndCancel()
{
    QThread actorThread;
    actorThread.start();
    QVERIFY(waitForEventDispatcher(&actorThread));

    auto *manager = new QCNetworkAccessManager();
    manager->moveToThread(&actorThread);
    const auto cleanup = qScopeGuard([&]() {
        manager->deleteLater();
        actorThread.quit();
        actorThread.wait();
    });

    DelayedHttpServer server(400);
    QVERIFY2(server.start(),
             qPrintable(
                 QStringLiteral("Cannot bind local port for actor thread model test server: %1")
                     .arg(server.errorString())));

    QCNetworkRequest request(server.url(QStringLiteral("/test")));
    QCNetworkReply *reply = getOnOwnerThread(manager, request);
    QVERIFY(reply != nullptr);
    QCOMPARE(reply->thread(), &actorThread);
    QCOMPARE(reply->parent(), manager);
    QTRY_COMPARE_WITH_TIMEOUT(runningRequestsCountOnOwnerThread(manager), 1, 1500);

    QSignalSpy cancelledSpy(reply, &QCNetworkReply::cancelled);
    QMetaObject::invokeMethod(reply, [&]() { reply->cancel(); }, Qt::BlockingQueuedConnection);
    QTRY_COMPARE_WITH_TIMEOUT(cancelledSpy.count(), 1, 1500);
    QTRY_COMPARE_WITH_TIMEOUT(runningRequestsCountOnOwnerThread(manager), 0, 1500);

    NetworkError error = NetworkError::NoError;
    QMetaObject::invokeMethod(reply, [&]() { error = reply->error(); }, Qt::BlockingQueuedConnection);
    QCOMPARE(error, NetworkError::OperationCancelled);

    reply->deleteLater();
    server.stop();
}

void tst_QCNetworkActorThreadModel::testNoEventLoopFailFast()
{
    struct Result
    {
        bool hasReply      = false;
        bool finished      = false;
        NetworkError error = NetworkError::Unknown;
        QString errorString;
    } result;

    std::thread noLoopThread([&result]() {
        QCNetworkAccessManager manager;
        QCNetworkRequest request(QUrl(QStringLiteral("http://mock.local/no-event-loop")));
        QCNetworkReply *reply = manager.get(request);
        result.hasReply       = reply != nullptr;
        if (!reply) {
            return;
        }

        result.finished    = reply->isFinished();
        result.error       = reply->error();
        result.errorString = reply->errorString();
    });
    noLoopThread.join();

    QVERIFY(result.hasReply);
    QVERIFY(result.finished);
    QCOMPARE(result.error, NetworkError::InvalidRequest);
    QVERIFY(result.errorString.contains(QStringLiteral("事件循环")));
}

void tst_QCNetworkActorThreadModel::testCrossThreadCookiesApis()
{
    QThread actorThread;
    actorThread.start();
    QVERIFY(waitForEventDispatcher(&actorThread));

    auto *manager = new QCNetworkAccessManager();
    enableSharedCookies(manager);
    manager->moveToThread(&actorThread);
    const auto cleanup = qScopeGuard([&]() {
        manager->deleteLater();
        actorThread.quit();
        actorThread.wait();
    });

    QString error;
    QCCookie sid(QByteArrayLiteral("sid"), QByteArrayLiteral("123"));
    QVERIFY(!manager->importCookies({sid}, QUrl(QStringLiteral("http://example.local")), &error));
    QVERIFY(error.contains(QStringLiteral("owner")));

    error.clear();
    const auto cookies = manager->exportCookies(QUrl(QStringLiteral("http://example.local")),
                                                &error);
    QVERIFY(!cookies.has_value());
    QVERIFY(error.contains(QStringLiteral("owner")));

    error.clear();
    QVERIFY(!manager->clearAllCookies(&error));
    QVERIFY(error.contains(QStringLiteral("owner")));
}

void tst_QCNetworkActorThreadModel::testCookieFutureFailureStates()
{
    QCNetworkAccessManager metaObjectProbe;
    const QMetaObject *metaObject = metaObjectProbe.metaObject();
    QCOMPARE(metaObject->indexOfSignal("cookiesImported(QCurl::QCCookieOperationResult)"), -1);
    QCOMPARE(metaObject->indexOfSignal("cookiesExported(QCurl::QCCookieExportResult)"), -1);
    QCOMPARE(metaObject->indexOfSignal("cookiesCleared(QCurl::QCCookieOperationResult)"), -1);

    struct NoEventLoopResult
    {
        QCCookieOperationResult value;
        int resultCount = 0;
    } dispatch;

    std::thread noLoopThread([&dispatch]() {
        QCNetworkAccessManager manager;
        auto future = manager.clearAllCookiesAsync();
        future.waitForFinished();
        dispatch.resultCount = future.resultCount();
        dispatch.value       = future.result();
    });
    noLoopThread.join();

    QCOMPARE(dispatch.resultCount, 1);
    QCOMPARE(dispatch.value.errorCode(), QCCookieAsyncError::DispatchFailed);
    QVERIFY(!dispatch.value.isSuccess());

    QThread actorThread;
    actorThread.start();
    QVERIFY(waitForEventDispatcher(&actorThread));
    auto *manager = new QCNetworkAccessManager();
    manager->moveToThread(&actorThread);

    auto businessFuture = manager->clearAllCookiesAsync();
    businessFuture.waitForFinished();
    QCOMPARE(businessFuture.resultCount(), 1);
    QCOMPARE(businessFuture.result().errorCode(), QCCookieAsyncError::BusinessError);
    QCOMPARE(businessFuture.result().policyCode(), QStringLiteral("cookie.share_disabled"));

    manager->deleteLater();
    actorThread.quit();
    actorThread.wait();
}

void tst_QCNetworkActorThreadModel::testCookieFutureCancellation()
{
    QThread actorThread;
    actorThread.start();
    QVERIFY(waitForEventDispatcher(&actorThread));

    auto *manager = new QCNetworkAccessManager();
    enableSharedCookies(manager);
    manager->moveToThread(&actorThread);
    const auto cleanup = qScopeGuard([&]() {
        manager->deleteLater();
        actorThread.quit();
        actorThread.wait();
    });

    QSemaphore entered;
    QSemaphore release;
    QVERIFY(QMetaObject::invokeMethod(
        manager,
        [&]() {
            entered.release();
            release.acquire();
        },
        Qt::QueuedConnection));
    QVERIFY(entered.tryAcquire(1, 1000));

    QCCookie sid(QByteArrayLiteral("sid"), QByteArrayLiteral("cancelled"));
    auto future = manager->importCookiesAsync({sid},
                                              QUrl(QStringLiteral(
                                                  "http://example.local/cancelled")));
    future.cancel();
    release.release();
    future.waitForFinished();

    QCOMPARE(future.resultCount(), 1);
    QCOMPARE(future.result().errorCode(), QCCookieAsyncError::Cancelled);

    auto exportFuture = manager->exportCookiesAsync(
        QUrl(QStringLiteral("http://example.local/cancelled")));
    exportFuture.waitForFinished();
    QVERIFY(exportFuture.result().isSuccess());
    QVERIFY(exportFuture.result().cookies().isEmpty());
}

void tst_QCNetworkActorThreadModel::testCookieFutureManagerDestroyed()
{
    QThread actorThread;
    actorThread.start();
    QVERIFY(waitForEventDispatcher(&actorThread));

    auto *manager = new QCNetworkAccessManager();
    enableSharedCookies(manager);
    manager->moveToThread(&actorThread);
    QPointer<QCNetworkAccessManager> managerGuard(manager);
    const auto cleanup = qScopeGuard([&]() {
        actorThread.quit();
        actorThread.wait();
    });

    QVERIFY(
        QMetaObject::invokeMethod(manager, [manager]() { delete manager; }, Qt::QueuedConnection));
    auto future = manager->clearAllCookiesAsync();
    future.waitForFinished();

    QTRY_VERIFY_WITH_TIMEOUT(managerGuard.isNull(), 1000);
    QCOMPARE(future.resultCount(), 1);
    QCOMPARE(future.result().errorCode(), QCCookieAsyncError::ManagerDestroyed);
}

void tst_QCNetworkActorThreadModel::testCookieQFutureAsyncBridge()
{
    QThread actorThread;
    actorThread.start();
    QVERIFY(waitForEventDispatcher(&actorThread));

    auto *manager = new QCNetworkAccessManager();
    enableSharedCookies(manager);
    manager->moveToThread(&actorThread);
    const auto cleanup = qScopeGuard([&]() {
        manager->deleteLater();
        actorThread.quit();
        actorThread.wait();
    });

    QCCookie sid(QByteArrayLiteral("sid"), QByteArrayLiteral("future"));
    auto importFuture = manager->importCookiesAsync({sid},
                                                    QUrl(QStringLiteral("http://example.local")));
    importFuture.waitForFinished();
    QVERIFY(importFuture.result().isSuccess());
    QCOMPARE(importFuture.result().errorCode(), QCCookieAsyncError::None);
    QCOMPARE(importFuture.resultCount(), 1);

    auto exportFuture = manager->exportCookiesAsync(QUrl(QStringLiteral("http://example.local/")));
    exportFuture.waitForFinished();
    const auto exportResult = exportFuture.result();
    QVERIFY(exportResult.isSuccess());
    QCOMPARE(exportResult.errorCode(), QCCookieAsyncError::None);
    QCOMPARE(exportFuture.resultCount(), 1);
    QVERIFY(exportResult.error().isEmpty());
    QVERIFY(
        hasCookie(exportResult.cookies(), QByteArrayLiteral("sid"), QByteArrayLiteral("future")));

    auto clearFuture = manager->clearAllCookiesAsync();
    clearFuture.waitForFinished();
    QVERIFY(clearFuture.result().isSuccess());
    QCOMPARE(clearFuture.resultCount(), 1);

    auto emptyFuture = manager->exportCookiesAsync(QUrl(QStringLiteral("http://example.local/")));
    emptyFuture.waitForFinished();
    QVERIFY(emptyFuture.result().isSuccess());
    QCOMPARE(emptyFuture.resultCount(), 1);
    QVERIFY(emptyFuture.result().cookies().isEmpty());
}

void tst_QCNetworkActorThreadModel::testRunningRequestCountDropsOnFinish()
{
    QThread actorThread;
    actorThread.start();
    QVERIFY(waitForEventDispatcher(&actorThread));

    auto *manager = new QCNetworkAccessManager();
    manager->moveToThread(&actorThread);
    const auto cleanup = qScopeGuard([&]() {
        manager->deleteLater();
        actorThread.quit();
        actorThread.wait();
    });

    DelayedHttpServer server(250);
    QVERIFY2(server.start(),
             qPrintable(
                 QStringLiteral("Cannot bind local port for actor thread model test server: %1")
                     .arg(server.errorString())));

    QCNetworkRequest request(server.url(QStringLiteral("/finish-count")));
    QCNetworkReply *reply = getOnOwnerThread(manager, request);
    QVERIFY(reply != nullptr);
    QCOMPARE(reply->thread(), &actorThread);

    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
    QTRY_COMPARE_WITH_TIMEOUT(runningRequestsCountOnOwnerThread(manager), 1, 1500);
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(runningRequestsCountOnOwnerThread(manager), 0, 1500);

    NetworkError error = NetworkError::Unknown;
    QMetaObject::invokeMethod(reply, [&]() { error = reply->error(); }, Qt::BlockingQueuedConnection);
    QCOMPARE(error, NetworkError::NoError);

    reply->deleteLater();
    server.stop();
}

void tst_QCNetworkActorThreadModel::testCrossThreadSendFailsFast()
{
    QThread actorThread;
    actorThread.start();
    QVERIFY(waitForEventDispatcher(&actorThread));

    auto *manager = new QCNetworkAccessManager();
    manager->moveToThread(&actorThread);
    const auto cleanup = qScopeGuard([&]() {
        manager->deleteLater();
        actorThread.quit();
        actorThread.wait();
    });

    QCNetworkRequest request(QUrl(QStringLiteral("http://example.local/cross-thread")));
    QCNetworkReply *reply = manager->get(request);
    QVERIFY(reply != nullptr);
    QCOMPARE(reply->thread(), QThread::currentThread());
    QCOMPARE(reply->parent(), nullptr);
    QVERIFY(reply->isFinished());
    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QVERIFY(reply->errorString().contains(QStringLiteral("owner")));
    QVERIFY(reply->errorString().contains(QStringLiteral("Blocking Extras")));

    reply->deleteLater();
}

QTEST_MAIN(tst_QCNetworkActorThreadModel)

#include "tst_QCNetworkActorThreadModel.moc"
