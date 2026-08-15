/**
 * @file tst_QCNetworkRetry.cpp
 * @brief QCNetworkRetryPolicy 和请求重试机制单元测试
 *
 * 测试覆盖：
 * - QCNetworkRetryPolicy 配置和工厂方法
 * - 指数退避算法延迟计算
 * - shouldRetry() 重试判断逻辑
 * - 异步请求自动重试行为
 * - 达到最大重试次数后的处理
 * - 不可重试错误（404 等）的处理
 * - 重试期间取消请求
 * - 自定义重试策略
 *
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkError.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRetryPolicy.h"
#include "private/QCNetworkRetryDecision_p.h"
#include "private/QCNetworkRetryPolicy_p.h"
#include "qcnetwork_managed_reply_wait_helper.h"
#include "test_httpbin_env.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QMetaMethod>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest/QtTest>

using namespace QCurl;

class TestQCNetworkRetry : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // ========== QCNetworkRetryPolicy 配置测试 ==========
    void testRetryPolicyDefaults();
    void testRetryPolicyFactoryMethods();
    void testRetryPolicyValidatedFactoryAndCopyDetach();
    void testRetryPolicyDelayCalculation();
    void testRetryPolicyShouldRetry();
    void testRetryPolicyRejectsInvalidValues();
    void testRetryPolicyValidatedFactory();
    void testRetryDecisionUsesUnifiedMethodGate();

    // ========== 实际重试行为测试 ==========
    void testNoRetry();            // 验证默认不重试
    void testBasicRetry();         // 基本重试成功场景
    void testMaxRetriesExceeded(); // 达到最大重试次数
    void testNonRetryableError();  // 404 等不可重试错误
    void testExponentialBackoff(); // 延迟时间验证
    void testCancelDuringRetry();  // 取消重试中的请求
    void testCustomRetryPolicy();  // 自定义策略
    void testManagedWaitRetry();   // managed reply wait helper 重试

private:
    QCNetworkAccessManager *m_manager = nullptr;
    QString m_httpbinBaseUrl;

    // 辅助方法
    bool waitForSignal(QObject *obj, const QMetaMethod &signal, int timeout = 10000);
    QCNetworkRequest createRequestWithRetry(const QUrl &url, int maxRetries);
};

// ============================================================================
// 测试初始化和清理
// ============================================================================

void TestQCNetworkRetry::initTestCase()
{
    m_httpbinBaseUrl = TestEnv::httpbinBaseUrl();
    QVERIFY2(!m_httpbinBaseUrl.isEmpty(), qPrintable(TestEnv::httpbinMissingReason()));
    qDebug() << "httpbin 服务地址:" << m_httpbinBaseUrl;

    // 创建网络管理器
    m_manager = new QCNetworkAccessManager(this);

    // 验证 httpbin 服务可用
    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/get"));
    auto *reply = m_manager->get(request);

    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 5000));
    QVERIFY2(reply->error() == NetworkError::NoError,
             qPrintable(TestEnv::httpbinUnavailableReason(m_httpbinBaseUrl, reply->errorString())));

    reply->deleteLater();
}

void TestQCNetworkRetry::cleanupTestCase()
{
    m_manager = nullptr;
}

void TestQCNetworkRetry::init()
{
    Internal::setRetryJitterFractionForTest(0.0);
}

void TestQCNetworkRetry::cleanup()
{
    Internal::setRetryJitterFractionForTest(std::nullopt);
}

// ============================================================================
// 辅助方法实现
// ============================================================================

bool TestQCNetworkRetry::waitForSignal(QObject *obj, const QMetaMethod &signal, int timeout)
{
    if (!obj) {
        return false;
    }

    if (auto *reply = qobject_cast<QCNetworkReply *>(obj)) {
        if (signal == QMetaMethod::fromSignal(&QCNetworkReply::finished) && reply->isFinished()) {
            return true;
        }
        if (signal == QMetaMethod::fromSignal(&QCNetworkReply::cancelled)
            && reply->state() == ReplyState::Cancelled) {
            return true;
        }
    }

    QSignalSpy spy(obj, signal);
    return spy.wait(timeout);
}

QCNetworkRequest TestQCNetworkRetry::createRequestWithRetry(const QUrl &url, int maxRetries)
{
    QCNetworkRequest request(url);
    QCNetworkRetryPolicy policy;
    const auto result = QCNetworkRetryPolicy::tryCreate(maxRetries,
                                                        std::chrono::milliseconds(100),
                                                        1.5,
                                                        &policy);
    if (result != QCNetworkRetryPolicy::UpdateResult::Applied
        || policy.setMaxDelay(std::chrono::milliseconds(5000))
               != QCNetworkRetryPolicy::UpdateResult::Applied) {
        qFatal("测试重试策略配置无效");
    }
    request.setRetryPolicy(policy);
    return request;
}

namespace {

static void verifyRetryAttemptSignals(const QSignalSpy &retrySpy, NetworkError expectedError)
{
    for (int i = 0; i < retrySpy.count(); ++i) {
        const QList<QVariant> args = retrySpy.at(i);
        QVERIFY(args.size() >= 2);
        QCOMPARE(args.at(0).toInt(), i + 1);
        QCOMPARE(qvariant_cast<NetworkError>(args.at(1)), expectedError);
    }
}

} // namespace

// ============================================================================
// QCNetworkRetryPolicy 配置测试
// ============================================================================

void TestQCNetworkRetry::testRetryPolicyDefaults()
{
    QCNetworkRetryPolicy policy;

    // 验证默认值
    QCOMPARE(policy.maxRetries(), 0);
    QCOMPARE(policy.initialDelay().count(), 1000);
    QCOMPARE(policy.backoffMultiplier(), 2.0);
    QCOMPARE(policy.maxDelay().count(), 30000);
    QCOMPARE(policy.retryableErrors().size(), 10); // 10 种可重试错误
    QVERIFY(policy.retryableErrors().contains(NetworkError::HttpNotImplemented));
}

void TestQCNetworkRetry::testRetryPolicyFactoryMethods()
{
    // 测试 noRetry()
    QCNetworkRetryPolicy noRetry = QCNetworkRetryPolicy::noRetry();
    QCOMPARE(noRetry.maxRetries(), 0);

    // 测试 standardRetry()
    QCNetworkRetryPolicy standard = QCNetworkRetryPolicy::standardRetry();
    QCOMPARE(standard.maxRetries(), 3);
    QCOMPARE(standard.initialDelay().count(), 1000);
    QCOMPARE(standard.backoffMultiplier(), 2.0);

    // 测试 aggressiveRetry()
    QCNetworkRetryPolicy aggressive = QCNetworkRetryPolicy::aggressiveRetry();
    QCOMPARE(aggressive.maxRetries(), 5);
    QCOMPARE(aggressive.initialDelay().count(), 500);
    QCOMPARE(aggressive.backoffMultiplier(), 1.5);
}

void TestQCNetworkRetry::testRetryPolicyValidatedFactoryAndCopyDetach()
{
    QCNetworkRetryPolicy policy;
    QCOMPARE(QCNetworkRetryPolicy::tryCreate(2,
                                             std::chrono::milliseconds(250),
                                             1.5,
                                             &policy),
             QCNetworkRetryPolicy::UpdateResult::Applied);
    QCOMPARE(policy.maxRetries(), 2);
    QCOMPARE(policy.initialDelay().count(), 250);
    QCOMPARE(policy.backoffMultiplier(), 1.5);

    QCNetworkRetryPolicy copied(policy);
    QCOMPARE(policy.setMaxRetries(5), QCNetworkRetryPolicy::UpdateResult::Applied);
    QCOMPARE(policy.setInitialDelay(std::chrono::seconds(1)),
             QCNetworkRetryPolicy::UpdateResult::Applied);

    QCOMPARE(copied.maxRetries(), 2);
    QCOMPARE(copied.initialDelay().count(), 250);
}

void TestQCNetworkRetry::testRetryPolicyDelayCalculation()
{
    QCNetworkRetryPolicy policy;
    const auto applied = QCNetworkRetryPolicy::UpdateResult::Applied;
    QCOMPARE(policy.setInitialDelay(std::chrono::milliseconds(1000)), applied);
    QCOMPARE(policy.setBackoffMultiplier(2.0), applied);
    QCOMPARE(policy.setMaxDelay(std::chrono::milliseconds(10000)), applied);

    // Equal-jitter starts at half of the bounded exponential delay.
    QCOMPARE(policy.delayForAttempt(0).count(), 500);
    QCOMPARE(policy.delayForAttempt(1).count(), 1000);
    QCOMPARE(policy.delayForAttempt(2).count(), 2000);
    QCOMPARE(policy.delayForAttempt(3).count(), 4000);
    QCOMPARE(policy.delayForAttempt(4).count(), 5000);

    Internal::setRetryJitterFractionForTest(1.0);
    QCOMPARE(policy.delayForAttempt(0).count(), 1000);
    QCOMPARE(policy.delayForAttempt(1).count(), 2000);
    QCOMPARE(policy.delayForAttempt(2).count(), 4000);
    QCOMPARE(policy.delayForAttempt(3).count(), 8000);
    QCOMPARE(policy.delayForAttempt(4).count(), 10000);

    Internal::setRetryJitterFractionForTest(0.0);
    QCOMPARE(policy.delayForAttempt(0, std::chrono::milliseconds(750)).count(), 750);
    QCOMPARE(policy.delayForAttempt(0, std::chrono::milliseconds(50000)).count(), 10000);
}

void TestQCNetworkRetry::testRetryPolicyShouldRetry()
{
    QCNetworkRetryPolicy policy;
    QCOMPARE(policy.setMaxRetries(3), QCNetworkRetryPolicy::UpdateResult::Applied);
    policy.setRetryableErrors({NetworkError::ConnectionTimeout, NetworkError::HttpServiceUnavailable});

    // 测试可重试错误
    QVERIFY(policy.shouldRetry(NetworkError::ConnectionTimeout, 0));
    QVERIFY(policy.shouldRetry(NetworkError::HttpServiceUnavailable, 1));

    // 测试不可重试错误
    QVERIFY(!policy.shouldRetry(NetworkError::HttpNotFound, 0));
    QVERIFY(!policy.shouldRetry(NetworkError::HttpBadRequest, 0));

    // 测试达到最大重试次数
    QVERIFY(!policy.shouldRetry(NetworkError::ConnectionTimeout, 3));
    QVERIFY(!policy.shouldRetry(NetworkError::ConnectionTimeout, 4));

    // 测试 maxRetries = 0 的情况
    QCOMPARE(policy.setMaxRetries(0), QCNetworkRetryPolicy::UpdateResult::Applied);
    QVERIFY(!policy.shouldRetry(NetworkError::ConnectionTimeout, 0));
}

void TestQCNetworkRetry::testRetryPolicyRejectsInvalidValues()
{
    QCNetworkRetryPolicy policy;
    using UpdateResult = QCNetworkRetryPolicy::UpdateResult;

    QCOMPARE(policy.setMaxRetries(-1), UpdateResult::InvalidArgument);
    QCOMPARE(policy.maxRetries(), 0);
    QCOMPARE(policy.setMaxRetries(2), UpdateResult::Applied);

    QCOMPARE(policy.setInitialDelay(std::chrono::milliseconds(-1)),
             UpdateResult::InvalidArgument);
    QCOMPARE(policy.initialDelay().count(), 1000);
    QCOMPARE(policy.setMaxDelay(std::chrono::milliseconds(-1)), UpdateResult::InvalidArgument);
    QCOMPARE(policy.maxDelay().count(), 30000);

    QCOMPARE(policy.setBackoffMultiplier(-1.0), UpdateResult::InvalidArgument);
    QCOMPARE(policy.setBackoffMultiplier(std::numeric_limits<double>::quiet_NaN()),
             UpdateResult::InvalidArgument);
    QCOMPARE(policy.setBackoffMultiplier(std::numeric_limits<double>::infinity()),
             UpdateResult::InvalidArgument);
    QCOMPARE(policy.backoffMultiplier(), 2.0);

    QCOMPARE(policy.setInitialDelay(std::chrono::milliseconds(1)), UpdateResult::Applied);
    QCOMPARE(policy.setMaxDelay(std::chrono::milliseconds::max()), UpdateResult::Applied);
    QCOMPARE(policy.setBackoffMultiplier(2.0), UpdateResult::Applied);
    Internal::setRetryJitterFractionForTest(1.0);
    const auto bounded = policy.delayForAttempt(std::numeric_limits<int>::max());
    QCOMPARE(bounded, std::chrono::milliseconds::max());
}

void TestQCNetworkRetry::testRetryPolicyValidatedFactory()
{
    using UpdateResult = QCNetworkRetryPolicy::UpdateResult;

    QCNetworkRetryPolicy output = QCNetworkRetryPolicy::aggressiveRetry();
    const QCNetworkRetryPolicy original = output;

    QCOMPARE(QCNetworkRetryPolicy::tryCreate(-1,
                                             std::chrono::milliseconds(250),
                                             1.5,
                                             &output),
             UpdateResult::InvalidArgument);
    QCOMPARE(output.maxRetries(), original.maxRetries());
    QCOMPARE(output.initialDelay(), original.initialDelay());
    QCOMPARE(output.backoffMultiplier(), original.backoffMultiplier());

    QCOMPARE(QCNetworkRetryPolicy::tryCreate(2,
                                             std::chrono::milliseconds(-1),
                                             1.5,
                                             &output),
             UpdateResult::InvalidArgument);
    QCOMPARE(output.maxRetries(), original.maxRetries());

    QCOMPARE(QCNetworkRetryPolicy::tryCreate(2,
                                             std::chrono::milliseconds(250),
                                             std::numeric_limits<double>::quiet_NaN(),
                                             &output),
             UpdateResult::InvalidArgument);
    QCOMPARE(output.maxRetries(), original.maxRetries());

    QCOMPARE(QCNetworkRetryPolicy::tryCreate(2,
                                             std::chrono::milliseconds(250),
                                             1.5,
                                             nullptr),
             UpdateResult::InvalidArgument);

    QCOMPARE(QCNetworkRetryPolicy::tryCreate(2,
                                             std::chrono::milliseconds(250),
                                             1.5,
                                             &output),
             UpdateResult::Applied);
    QCOMPARE(output.maxRetries(), 2);
    QCOMPARE(output.initialDelay(), std::chrono::milliseconds(250));
    QCOMPARE(output.backoffMultiplier(), 1.5);
}

void TestQCNetworkRetry::testRetryDecisionUsesUnifiedMethodGate()
{
    QCNetworkRetryPolicy policy;
    QCOMPARE(policy.setMaxRetries(1), QCNetworkRetryPolicy::UpdateResult::Applied);

    const QCNetworkRequest getRequest(QUrl(QStringLiteral("http://example.com/get")));
    const QCNetworkRequest postRequest(QUrl(QStringLiteral("http://example.com/post")));

    QVERIFY(Internal::QCNetworkRetryDecision::evaluate(policy,
                                                       HttpMethod::Get,
                                                       getRequest,
                                                       true,
                                                       NetworkError::HttpServiceUnavailable,
                                                       0)
                .allowed);
    QVERIFY(!Internal::QCNetworkRetryDecision::evaluate(policy,
                                                        HttpMethod::Post,
                                                        postRequest,
                                                        true,
                                                        NetworkError::HttpServiceUnavailable,
                                                        0)
                 .allowed);
    QVERIFY(!Internal::QCNetworkRetryDecision::evaluate(policy,
                                                        HttpMethod::Post,
                                                        postRequest,
                                                        true,
                                                        NetworkError::ConnectionRefused,
                                                        0)
                 .allowed);

    QCOMPARE(policy.setRetryMethodPolicy(QCNetworkRetryMethodPolicy::AllowExplicitIdempotencyKey),
             QCNetworkRetryPolicy::UpdateResult::Applied);
    QVERIFY(!Internal::QCNetworkRetryDecision::evaluate(policy,
                                                        HttpMethod::Post,
                                                        postRequest,
                                                        true,
                                                        NetworkError::ConnectionRefused,
                                                        0)
                 .allowed);

    QCNetworkRequest keyedRequest = postRequest;
    keyedRequest.setRawHeader(QByteArrayLiteral("idempotency-key"), QByteArrayLiteral("stable-1"));
    QVERIFY(Internal::QCNetworkRetryDecision::evaluate(policy,
                                                       HttpMethod::Post,
                                                       keyedRequest,
                                                       true,
                                                       NetworkError::ConnectionRefused,
                                                       0)
                .allowed);
    QVERIFY(!Internal::QCNetworkRetryDecision::evaluate(policy,
                                                        HttpMethod::Post,
                                                        keyedRequest,
                                                        false,
                                                        NetworkError::ConnectionRefused,
                                                        0)
                 .allowed);
}

// ============================================================================
// 实际重试行为测试
// ============================================================================

void TestQCNetworkRetry::testNoRetry()
{
    // 验证默认不重试行为
    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/status/503"));
    // 不设置 retryPolicy，使用默认的 noRetry()

    auto *reply = m_manager->get(request);
    QSignalSpy retrySpy(reply, &QCNetworkReply::retryAttempt);

    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 5000));

    // 验证：没有重试
    QCOMPARE(retrySpy.count(), 0);
    QCOMPARE(reply->error(), NetworkError::HttpServiceUnavailable);

    reply->deleteLater();
}

void TestQCNetworkRetry::testBasicRetry()
{
    // 基本重试场景：503 错误重试 3 次
    QCNetworkRequest request = createRequestWithRetry(QUrl(m_httpbinBaseUrl + "/status/503"), 3);

    auto *reply = m_manager->get(request);
    QSignalSpy retrySpy(reply, &QCNetworkReply::retryAttempt);

    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000));

    // 验证：重试了 3 次
    QCOMPARE(retrySpy.count(), 3);

    // 验证：最终仍然失败（503 错误）
    QCOMPARE(reply->error(), NetworkError::HttpServiceUnavailable);

    // 验证信号参数
    for (int i = 0; i < retrySpy.count(); ++i) {
        auto args          = retrySpy.at(i);
        int attemptCount   = args.at(0).toInt();
        NetworkError error = qvariant_cast<NetworkError>(args.at(1));

        QCOMPARE(attemptCount, i + 1); // 1, 2, 3
        QCOMPARE(error, NetworkError::HttpServiceUnavailable);
    }

    reply->deleteLater();
}

void TestQCNetworkRetry::testMaxRetriesExceeded()
{
    // 测试达到最大重试次数后停止
    QCNetworkRequest request = createRequestWithRetry(QUrl(m_httpbinBaseUrl + "/status/503"),
                                                      2); // 最多重试 2 次

    auto *reply = m_manager->get(request);
    QSignalSpy retrySpy(reply, &QCNetworkReply::retryAttempt);

    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000));

    // 验证：只重试了 2 次（不超过 maxRetries）
    QCOMPARE(retrySpy.count(), 2);
    QCOMPARE(reply->error(), NetworkError::HttpServiceUnavailable);

    reply->deleteLater();
}

void TestQCNetworkRetry::testNonRetryableError()
{
    // 测试不可重试错误（如 404）不会触发重试
    QCNetworkRequest request = createRequestWithRetry(QUrl(m_httpbinBaseUrl + "/status/404"), 3);

    auto *reply = m_manager->get(request);
    QSignalSpy retrySpy(reply, &QCNetworkReply::retryAttempt);

    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 5000));

    // 验证：404 错误不重试
    QCOMPARE(retrySpy.count(), 0);
    QCOMPARE(reply->error(), NetworkError::HttpNotFound);

    reply->deleteLater();
}

void TestQCNetworkRetry::testExponentialBackoff()
{
    // 测试延迟时间符合指数退避算法
    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/status/503"));
    QCNetworkRetryPolicy policy;
    const auto applied = QCNetworkRetryPolicy::UpdateResult::Applied;
    QCOMPARE(policy.setMaxRetries(2), applied);
    QCOMPARE(policy.setInitialDelay(std::chrono::milliseconds(200)), applied);
    QCOMPARE(policy.setBackoffMultiplier(1.5), applied);
    request.setRetryPolicy(policy);
    Internal::setRetryJitterFractionForTest(1.0);

    QElapsedTimer timer;
    timer.start();

    auto *reply = m_manager->get(request);
    QSignalSpy retrySpy(reply, &QCNetworkReply::retryAttempt);
    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 15000));

    qint64 elapsed = timer.elapsed();

    qDebug() << "Actual elapsed time:" << elapsed << "ms";
    QCOMPARE(retrySpy.count(), policy.maxRetries());
    verifyRetryAttemptSignals(retrySpy, NetworkError::HttpServiceUnavailable);
    QCOMPARE(reply->error(), NetworkError::HttpServiceUnavailable);
    QVERIFY2(elapsed >= 450, qPrintable(QStringLiteral("指数退避总耗时过短：%1 ms").arg(elapsed)));
    QVERIFY2(elapsed <= 2000, qPrintable(QStringLiteral("指数退避总耗时过长：%1 ms").arg(elapsed)));

    reply->deleteLater();
}

void TestQCNetworkRetry::testCancelDuringRetry()
{
    // 测试在重试延迟期间取消请求
    QCNetworkRequest request = createRequestWithRetry(QUrl(m_httpbinBaseUrl + "/status/503"), 5);

    auto *reply = m_manager->get(request);
    QSignalSpy retrySpy(reply, &QCNetworkReply::retryAttempt);
    QSignalSpy cancelledSpy(reply, &QCNetworkReply::cancelled);
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);

    QTRY_VERIFY_WITH_TIMEOUT(retrySpy.count() >= 1, 3000);
    QCOMPARE(retrySpy.count(), 1);
    verifyRetryAttemptSignals(retrySpy, NetworkError::HttpServiceUnavailable);

    // 取消请求
    reply->cancel();

    QTRY_VERIFY_WITH_TIMEOUT(cancelledSpy.count() >= 1, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() >= 1, 2000);
    QCOMPARE(reply->state(), ReplyState::Cancelled);
    QCOMPARE(reply->error(), NetworkError::OperationCancelled);

    bool extraRetryObserved = false;
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(reply, &QCNetworkReply::retryAttempt, &loop, [&](int attempt, NetworkError error) {
        Q_UNUSED(error);
        if (attempt > 1) {
            extraRetryObserved = true;
        }
        loop.quit();
    });
    deadline.start(1000);
    loop.exec();

    QVERIFY(!extraRetryObserved);
    QCOMPARE(retrySpy.count(), 1);

    reply->deleteLater();
}

void TestQCNetworkRetry::testCustomRetryPolicy()
{
    // 测试自定义重试策略
    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/status/500"));
    QCNetworkRetryPolicy policy;
    const auto applied = QCNetworkRetryPolicy::UpdateResult::Applied;
    QCOMPARE(policy.setMaxRetries(4), applied);
    QCOMPARE(policy.setInitialDelay(std::chrono::milliseconds(50)), applied);
    QCOMPARE(policy.setBackoffMultiplier(1.2), applied);
    QCOMPARE(policy.setMaxDelay(std::chrono::milliseconds(1000)), applied);
    policy.setRetryableErrors({NetworkError::HttpInternalServerError});
    request.setRetryPolicy(policy);

    auto *reply = m_manager->get(request);
    QSignalSpy retrySpy(reply, &QCNetworkReply::retryAttempt);

    QVERIFY(waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 10000));

    // 验证：按自定义参数重试了 4 次
    QCOMPARE(retrySpy.count(), 4);
    QCOMPARE(reply->error(), NetworkError::HttpInternalServerError);

    reply->deleteLater();
}

void TestQCNetworkRetry::testManagedWaitRetry()
{
    // 测试 managed reply wait helper 覆盖重试延迟。
    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/status/503"));
    QCNetworkRetryPolicy policy;
    const auto applied = QCNetworkRetryPolicy::UpdateResult::Applied;
    QCOMPARE(policy.setMaxRetries(2), applied);
    QCOMPARE(policy.setInitialDelay(std::chrono::milliseconds(100)), applied);
    QCOMPARE(policy.setBackoffMultiplier(1.5), applied);
    request.setRetryPolicy(policy);
    Internal::setRetryJitterFractionForTest(1.0);

    QElapsedTimer timer;
    timer.start();

    // 使用 Core 异步 API 并等待 reply 完成。
    auto *reply = TestSupport::sendWaitedAsyncTestReply(*m_manager, request);

    qint64 elapsed = timer.elapsed();

    // 验证：请求失败
    QCOMPARE(reply->error(), NetworkError::HttpServiceUnavailable);

    // 验证：总时间应包含重试延迟（100 + 150 = 250ms）
    qDebug() << "Managed wait retry elapsed:" << elapsed << "ms";
    QVERIFY(elapsed >= 200); // 至少等待了延迟时间

    reply->deleteLater();
}

// ============================================================================
// Qt Test 入口
// ============================================================================

QTEST_MAIN(TestQCNetworkRetry)
#include "tst_QCNetworkRetry.moc"
