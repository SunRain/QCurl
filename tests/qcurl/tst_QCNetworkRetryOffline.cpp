/**
 * @file tst_QCNetworkRetryOffline.cpp
 * @brief 使用 MockHandler 的纯离线重试/限流语义门禁
 *
 * 目标：
 * - 不依赖 httpbin/docker/socket
 * - 覆盖 HTTP 5xx 重试、HTTP 429 + Retry-After 覆写、无 Retry-After 回退指数退避
 * - 覆盖统一 method/idempotency retry gate
 *
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRetryPolicy.h"
#include "private/QCNetworkRetryPolicy_p.h"
#include "qcnetwork_mock_test_support.h"
#include "qcnetwork_retry_policy_test_helper.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QtTest/QtTest>

using namespace QCurl;

class TestQCNetworkRetryOffline : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void testRetry500Then200();
    void testRetry501Then200();
    void testRetry503Exceeded();
    void testRetry429RetryAfterOverride();
    void testRetry429RetryAfterHttpDateOverride();
    void testRetry429FallbackToBackoff();
    void testNonIdempotentHttpErrorRequiresIdempotencyKey();
    void testNonIdempotentNetworkErrorRequiresIdempotencyKey();
    void testExplicitIdempotencyKeyAllowsHttpAndNetworkRetry();
    void testCancelDuringRetryDelay();

private:
    QCNetworkAccessManager *m_manager = nullptr;
    QCNetworkMockHandler m_mock;
};

void TestQCNetworkRetryOffline::init()
{
    m_manager = new QCNetworkAccessManager(this);
    Internal::setRetryJitterFractionForTest(0.0);
    QCurl::TestSupport::setMockHandler(*m_manager, &m_mock);
    m_mock.clear();
    m_mock.setCaptureEnabled(true);
    m_mock.clearCapturedRequests();
    m_mock.setGlobalDelay(0);
}

void TestQCNetworkRetryOffline::cleanup()
{
    Internal::setRetryJitterFractionForTest(std::nullopt);
    if (m_manager) {
        QCurl::TestSupport::setMockHandler(*m_manager, nullptr);
        m_manager->deleteLater();
        m_manager = nullptr;
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void TestQCNetworkRetryOffline::testRetry500Then200()
{
    const QUrl url("http://example.com/offline/retry/500_then_200");
    m_mock.enqueueResponse(HttpMethod::Get, url, QByteArray("fail"), 500);
    m_mock.enqueueResponse(HttpMethod::Get, url, QByteArray("ok"), 200);

    QCNetworkRequest request(url);
    const QCNetworkRetryPolicy policy = TestSupport::makeRetryPolicyOrFail(
        1, std::chrono::milliseconds(10), 2.0, std::chrono::milliseconds(200));
    request.setRetryPolicy(policy);

    auto *reply = m_manager->get(request);
    QSignalSpy retrySpy(reply, &QCNetworkReply::retryAttempt);
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);

    QVERIFY(finishedSpy.wait(2000));
    QCOMPARE(retrySpy.count(), 1);
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->readAll().value_or(QByteArray()), QByteArray("ok"));

    reply->deleteLater();
}

void TestQCNetworkRetryOffline::testRetry501Then200()
{
    const QUrl url("http://example.com/offline/retry/501_then_200");
    m_mock.enqueueResponse(HttpMethod::Get, url, QByteArray("fail"), 501);
    m_mock.enqueueResponse(HttpMethod::Get, url, QByteArray("ok"), 200);

    QCNetworkRequest request(url);
    const QCNetworkRetryPolicy policy = TestSupport::makeRetryPolicyOrFail(
        1, std::chrono::milliseconds(10), 2.0, std::chrono::milliseconds(200));
    request.setRetryPolicy(policy);

    auto *reply = m_manager->get(request);
    QSignalSpy retrySpy(reply, &QCNetworkReply::retryAttempt);
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);

    QVERIFY(finishedSpy.wait(2000));
    QCOMPARE(retrySpy.count(), 1);
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->readAll().value_or(QByteArray()), QByteArray("ok"));

    reply->deleteLater();
}

void TestQCNetworkRetryOffline::testRetry503Exceeded()
{
    const QUrl url("http://example.com/offline/retry/503_exceeded");
    m_mock.mockResponse(HttpMethod::Get, url, QByteArray("svc down"), 503);

    QCNetworkRequest request(url);
    const QCNetworkRetryPolicy policy = TestSupport::makeRetryPolicyOrFail(
        2, std::chrono::milliseconds(10), 1.2, std::chrono::milliseconds(200));
    request.setRetryPolicy(policy);

    auto *reply = m_manager->get(request);
    QSignalSpy retrySpy(reply, &QCNetworkReply::retryAttempt);
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);

    QVERIFY(finishedSpy.wait(2000));
    QCOMPARE(retrySpy.count(), 2);
    QCOMPARE(reply->error(), NetworkError::HttpServiceUnavailable);

    reply->deleteLater();
}

void TestQCNetworkRetryOffline::testRetry429RetryAfterOverride()
{
    const QUrl url("http://example.com/offline/retry/429_retry_after");

    QMap<QByteArray, QByteArray> headers;
    headers.insert("Retry-After", "999"); // 999s，期望被 maxDelay cap
    m_mock.enqueueResponse(HttpMethod::Get, url, QByteArray("rate limited"), 429, headers);
    m_mock.enqueueResponse(HttpMethod::Get, url, QByteArray("ok"), 200);

    QCNetworkRequest request(url);
    const QCNetworkRetryPolicy policy = TestSupport::makeRetryPolicyOrFail(
        1, std::chrono::milliseconds(1), 2.0, std::chrono::milliseconds(50));
    request.setRetryPolicy(policy);

    QElapsedTimer timer;
    timer.start();

    auto *reply = m_manager->get(request);
    QSignalSpy retrySpy(reply, &QCNetworkReply::retryAttempt);
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);

    QVERIFY(finishedSpy.wait(2000));
    QVERIFY(timer.elapsed() >= 30);
    QCOMPARE(retrySpy.count(), 1);
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->readAll().value_or(QByteArray()), QByteArray("ok"));

    reply->deleteLater();
}

void TestQCNetworkRetryOffline::testRetry429RetryAfterHttpDateOverride()
{
    const QUrl url("http://example.com/offline/retry/429_retry_after_http_date");

    QMap<QByteArray, QByteArray> headers;
    headers.insert("Retry-After", "Thu, 31 Dec 2037 23:59:59 GMT"); // HTTP-date，期望被 maxDelay cap
    m_mock.enqueueResponse(HttpMethod::Get, url, QByteArray("rate limited"), 429, headers);
    m_mock.enqueueResponse(HttpMethod::Get, url, QByteArray("ok"), 200);

    QCNetworkRequest request(url);
    const QCNetworkRetryPolicy policy = TestSupport::makeRetryPolicyOrFail(
        1, std::chrono::milliseconds(1), 2.0, std::chrono::milliseconds(50));
    request.setRetryPolicy(policy);

    QElapsedTimer timer;
    timer.start();

    auto *reply = m_manager->get(request);
    QSignalSpy retrySpy(reply, &QCNetworkReply::retryAttempt);
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);

    QVERIFY(finishedSpy.wait(2000));
    QVERIFY(timer.elapsed() >= 30);
    QCOMPARE(retrySpy.count(), 1);
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->readAll().value_or(QByteArray()), QByteArray("ok"));

    reply->deleteLater();
}

void TestQCNetworkRetryOffline::testRetry429FallbackToBackoff()
{
    const QUrl url("http://example.com/offline/retry/429_fallback");
    m_mock.enqueueResponse(HttpMethod::Get, url, QByteArray("rate limited"), 429);
    m_mock.enqueueResponse(HttpMethod::Get, url, QByteArray("ok"), 200);

    QCNetworkRequest request(url);
    const QCNetworkRetryPolicy policy = TestSupport::makeRetryPolicyOrFail(
        1, std::chrono::milliseconds(40), 2.0, std::chrono::milliseconds(200));
    request.setRetryPolicy(policy);

    QElapsedTimer timer;
    timer.start();

    auto *reply = m_manager->get(request);
    QSignalSpy retrySpy(reply, &QCNetworkReply::retryAttempt);
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);

    QVERIFY(finishedSpy.wait(2000));
    // equal-jitter 的确定性样本 0 取 base/2（20 ms）；为事件循环调度保留少量余量。
    QVERIFY(timer.elapsed() >= 15);
    QCOMPARE(retrySpy.count(), 1);
    QCOMPARE(reply->error(), NetworkError::NoError);

    reply->deleteLater();
}

void TestQCNetworkRetryOffline::testNonIdempotentHttpErrorRequiresIdempotencyKey()
{
    const QUrl url("http://example.com/offline/retry/non_idempotent_http");
    m_mock.enqueueResponse(HttpMethod::Post, url, QByteArray("fail"), 500);
    m_mock.enqueueResponse(HttpMethod::Post, url, QByteArray("ok"), 200);

    QCNetworkRequest request(url);
    const QCNetworkRetryPolicy policy
        = TestSupport::makeRetryPolicyOrFail(1, std::chrono::milliseconds(10));
    request.setRetryPolicy(policy);

    auto *reply = m_manager->post(request, QByteArray("x"));
    QSignalSpy retrySpy(reply, &QCNetworkReply::retryAttempt);
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);

    QVERIFY(finishedSpy.wait(2000));
    QCOMPARE(retrySpy.count(), 0);
    QCOMPARE(reply->error(), NetworkError::HttpInternalServerError);
    QCOMPARE(m_mock.capturedRequests().size(), 1);

    reply->deleteLater();
}

void TestQCNetworkRetryOffline::testNonIdempotentNetworkErrorRequiresIdempotencyKey()
{
    const QUrl url("http://example.com/offline/retry/non_idempotent_network");
    m_mock.enqueueError(HttpMethod::Post, url, NetworkError::ConnectionRefused);
    m_mock.enqueueResponse(HttpMethod::Post, url, QByteArray("ok"), 200);

    QCNetworkRequest request(url);
    const QCNetworkRetryPolicy policy
        = TestSupport::makeRetryPolicyOrFail(1, std::chrono::milliseconds(1));
    request.setRetryPolicy(policy);

    auto *reply = m_manager->post(request, QByteArrayLiteral("payload"));
    QSignalSpy retrySpy(reply, &QCNetworkReply::retryAttempt);
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);

    QVERIFY(finishedSpy.wait(2000));
    QCOMPARE(retrySpy.count(), 0);
    QCOMPARE(reply->error(), NetworkError::ConnectionRefused);
    QCOMPARE(m_mock.capturedRequests().size(), 1);

    reply->deleteLater();
}

void TestQCNetworkRetryOffline::testExplicitIdempotencyKeyAllowsHttpAndNetworkRetry()
{
    const QUrl httpUrl("http://example.com/offline/retry/explicit_http");
    m_mock.enqueueResponse(HttpMethod::Patch, httpUrl, QByteArray("fail"), 503);
    m_mock.enqueueResponse(HttpMethod::Patch, httpUrl, QByteArray("ok"), 200);

    QCNetworkRequest httpRequest(httpUrl);
    httpRequest.setRawHeader(QByteArrayLiteral("Idempotency-Key"), QByteArrayLiteral("stable-1"));
    QCNetworkRetryPolicy httpPolicy
        = TestSupport::makeRetryPolicyOrFail(1, std::chrono::milliseconds(1));
    QCOMPARE(httpPolicy.setRetryMethodPolicy(
                 QCNetworkRetryMethodPolicy::AllowExplicitIdempotencyKey),
             QCNetworkRetryPolicy::UpdateResult::Applied);
    httpRequest.setRetryPolicy(httpPolicy);

    auto *httpReply = m_manager->patch(httpRequest, QByteArrayLiteral("payload"));
    QSignalSpy httpRetrySpy(httpReply, &QCNetworkReply::retryAttempt);
    QSignalSpy httpFinishedSpy(httpReply, &QCNetworkReply::finished);
    QVERIFY(httpFinishedSpy.wait(2000));
    QCOMPARE(httpRetrySpy.count(), 1);
    QCOMPARE(httpReply->error(), NetworkError::NoError);
    QCOMPARE(httpReply->readAll().value_or(QByteArray()), QByteArrayLiteral("ok"));

    const QUrl networkUrl("http://example.com/offline/retry/explicit_network");
    m_mock.enqueueError(HttpMethod::Patch, networkUrl, NetworkError::ConnectionRefused);
    m_mock.enqueueResponse(HttpMethod::Patch, networkUrl, QByteArray("ok"), 200);

    QCNetworkRequest networkRequest(networkUrl);
    networkRequest.setRawHeader(QByteArrayLiteral("idempotency-key"), QByteArrayLiteral("stable-2"));
    QCNetworkRetryPolicy networkPolicy
        = TestSupport::makeRetryPolicyOrFail(1, std::chrono::milliseconds(1));
    QCOMPARE(networkPolicy.setRetryMethodPolicy(
                 QCNetworkRetryMethodPolicy::AllowExplicitIdempotencyKey),
             QCNetworkRetryPolicy::UpdateResult::Applied);
    networkRequest.setRetryPolicy(networkPolicy);

    auto *networkReply = m_manager->patch(networkRequest, QByteArrayLiteral("payload"));
    QSignalSpy networkRetrySpy(networkReply, &QCNetworkReply::retryAttempt);
    QSignalSpy networkFinishedSpy(networkReply, &QCNetworkReply::finished);
    QVERIFY(networkFinishedSpy.wait(2000));
    QCOMPARE(networkRetrySpy.count(), 1);
    QCOMPARE(networkReply->error(), NetworkError::NoError);
    QCOMPARE(networkReply->readAll().value_or(QByteArray()), QByteArrayLiteral("ok"));
    QCOMPARE(m_mock.capturedRequests().size(), 4);

    httpReply->deleteLater();
    networkReply->deleteLater();
}

void TestQCNetworkRetryOffline::testCancelDuringRetryDelay()
{
    const QUrl url("http://example.com/offline/retry/cancel_during_delay");

    // 只提供 1 次失败响应：若取消后仍发生 retry，会触发 “no mock matched” 并把状态覆盖为 Error。
    m_mock.enqueueResponse(HttpMethod::Get, url, QByteArray("svc down"), 503);

    QCNetworkRequest request(url);
    const QCNetworkRetryPolicy policy = TestSupport::makeRetryPolicyOrFail(
        3, std::chrono::milliseconds(500), 1.0, std::chrono::milliseconds(500));
    request.setRetryPolicy(policy);

    auto *reply = m_manager->get(request);
    QSignalSpy retrySpy(reply, &QCNetworkReply::retryAttempt);
    QSignalSpy cancelledSpy(reply, &QCNetworkReply::cancelled);
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);

    if (retrySpy.count() == 0) {
        QVERIFY(retrySpy.wait(2000));
    }
    QCOMPARE(retrySpy.count(), 1);
    QCOMPARE(retrySpy.at(0).at(0).toInt(), 1);

    reply->cancel();

    if (cancelledSpy.count() == 0) {
        QVERIFY(cancelledSpy.wait(2000));
    }
    QCOMPARE(reply->state(), ReplyState::Cancelled);
    QCOMPARE(reply->error(), NetworkError::OperationCancelled);
    QCOMPARE(finishedSpy.count(), 1);

    // 等待超过 initialDelay，确保不会再次触发 execute()/consumeMock。
    QTest::qWait(static_cast<int>((policy.initialDelay() * 2).count()));
    QCOMPARE(retrySpy.count(), 1);
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(reply->state(), ReplyState::Cancelled);
    QCOMPARE(reply->error(), NetworkError::OperationCancelled);

    reply->deleteLater();
}

QTEST_MAIN(TestQCNetworkRetryOffline)
#include "tst_QCNetworkRetryOffline.moc"
