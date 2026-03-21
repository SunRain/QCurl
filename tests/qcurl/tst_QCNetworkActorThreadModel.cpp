// SPDX-License-Identifier: MIT
// Copyright (c) 2026 QCurl Project

#include "../src/QCNetworkAccessManager.h"
#include "../src/QCCurlMultiManager.h"
#include "../src/QCNetworkReply.h"
#include "../src/QCNetworkRequest.h"

#include <QAbstractEventDispatcher>
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

QCNetworkReply *sendGetOnOwnerThread(QCNetworkAccessManager *manager, const QCNetworkRequest &request)
{
    if (!manager) {
        return nullptr;
    }

    if (QThread::currentThread() == manager->thread()) {
        return manager->sendGet(request);
    }

    QCNetworkReply *reply = nullptr;
    const bool invoked = QMetaObject::invokeMethod(
        manager, [manager, request, &reply]() { reply = manager->sendGet(request); }, Qt::BlockingQueuedConnection);
    return invoked ? reply : nullptr;
}

bool waitForEventDispatcher(QThread *thread, int timeoutMs = 1000)
{
    if (!thread) {
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    while (QAbstractEventDispatcher::instance(thread) == nullptr && timer.elapsed() < timeoutMs) {
        QTest::qWait(10);
    }

    return QAbstractEventDispatcher::instance(thread) != nullptr;
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
    void testRunningRequestCountDropsOnFinish();
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
    if (!server.start()) {
        const QByteArray skipReason = QStringLiteral(
                                          "Cannot bind local port for actor thread model test server: %1")
                                          .arg(server.errorString())
                                          .toUtf8();
        QSKIP(skipReason.constData());
    }

    QCNetworkRequest request(server.url(QStringLiteral("/test")));
    QCNetworkReply *reply = sendGetOnOwnerThread(manager, request);
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
    QThread noLoopThread;

    auto *manager = new QCNetworkAccessManager();
    manager->moveToThread(&noLoopThread);

    const QUrl url(QStringLiteral("http://mock.local/no-event-loop"));
    QCNetworkRequest request(url);

    QCNetworkReply *reply = manager->sendGet(request);
    QVERIFY(reply != nullptr);
    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QVERIFY(reply->errorString().contains(QStringLiteral("事件循环")));

    reply->deleteLater();

    // noLoopThread 未启动时，deleteLater 不会执行；通过启动线程处理 deferred delete，再退出线程。
    QObject::connect(manager,
                     &QObject::destroyed,
                     &noLoopThread,
                     &QThread::quit,
                     Qt::DirectConnection);
    noLoopThread.start();
    manager->deleteLater();
    QVERIFY(noLoopThread.wait(3000));
}

void tst_QCNetworkActorThreadModel::testCrossThreadCookiesApis()
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

    QString error;
    const auto cookies = manager->exportCookies(QUrl(QStringLiteral("http://example.local")),
                                                &error);
    QVERIFY(cookies.isEmpty());
    QVERIFY(!error.isEmpty());

    error.clear();
    QVERIFY(!manager->clearAllCookies(&error));
    QVERIFY(!error.isEmpty());

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
    if (!server.start()) {
        const QByteArray skipReason = QStringLiteral(
                                          "Cannot bind local port for actor thread model test server: %1")
                                          .arg(server.errorString())
                                          .toUtf8();
        QSKIP(skipReason.constData());
    }

    QCNetworkRequest request(server.url(QStringLiteral("/finish-count")));
    QCNetworkReply *reply = manager->sendGet(request);
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

QTEST_MAIN(tst_QCNetworkActorThreadModel)

#include "tst_QCNetworkActorThreadModel.moc"
