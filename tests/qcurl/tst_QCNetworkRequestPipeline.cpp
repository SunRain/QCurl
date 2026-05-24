// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkAccessManager.h"
#include "QCNetworkMockHandler.h"
#include "qcnetwork_mock_test_support.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRequestPriority.h"
#include "private/QCRequestPipeline_p.h"

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
    void testAsyncAndCompiledPostShareDigest();
    void testDigestIgnoresRuntimeBodySource();

private:
    QByteArray planDigest(QCNetworkReply *reply) const;
    QByteArray compiledPlanDigest(const QCNetworkRequest &request,
                                  HttpMethod method,
                                  const QByteArray &body) const;

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
    QCurl::TestSupport::setMockHandler(*m_manager, &m_mock);
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

QByteArray TestQCNetworkRequestPipeline::compiledPlanDigest(const QCNetworkRequest &request,
                                                           HttpMethod method,
                                                           const QByteArray &body) const
{
    const Internal::RequestBody requestBody = body.isEmpty()
        ? Internal::makeEmptyRequestBody()
        : Internal::makeInlineRequestBody(body);
    const Internal::NormalizedRequest normalized =
        Internal::normalizeRequest(request, method, requestBody);
    const Internal::CurlPlan plan = Internal::compileRequest(normalized);
    return Internal::buildCurlPlanDigestForTest(plan);
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
    auto *directReply = m_manager->get(request);
    const QByteArray directDigest = planDigest(directReply);
    QTRY_VERIFY_WITH_TIMEOUT(directReply->isFinished(), 2000);
    QCOMPARE(directReply->error(), NetworkError::NoError);
    directReply->deleteLater();

    m_manager->enableRequestScheduler(true);
    auto *scheduledReply = m_manager->get(request);
    const QByteArray scheduledDigest = planDigest(scheduledReply);
    QCOMPARE(scheduledDigest, directDigest);
    QTRY_VERIFY_WITH_TIMEOUT(scheduledReply->isFinished(), 2000);
    QCOMPARE(scheduledReply->error(), NetworkError::NoError);
    scheduledReply->deleteLater();
}

void TestQCNetworkRequestPipeline::testAsyncAndCompiledPostShareDigest()
{
    const QUrl url("http://example.com/pipeline-post");
    const QByteArray body(R"({"hello":"world"})");
    m_mock.enqueueResponse(HttpMethod::Post, url, QByteArray("ASYNC"));

    QCNetworkRequest request(url);
    request.setRawHeader("Content-Type", "application/json")
        .setTimeout(std::chrono::seconds(2))
        .setPriority(QCNetworkRequestPriority::Normal);

    auto *asyncReply = m_manager->post(request, body);
    const QByteArray asyncDigest = planDigest(asyncReply);
    QTRY_VERIFY_WITH_TIMEOUT(asyncReply->isFinished(), 2000);
    QCOMPARE(asyncReply->error(), NetworkError::NoError);
    asyncReply->deleteLater();

    const QByteArray syncDigest = compiledPlanDigest(request, HttpMethod::Post, body);
    QCOMPARE(syncDigest, asyncDigest);
}

void TestQCNetworkRequestPipeline::testDigestIgnoresRuntimeBodySource()
{
    const QUrl url("http://example.com/pipeline-body-source");
    m_mock.mockResponse(HttpMethod::Post, url, QByteArray("INLINE"));
    m_mock.mockResponse(HttpMethod::Post, url, QByteArray("DEVICE"));

    QCNetworkRequest inlineRequest(url);
    auto *inlineReply = m_manager->post(inlineRequest, QByteArray("inline-body"));
    const QByteArray inlineDigest = planDigest(inlineReply);
    QTRY_VERIFY_WITH_TIMEOUT(inlineReply->isFinished(), 2000);
    QCOMPARE(inlineReply->error(), NetworkError::NoError);
    inlineReply->deleteLater();

    QBuffer deviceBuffer;
    deviceBuffer.setData(QByteArray("device-body"));
    QVERIFY(deviceBuffer.open(QIODevice::ReadOnly));

    QCNetworkRequest deviceRequest(url);
    auto *deviceReply = m_manager->post(deviceRequest, &deviceBuffer, deviceBuffer.size());
    const QByteArray deviceDigest = planDigest(deviceReply);
    QCOMPARE(deviceDigest, inlineDigest);
    QTRY_VERIFY_WITH_TIMEOUT(deviceReply->isFinished(), 2000);
    QCOMPARE(deviceReply->error(), NetworkError::NoError);
    deviceReply->deleteLater();
}

QTEST_MAIN(TestQCNetworkRequestPipeline)
#include "tst_QCNetworkRequestPipeline.moc"
