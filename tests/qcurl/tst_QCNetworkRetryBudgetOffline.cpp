/**
 * @file
 * @brief 离线验证重试预算与失败响应保留，不访问外部服务。
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRetryPolicy.h"
#include "private/QCNetworkReplyRuntime_p.h"
#include "private/QCNetworkRetryPolicy_p.h"
#include "qcnetwork_mock_test_support.h"
#include "qcnetwork_retry_policy_test_helper.h"

#include <QSignalSpy>
#include <QtTest>

using namespace QCurl;
using namespace std::chrono_literals;

class TestQCNetworkRetryBudgetOffline : public QObject
{
    Q_OBJECT

public:
    TestQCNetworkRetryBudgetOffline() = default;

private Q_SLOTS:
    void init();
    void cleanup();
    void budgetArithmetic_data();
    void budgetArithmetic();
    void budgetUpdatesAreTransactional();
    void retryAfterQuery_data();
    void retryAfterQuery();
    void cumulativeBudget_data();
    void cumulativeBudget();
    void elapsedBudgetIncludesTransferTime();
    void retryAfterCannotExceedBudget_data();
    void retryAfterCannotExceedBudget();
    void nonIdempotentRetryRequiresBothOptInAndKey_data();
    void nonIdempotentRetryRequiresBothOptInAndKey();

private:
    Q_DISABLE_COPY_MOVE(TestQCNetworkRetryBudgetOffline)

    QCNetworkAccessManager m_manager;
    QCNetworkMockHandler m_mock;
};

void TestQCNetworkRetryBudgetOffline::init()
{
    Internal::setRetryJitterFractionForTest(0.0);
    m_mock.clear();
    m_mock.setGlobalDelay(0);
    m_mock.setCaptureEnabled(true);
    m_mock.clearCapturedRequests();
    TestSupport::setMockHandler(m_manager, &m_mock);
}

void TestQCNetworkRetryBudgetOffline::cleanup()
{
    TestSupport::setMockHandler(m_manager, nullptr);
    Internal::setRetryJitterFractionForTest(std::nullopt);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void TestQCNetworkRetryBudgetOffline::budgetArithmetic_data()
{
    QTest::addColumn<qint64>("used");
    QTest::addColumn<qint64>("limit");
    QTest::addColumn<bool>("accepted");
    const auto max = std::chrono::milliseconds::max().count();
    QTest::newRow("used-over-limit") << qint64(10) << qint64(9) << false;
    QTest::newRow("overflow-rejected") << qint64(max - 3) << qint64(max) << false;
    QTest::newRow("exact-limit") << qint64(max - 5) << qint64(max) << true;
    QTest::newRow("unlimited-saturates") << qint64(max - 3) << qint64(-1) << true;
}

void TestQCNetworkRetryBudgetOffline::budgetArithmetic()
{
    QFETCH(qint64, used);
    QFETCH(qint64, limit);
    QFETCH(bool, accepted);
    auto policy = TestSupport::makeRetryPolicyOrFail(2, 10ms, 1.0, 10ms);
    if (limit >= 0) {
        QCOMPARE(policy.setMaxCumulativeDelay(std::chrono::milliseconds(limit)),
                 QCNetworkRetryPolicy::UpdateResult::Applied);
    }
    QCNetworkRequest request(QUrl(QStringLiteral("http://example.com/budget-arithmetic")));
    request.setRetryPolicy(policy);
    QCNetworkReplyPrivate state(nullptr,
                                request,
                                HttpMethod::Get,
                                Internal::makeEmptyRequestBody(),
                                {});
    state.cumulativeRetryDelay = std::chrono::milliseconds(used);
    static_cast<void>(
        Internal::advanceReplyRetryIfNeeded(&state, NetworkError::HttpServiceUnavailable));
    QCOMPARE(state.attemptCount, accepted ? 1 : 0);
    const auto expected = accepted ? std::chrono::milliseconds::max()
                                   : std::chrono::milliseconds(used);
    QCOMPARE(state.cumulativeRetryDelay, expected);
}

void TestQCNetworkRetryBudgetOffline::budgetUpdatesAreTransactional()
{
    QCNetworkRetryPolicy policy;
    QVERIFY(!policy.maxCumulativeDelay());
    QVERIFY(!policy.maxElapsedTime());
    QCOMPARE(policy.setMaxCumulativeDelay(10ms), QCNetworkRetryPolicy::UpdateResult::Applied);
    QCOMPARE(policy.setMaxElapsedTime(20ms), QCNetworkRetryPolicy::UpdateResult::Applied);
    QCNetworkRetryPolicy copy = policy;
    QCOMPARE(copy.setMaxCumulativeDelay(-1ms), QCNetworkRetryPolicy::UpdateResult::InvalidArgument);
    QCOMPARE(copy.setMaxElapsedTime(-1ms), QCNetworkRetryPolicy::UpdateResult::InvalidArgument);
    QCOMPARE(copy.maxCumulativeDelay().value(), 10ms);
    QCOMPARE(copy.maxElapsedTime().value(), 20ms);
    QCOMPARE(copy.setMaxCumulativeDelay(0ms), QCNetworkRetryPolicy::UpdateResult::Applied);
    QCOMPARE(copy.setMaxElapsedTime(std::nullopt), QCNetworkRetryPolicy::UpdateResult::Applied);
    QCOMPARE(copy.maxCumulativeDelay().value(), 0ms);
    QVERIFY(!copy.maxElapsedTime());
    QCOMPARE(policy.maxCumulativeDelay().value(), 10ms);
    QCOMPARE(policy.maxElapsedTime().value(), 20ms);
}

void TestQCNetworkRetryBudgetOffline::cumulativeBudget_data()
{
    QTest::addColumn<int>("budgetMs");
    QTest::addColumn<int>("expectedRetries");
    QTest::newRow("unlimited") << -1 << 2;
    QTest::newRow("zero") << 0 << 0;
    QTest::newRow("below-first-wait") << 4 << 0;
    QTest::newRow("exact-first-wait") << 5 << 1;
    QTest::newRow("below-second-wait") << 9 << 1;
    QTest::newRow("exact-second-wait") << 10 << 2;
}

void TestQCNetworkRetryBudgetOffline::cumulativeBudget()
{
    QFETCH(int, budgetMs);
    QFETCH(int, expectedRetries);
    const QUrl url(QStringLiteral("http://example.com/retry/cumulative-budget"));
    m_mock.mockResponse(HttpMethod::Get, url, QByteArrayLiteral("failure-body"), 503);
    auto policy = TestSupport::makeRetryPolicyOrFail(2, 10ms, 1.0, 10ms);
    if (budgetMs >= 0) {
        QCOMPARE(policy.setMaxCumulativeDelay(std::chrono::milliseconds(budgetMs)),
                 QCNetworkRetryPolicy::UpdateResult::Applied);
    }
    QCNetworkRequest request(url);
    request.setRetryPolicy(policy);
    auto *reply = m_manager.get(request);
    QSignalSpy retrySpy(reply, &QCNetworkReply::retryAttempt);
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
    QVERIFY(finishedSpy.wait(2000));
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(retrySpy.count(), expectedRetries);
    QCOMPARE(reply->retryAttemptCount(), expectedRetries);
    QCOMPARE(reply->cumulativeRetryDelay(), expectedRetries * 5ms);
    QCOMPARE(m_mock.capturedRequests().size(), expectedRetries + 1);
    QCOMPARE(reply->error(), NetworkError::HttpServiceUnavailable);
    QCOMPARE(reply->readAll().value(), QByteArrayLiteral("failure-body"));
    reply->deleteLater();
}

void TestQCNetworkRetryBudgetOffline::elapsedBudgetIncludesTransferTime()
{
    const QUrl url(QStringLiteral("http://example.com/retry/elapsed-budget"));
    m_mock.setGlobalDelay(30);
    m_mock.mockResponse(HttpMethod::Get, url, QByteArrayLiteral("slow-failure"), 503);
    auto policy = TestSupport::makeRetryPolicyOrFail(2, 0ms);
    QCOMPARE(policy.setMaxElapsedTime(1ms), QCNetworkRetryPolicy::UpdateResult::Applied);
    QCNetworkRequest request(url);
    request.setRetryPolicy(policy);
    auto *reply = m_manager.get(request);
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
    QVERIFY(finishedSpy.wait(2000));
    QCOMPARE(reply->retryAttemptCount(), 0);
    QCOMPARE(reply->cumulativeRetryDelay(), 0ms);
    QCOMPARE(m_mock.capturedRequests().size(), 1);
    QCOMPARE(reply->error(), NetworkError::HttpServiceUnavailable);
    QCOMPARE(reply->readAll().value(), QByteArrayLiteral("slow-failure"));
    reply->deleteLater();
}

void TestQCNetworkRetryBudgetOffline::retryAfterCannotExceedBudget_data()
{
    QTest::addColumn<bool>("cumulative");
    QTest::newRow("cumulative") << true;
    QTest::newRow("elapsed") << false;
}

void TestQCNetworkRetryBudgetOffline::retryAfterCannotExceedBudget()
{
    QFETCH(bool, cumulative);
    const QUrl url(QStringLiteral("http://example.com/retry/retry-after-budget"));
    m_mock.mockResponse(HttpMethod::Get,
                        url,
                        QByteArrayLiteral("limited"),
                        429,
                        {{QByteArrayLiteral("Retry-After"), QByteArrayLiteral("1")}});
    auto policy = TestSupport::makeRetryPolicyOrFail(2, 0ms, 1.0, 2s);
    QCOMPARE(cumulative ? policy.setMaxCumulativeDelay(500ms) : policy.setMaxElapsedTime(500ms),
             QCNetworkRetryPolicy::UpdateResult::Applied);
    QCNetworkRequest request(url);
    request.setRetryPolicy(policy);
    auto *reply = m_manager.get(request);
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
    QVERIFY(finishedSpy.wait(2000));
    QCOMPARE(reply->retryAttemptCount(), 0);
    QCOMPARE(reply->cumulativeRetryDelay(), 0ms);
    QCOMPARE(reply->retryAfterDelay().value(), 1s);
    QCOMPARE(m_mock.capturedRequests().size(), 1);
    QCOMPARE(reply->error(), NetworkError::HttpTooManyRequests);
    QCOMPARE(reply->readAll().value(), QByteArrayLiteral("limited"));
    reply->deleteLater();
}

void TestQCNetworkRetryBudgetOffline::nonIdempotentRetryRequiresBothOptInAndKey_data()
{
    QTest::addColumn<bool>("allow");
    QTest::addColumn<QByteArray>("key");
    QTest::addColumn<int>("retries");
    QTest::newRow("key-without-opt-in") << false << QByteArrayLiteral("stable") << 0;
    QTest::newRow("opt-in-without-key") << true << QByteArray() << 0;
    QTest::newRow("opt-in-with-blank-key") << true << QByteArrayLiteral("   ") << 0;
    QTest::newRow("opt-in-with-key") << true << QByteArrayLiteral("stable") << 1;
}

void TestQCNetworkRetryBudgetOffline::nonIdempotentRetryRequiresBothOptInAndKey()
{
    QFETCH(bool, allow);
    QFETCH(QByteArray, key);
    QFETCH(int, retries);
    const QUrl url(QStringLiteral("http://example.com/retry/idempotency"));
    m_mock.enqueueResponse(HttpMethod::Post, url, QByteArrayLiteral("fail"), 500);
    m_mock.enqueueResponse(HttpMethod::Post, url, QByteArrayLiteral("ok"), 200);
    auto policy = TestSupport::makeRetryPolicyOrFail(1, 0ms);
    if (allow) {
        QCOMPARE(policy.setRetryMethodPolicy(
                     QCNetworkRetryMethodPolicy::AllowExplicitIdempotencyKey),
                 QCNetworkRetryPolicy::UpdateResult::Applied);
    }
    QCNetworkRequest request(url);
    request.setRawHeader(QByteArrayLiteral("Idempotency-Key"), key);
    request.setRetryPolicy(policy);
    auto *reply = m_manager.post(request, QByteArrayLiteral("payload"));
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
    QVERIFY(finishedSpy.wait(2000));
    QCOMPARE(reply->retryAttemptCount(), retries);
    QCOMPARE(m_mock.capturedRequests().size(), retries + 1);
    QCOMPARE(reply->error(),
             retries ? NetworkError::NoError : NetworkError::HttpInternalServerError);
    reply->deleteLater();
}

void TestQCNetworkRetryBudgetOffline::retryAfterQuery_data()
{
    QTest::addColumn<QByteArray>("value");
    QTest::addColumn<qint64>("milliseconds");
    QTest::newRow("zero") << QByteArrayLiteral("0") << qint64(0);
    QTest::newRow("seconds") << QByteArrayLiteral("2") << qint64(2000);
    QTest::newRow("past-date") << QByteArrayLiteral("Thu, 01 Jan 1970 00:00:00 GMT") << qint64(0);
    QTest::newRow("negative") << QByteArrayLiteral("-1") << qint64(-1);
    QTest::newRow("missing") << QByteArray() << qint64(-1);
    QTest::newRow("invalid") << QByteArrayLiteral("invalid") << qint64(-1);
    QTest::newRow("overflow") << QByteArrayLiteral("9223372036854775807") << qint64(-1);
}

void TestQCNetworkRetryBudgetOffline::retryAfterQuery()
{
    QFETCH(QByteArray, value);
    QFETCH(qint64, milliseconds);
    const QUrl url(QStringLiteral("http://example.com/query"));
    const auto headers = QByteArrayLiteral("HTTP/1.1 100 Continue\r\nRetry-After: "
                                           "99\r\n\r\nHTTP/1.1 503 Unavailable\r\nrEtRy-AfTeR: ")
                         + value + QByteArrayLiteral("\r\n\r\n");
    m_mock.mockResponse(HttpMethod::Get, url, QByteArrayLiteral("fail"), 503, {}, headers);
    auto *reply = m_manager.get(QCNetworkRequest(url));
    QSignalSpy done(reply, &QCNetworkReply::finished);
    QVERIFY(done.wait(2000));
    if (milliseconds < 0) {
        QVERIFY(!reply->retryAfterDelay());
    } else {
        QCOMPARE(reply->retryAfterDelay().value().count(), milliseconds);
    }
    QCOMPARE(reply->retryAttemptCount(), 0);
    reply->deleteLater();
}

QTEST_MAIN(TestQCNetworkRetryBudgetOffline)
#include "tst_QCNetworkRetryBudgetOffline.moc"
