// SPDX-License-Identifier: MIT
// Copyright (c) 2026 QCurl Project

#include "../src/QCNetworkAccessManager.h"
#include "../src/QCCurlMultiManager.h"
#include "../src/QCNetworkReply.h"
#include "../src/QCNetworkRequest.h"
#include "test_wait_utils.h"

#include <QAbstractEventDispatcher>
#include <QFuture>
#include <QHostAddress>
#include <QPointer>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QtTest>

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

    int count = -1;
    const bool invoked = QMetaObject::invokeMethod(
        manager,
        [&count]() { count = QCCurlMultiManager::instance()->runningRequestsCount(); },
        Qt::BlockingQueuedConnection);
    return invoked ? count : -1;
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
    const bool invoked = QMetaObject::invokeMethod(
        manager, [manager, request, &reply]() { reply = manager->get(request); }, Qt::BlockingQueuedConnection);
    return invoked ? reply : nullptr;
}

bool waitForEventDispatcher(QThread *thread, int timeoutMs = 1000)
{
    if (!thread) {
        return false;
    }

    return TestWaitUtils::waitUntil(
        [thread]() { return QAbstractEventDispatcher::instance(thread) != nullptr; },
        timeoutMs);
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
    explicit DelayedHttpServer(int responseDelayMs, QByteArray responseBody = QByteArrayLiteral("ok"))
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

                        const QByteArray response
                            = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Length: ")
                              + QByteArray::number(m_responseBody.size())
                              + QByteArrayLiteral("\r\nConnection: close\r\n\r\n")
                              + m_responseBody;
                        safeSocket->write(response);
                        safeSocket->flush();
                        safeSocket->disconnectFromHost();
                    });
                });
                QObject::connect(
                    socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
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

private slots:
    void testCrossThreadSubmitAndCancel();
    void testNoEventLoopFailFast();
    void testCrossThreadCookiesApis();
    void testCookieSignalAsyncBridge();
    void testCookieQFutureAsyncBridge();
    void testRunningRequestCountDropsOnFinish();
    void testCrossThreadSendFailsFast();
};

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
             qPrintable(QStringLiteral("Cannot bind local port for actor thread model test server: %1")
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
        bool hasReply = false;
        bool finished = false;
        NetworkError error = NetworkError::Unknown;
        QString errorString;
    } result;

    std::thread noLoopThread([&result]() {
        QCNetworkAccessManager manager;
        QCNetworkRequest request(QUrl(QStringLiteral("http://mock.local/no-event-loop")));
        QCNetworkReply *reply = manager.get(request);
        result.hasReply = reply != nullptr;
        if (!reply) {
            return;
        }

        result.finished = reply->isFinished();
        result.error = reply->error();
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

void tst_QCNetworkActorThreadModel::testCookieSignalAsyncBridge()
{
    qRegisterMetaType<QCurl::QCCookieOperationResult>();
    qRegisterMetaType<QCurl::QCCookieExportResult>();

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

    QCCookieOperationResult importResult;
    bool importSignalReceived = false;
    QObject::connect(manager,
                     &QCNetworkAccessManager::cookiesImported,
                     this,
                     [&](const QCCookieOperationResult &result) {
                         importResult = result;
                         importSignalReceived = true;
                     });

    QCCookie sid(QByteArrayLiteral("sid"), QByteArrayLiteral("signal"));
    auto importFuture =
        manager->importCookiesAsync({sid}, QUrl(QStringLiteral("http://example.local")));
    QTRY_VERIFY_WITH_TIMEOUT(importSignalReceived, 1500);
    QVERIFY(importResult.isSuccess());
    QVERIFY(importResult.error().isEmpty());
    importFuture.waitForFinished();
    QVERIFY(importFuture.result().isSuccess());

    QCCookieExportResult exportResult;
    bool exportSignalReceived = false;
    QObject::connect(manager,
                     &QCNetworkAccessManager::cookiesExported,
                     this,
                     [&](const QCCookieExportResult &result) {
                         exportResult = result;
                         exportSignalReceived = true;
                     });

    auto exportFuture = manager->exportCookiesAsync(QUrl(QStringLiteral("http://example.local/")));
    QTRY_VERIFY_WITH_TIMEOUT(exportSignalReceived, 1500);
    QVERIFY(exportResult.isSuccess());
    QVERIFY(exportResult.error().isEmpty());
    QVERIFY(hasCookie(exportResult.cookies(), QByteArrayLiteral("sid"), QByteArrayLiteral("signal")));
    exportFuture.waitForFinished();
    QVERIFY(exportFuture.result().isSuccess());
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
    auto importFuture =
        manager->importCookiesAsync({sid}, QUrl(QStringLiteral("http://example.local")));
    importFuture.waitForFinished();
    QVERIFY(importFuture.result().isSuccess());

    auto exportFuture = manager->exportCookiesAsync(QUrl(QStringLiteral("http://example.local/")));
    exportFuture.waitForFinished();
    const auto exportResult = exportFuture.result();
    QVERIFY(exportResult.isSuccess());
    QVERIFY(exportResult.error().isEmpty());
    QVERIFY(hasCookie(exportResult.cookies(), QByteArrayLiteral("sid"), QByteArrayLiteral("future")));

    auto clearFuture = manager->clearAllCookiesAsync();
    clearFuture.waitForFinished();
    QVERIFY(clearFuture.result().isSuccess());

    auto emptyFuture = manager->exportCookiesAsync(QUrl(QStringLiteral("http://example.local/")));
    emptyFuture.waitForFinished();
    QVERIFY(emptyFuture.result().isSuccess());
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
             qPrintable(QStringLiteral("Cannot bind local port for actor thread model test server: %1")
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
