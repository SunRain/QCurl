/**
 * @file tst_QCNetworkRetryOffline.cpp
 * @brief 使用 MockHandler 的纯离线重试/限流语义门禁
 *
 * 目标：
 * - 不依赖 httpbin/docker/socket
 * - 覆盖 HTTP 5xx 重试、HTTP 429 + Retry-After 覆写、无 Retry-After 回退指数退避
 * - 覆盖 opt-in 的 GET-only HTTP 状态码重试限制
 *
 */

#include <QtTest/QtTest>
#include <QElapsedTimer>
#include <QSignalSpy>

#include "QCNetworkAccessManager.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRetryPolicy.h"

using namespace QCurl;

class TestQCNetworkRetryOffline : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testRetry500Then200();
    void testRetry501Then200();
    void testRetry503Exceeded();
    void testRetry429RetryAfterOverride();
    void testRetry429RetryAfterHttpDateOverride();
    void testRetry429FallbackToBackoff();
    void testRetryHttpStatusGetOnlyGating();

private:
    QCNetworkAccessManager *m_manager = nullptr;
    QCNetworkMockHandler m_mock;
};

void TestQCNetworkRetryOffline::init()
{
    m_manager = new QCNetworkAccessManager(this);
    m_manager->setMockHandler(&m_mock);
    m_mock.clear();
    m_mock.setGlobalDelay(0);
}

void TestQCNetworkRetryOffline::cleanup()
{
    if (m_manager) {
        m_manager->setMockHandler(nullptr);
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
    QCNetworkRetryPolicy policy;
    policy.maxRetries = 1;
    policy.initialDelay = std::chrono::milliseconds(10);
    policy.backoffMultiplier = 2.0;
    policy.maxDelay = std::chrono::milliseconds(200);
    request.setRetryPolicy(policy);

    auto *reply = m_manager->sendGet(request);
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
    QCNetworkRetryPolicy policy;
    policy.maxRetries = 1;
    policy.initialDelay = std::chrono::milliseconds(10);
    policy.backoffMultiplier = 2.0;
    policy.maxDelay = std::chrono::milliseconds(200);
    request.setRetryPolicy(policy);

    auto *reply = m_manager->sendGet(request);
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
    QCNetworkRetryPolicy policy;
    policy.maxRetries = 2;
    policy.initialDelay = std::chrono::milliseconds(10);
    policy.backoffMultiplier = 1.2;
    policy.maxDelay = std::chrono::milliseconds(200);
    request.setRetryPolicy(policy);

    auto *reply = m_manager->sendGet(request);
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
    headers.insert("Retry-After", "999");  // 999s，期望被 maxDelay cap
    m_mock.enqueueResponse(HttpMethod::Get, url, QByteArray("rate limited"), 429, headers);
    m_mock.enqueueResponse(HttpMethod::Get, url, QByteArray("ok"), 200);

    QCNetworkRequest request(url);
    QCNetworkRetryPolicy policy;
    policy.maxRetries = 1;
    policy.initialDelay = std::chrono::milliseconds(1);
    policy.backoffMultiplier = 2.0;
    policy.maxDelay = std::chrono::milliseconds(50);
    request.setRetryPolicy(policy);

    QElapsedTimer timer;
    timer.start();

    auto *reply = m_manager->sendGet(request);
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
    headers.insert("Retry-After", "Thu, 31 Dec 2037 23:59:59 GMT");  // HTTP-date，期望被 maxDelay cap
    m_mock.enqueueResponse(HttpMethod::Get, url, QByteArray("rate limited"), 429, headers);
    m_mock.enqueueResponse(HttpMethod::Get, url, QByteArray("ok"), 200);

    QCNetworkRequest request(url);
    QCNetworkRetryPolicy policy;
    policy.maxRetries = 1;
    policy.initialDelay = std::chrono::milliseconds(1);
    policy.backoffMultiplier = 2.0;
    policy.maxDelay = std::chrono::milliseconds(50);
    request.setRetryPolicy(policy);

    QElapsedTimer timer;
    timer.start();

    auto *reply = m_manager->sendGet(request);
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
    QCNetworkRetryPolicy policy;
    policy.maxRetries = 1;
    policy.initialDelay = std::chrono::milliseconds(40);
    policy.backoffMultiplier = 2.0;
    policy.maxDelay = std::chrono::milliseconds(200);
    request.setRetryPolicy(policy);

    QElapsedTimer timer;
    timer.start();

    auto *reply = m_manager->sendGet(request);
    QSignalSpy retrySpy(reply, &QCNetworkReply::retryAttempt);
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);

    QVERIFY(finishedSpy.wait(2000));
    QVERIFY(timer.elapsed() >= 25);
    QCOMPARE(retrySpy.count(), 1);
    QCOMPARE(reply->error(), NetworkError::NoError);

    reply->deleteLater();
}

void TestQCNetworkRetryOffline::testRetryHttpStatusGetOnlyGating()
{
    const QUrl url("http://example.com/offline/retry/get_only_gating");
    m_mock.enqueueResponse(HttpMethod::Post, url, QByteArray("fail"), 500);
    m_mock.enqueueResponse(HttpMethod::Post, url, QByteArray("ok"), 200);

    QCNetworkRequest request(url);
    QCNetworkRetryPolicy policy;
    policy.maxRetries = 1;
    policy.initialDelay = std::chrono::milliseconds(10);
    policy.retryHttpStatusErrorsForGetOnly = true;
    request.setRetryPolicy(policy);

    auto *reply = m_manager->sendPost(request, QByteArray("x"));
    QSignalSpy retrySpy(reply, &QCNetworkReply::retryAttempt);
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);

    QVERIFY(finishedSpy.wait(2000));
    QCOMPARE(retrySpy.count(), 0);
    QCOMPARE(reply->error(), NetworkError::HttpInternalServerError);

    reply->deleteLater();
}

QTEST_MAIN(TestQCNetworkRetryOffline)
#include "tst_QCNetworkRetryOffline.moc"
