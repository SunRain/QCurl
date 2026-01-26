// SPDX-License-Identifier: MIT
// Copyright (c) 2026 QCurl Project

#include "../src/QCNetworkAccessManager.h"
#include "../src/QCNetworkMockHandler.h"
#include "../src/QCNetworkReply.h"
#include "../src/QCNetworkRequest.h"

#include <QSignalSpy>
#include <QThread>
#include <QtTest>

using namespace QCurl;

class tst_QCNetworkActorThreadModel : public QObject
{
    Q_OBJECT

private slots:
    void testCrossThreadSubmitAndCancel();
    void testNoEventLoopFailFast();
    void testCrossThreadCookiesApis();
};

void tst_QCNetworkActorThreadModel::testCrossThreadSubmitAndCancel()
{
    QThread actorThread;
    actorThread.start();

    auto *manager = new QCNetworkAccessManager();
    manager->moveToThread(&actorThread);

    QCNetworkMockHandler mock;
    mock.setGlobalDelay(200);

    const QUrl url(QStringLiteral("http://mock.local/test"));
    mock.mockResponse(HttpMethod::Get, url, QByteArrayLiteral("ok"), 200);

    QMetaObject::invokeMethod(
        manager, [&]() { manager->setMockHandler(&mock); }, Qt::BlockingQueuedConnection);

    QCNetworkRequest request(url);
    QCNetworkReply *reply = manager->sendGet(request);
    QVERIFY(reply != nullptr);
    QCOMPARE(reply->thread(), &actorThread);
    QCOMPARE(reply->parent(), manager);

    QSignalSpy cancelledSpy(reply, &QCNetworkReply::cancelled);
    reply->cancel();
    QVERIFY(cancelledSpy.wait(1500));

    NetworkError error = NetworkError::NoError;
    QMetaObject::invokeMethod(reply, [&]() { error = reply->error(); }, Qt::BlockingQueuedConnection);
    QCOMPARE(error, NetworkError::OperationCancelled);

    reply->deleteLater();

    QMetaObject::invokeMethod(
        manager, [&]() { manager->setMockHandler(nullptr); }, Qt::BlockingQueuedConnection);

    manager->deleteLater();

    actorThread.quit();
    actorThread.wait();
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

    auto *manager = new QCNetworkAccessManager();
    manager->moveToThread(&actorThread);

    QString error;
    const auto cookies = manager->exportCookies(QUrl(QStringLiteral("http://example.local")),
                                                &error);
    QVERIFY(cookies.isEmpty());
    QVERIFY(!error.isEmpty());

    error.clear();
    QVERIFY(!manager->clearAllCookies(&error));
    QVERIFY(!error.isEmpty());

    manager->deleteLater();
    actorThread.quit();
    actorThread.wait();
}

QTEST_MAIN(tst_QCNetworkActorThreadModel)

#include "tst_QCNetworkActorThreadModel.moc"
