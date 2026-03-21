// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkAccessManager.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRequestPriority.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QEvent>
#include <QtTest>

#include <chrono>

using namespace QCurl;

class TestQCNetworkRequestPipeline : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testSendGetAndSchedulerShareDigest();
    void testAsyncAndSyncPostShareDigest();
    void testDigestReflectsBodyKinds();

private:
    QByteArray planDigest(QCNetworkReply *reply) const;

    QCNetworkAccessManager *m_manager = nullptr;
    QCNetworkMockHandler m_mock;
};

void TestQCNetworkRequestPipeline::init()
{
    m_manager = new QCNetworkAccessManager(this);
    m_manager->enableRequestScheduler(false);

    m_mock.clear();
    m_mock.clearCapturedRequests();
    m_mock.setCaptureEnabled(true);
    m_mock.setGlobalDelay(0);
    m_manager->setMockHandler(&m_mock);
}

void TestQCNetworkRequestPipeline::cleanup()
{
    if (m_manager) {
        m_manager->deleteLater();
        m_manager = nullptr;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

QByteArray TestQCNetworkRequestPipeline::planDigest(QCNetworkReply *reply) const
{
    if (!reply) {
        QTest::qFail("reply should not be null", __FILE__, __LINE__);
        return QByteArray();
    }

    const QByteArray digest = Internal::testCurlPlanDigest(reply);
    if (digest.isEmpty()) {
        QTest::qFail("plan digest should be available in test builds", __FILE__, __LINE__);
        return QByteArray();
    }
    return digest;
}

void TestQCNetworkRequestPipeline::testSendGetAndSchedulerShareDigest()
{
    const QUrl url("http://example.com/pipeline-get");
    m_mock.enqueueResponse(HttpMethod::Get, url, QByteArray("OK-1"));
    m_mock.enqueueResponse(HttpMethod::Get, url, QByteArray("OK-2"));

    QCNetworkRequest request(url);
    request.setRawHeader("X-Trace-Id", "pipeline-get")
        .setTimeout(std::chrono::milliseconds(1500))
        .setFollowLocation(true)
        .setPriority(QCNetworkRequestPriority::High);

    m_manager->enableRequestScheduler(false);
    auto *directReply = m_manager->sendGet(request);
    const QByteArray directDigest = planDigest(directReply);
    QTRY_VERIFY_WITH_TIMEOUT(directReply->isFinished(), 2000);
    QCOMPARE(directReply->error(), NetworkError::NoError);
    directReply->deleteLater();

    m_manager->enableRequestScheduler(true);
    auto *scheduledReply = m_manager->sendGet(request);
    const QByteArray scheduledDigest = planDigest(scheduledReply);
    QCOMPARE(scheduledDigest, directDigest);
    QTRY_VERIFY_WITH_TIMEOUT(scheduledReply->isFinished(), 2000);
    QCOMPARE(scheduledReply->error(), NetworkError::NoError);
    scheduledReply->deleteLater();
}

void TestQCNetworkRequestPipeline::testAsyncAndSyncPostShareDigest()
{
    const QUrl url("http://example.com/pipeline-post");
    const QByteArray body(R"({"hello":"world"})");
    m_mock.enqueueResponse(HttpMethod::Post, url, QByteArray("ASYNC"));
    m_mock.enqueueResponse(HttpMethod::Post, url, QByteArray("SYNC"));

    QCNetworkRequest request(url);
    request.setRawHeader("Content-Type", "application/json")
        .setTimeout(std::chrono::seconds(2))
        .setPriority(QCNetworkRequestPriority::Normal);

    auto *asyncReply = m_manager->sendPost(request, body);
    const QByteArray asyncDigest = planDigest(asyncReply);
    QTRY_VERIFY_WITH_TIMEOUT(asyncReply->isFinished(), 2000);
    QCOMPARE(asyncReply->error(), NetworkError::NoError);
    asyncReply->deleteLater();

    auto *syncReply = m_manager->sendPostSync(request, body);
    const QByteArray syncDigest = planDigest(syncReply);
    QCOMPARE(syncDigest, asyncDigest);
    QVERIFY(syncReply->isFinished());
    QCOMPARE(syncReply->error(), NetworkError::NoError);
    syncReply->deleteLater();
}

void TestQCNetworkRequestPipeline::testDigestReflectsBodyKinds()
{
    const QUrl inlineUrl("http://example.com/pipeline-inline");
    const QUrl deviceUrl("http://example.com/pipeline-device");
    m_mock.mockResponse(HttpMethod::Post, inlineUrl, QByteArray("INLINE"));
    m_mock.mockResponse(HttpMethod::Post, deviceUrl, QByteArray("DEVICE"));

    QCNetworkRequest inlineRequest(inlineUrl);
    auto *inlineReply = m_manager->sendPost(inlineRequest, QByteArray("inline-body"));
    const QByteArray inlineDigest = planDigest(inlineReply);
    QVERIFY(inlineDigest.contains("body_kind=inline"));
    QTRY_VERIFY_WITH_TIMEOUT(inlineReply->isFinished(), 2000);
    QCOMPARE(inlineReply->error(), NetworkError::NoError);
    inlineReply->deleteLater();

    QBuffer deviceBuffer;
    deviceBuffer.setData(QByteArray("device-body"));
    QVERIFY(deviceBuffer.open(QIODevice::ReadOnly));

    QCNetworkRequest deviceRequest(deviceUrl);
    deviceRequest.setUploadDevice(&deviceBuffer, deviceBuffer.size());
    auto *deviceReply = m_manager->sendPost(deviceRequest, QByteArray());
    const QByteArray deviceDigest = planDigest(deviceReply);
    QVERIFY(deviceDigest.contains("body_kind=device"));
    QVERIFY(deviceDigest != inlineDigest);
    QTRY_VERIFY_WITH_TIMEOUT(deviceReply->isFinished(), 2000);
    QCOMPARE(deviceReply->error(), NetworkError::NoError);
    deviceReply->deleteLater();
}

QTEST_MAIN(TestQCNetworkRequestPipeline)
#include "tst_QCNetworkRequestPipeline.moc"
