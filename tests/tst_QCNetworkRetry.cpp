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

#include <QtTest/QtTest>
#include <QEventLoop>
#include <QTimer>
#include <QSignalSpy>
#include <QElapsedTimer>

#include "QCNetworkAccessManager.h"
#include "QCNetworkRequest.h"
#include "QCNetworkReply.h"
#include "QCNetworkError.h"
#include "QCNetworkRetryPolicy.h"

using namespace QCurl;

class TestQCNetworkRetry : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // ========== QCNetworkRetryPolicy 配置测试 ==========
    void testRetryPolicyDefaults();
    void testRetryPolicyFactoryMethods();
    void testRetryPolicyDelayCalculation();
    void testRetryPolicyShouldRetry();

    // ========== 实际重试行为测试 ==========
    void testNoRetry();                    // 验证默认不重试
    void testBasicRetry();                 // 基本重试成功场景
    void testMaxRetriesExceeded();         // 达到最大重试次数
    void testNonRetryableError();          // 404 等不可重试错误
    void testExponentialBackoff();         // 延迟时间验证
    void testCancelDuringRetry();          // 取消重试中的请求
    void testCustomRetryPolicy();          // 自定义策略
    void testSyncRetry();                  // 同步模式重试

private:
    QCNetworkAccessManager *manager;
    static const QString HTTPBIN_BASE_URL;

    // 辅助方法
    bool waitForSignal(QObject *obj, const char *signal, int timeout = 10000);
    QCNetworkRequest createRequestWithRetry(const QUrl &url, int maxRetries);
};

const QString TestQCNetworkRetry::HTTPBIN_BASE_URL = QStringLiteral("http://localhost:8935");

// ============================================================================
// 测试初始化和清理
// ============================================================================

void TestQCNetworkRetry::initTestCase()
{
    qDebug() << "========================================";
    qDebug() << "QCNetworkRetry 单元测试开始";
    qDebug() << "httpbin 服务地址:" << HTTPBIN_BASE_URL;
    qDebug() << "========================================";

    // 创建网络管理器
    manager = new QCNetworkAccessManager(this);

    // 验证 httpbin 服务可用
    QCNetworkRequest request(QUrl(HTTPBIN_BASE_URL + "/get"));
    auto *reply = manager->sendGet(request);

    QVERIFY(waitForSignal(reply, SIGNAL(finished()), 5000));

    if (reply->error() != NetworkError::NoError) {
        qCritical() << "httpbin 服务不可用！请启动服务：";
        qCritical() << "docker run -d -p 8935:80 --name qcurl-httpbin kennethreitz/httpbin";
        QFAIL("httpbin 服务不可用");
    }

    reply->deleteLater();
    qDebug() << "httpbin 服务验证通过";
}

void TestQCNetworkRetry::cleanupTestCase()
{
    delete manager;
    qDebug() << "========================================";
    qDebug() << "QCNetworkRetry 单元测试完成";
    qDebug() << "========================================";
}

void TestQCNetworkRetry::init()
{
    // 每个测试前执行
}

void TestQCNetworkRetry::cleanup()
{
    // 每个测试后执行
}

// ============================================================================
// 辅助方法实现
// ============================================================================

bool TestQCNetworkRetry::waitForSignal(QObject *obj, const char *signal, int timeout)
{
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    connect(obj, signal, &loop, SLOT(quit()));
    connect(&timer, SIGNAL(timeout()), &loop, SLOT(quit()));  // 使用 SIGNAL/SLOT 宏

    timer.start(timeout);
    loop.exec();

    return timer.isActive();  // true = 收到信号, false = 超时
}

QCNetworkRequest TestQCNetworkRetry::createRequestWithRetry(const QUrl &url, int maxRetries)
{
    QCNetworkRequest request(url);
    QCNetworkRetryPolicy policy;
    policy.maxRetries = maxRetries;
    policy.initialDelay = std::chrono::milliseconds(100);  // 缩短测试时间
    policy.backoffMultiplier = 1.5;
    policy.maxDelay = std::chrono::milliseconds(5000);
    request.setRetryPolicy(policy);
    return request;
}

// ============================================================================
// QCNetworkRetryPolicy 配置测试
// ============================================================================

void TestQCNetworkRetry::testRetryPolicyDefaults()
{
    QCNetworkRetryPolicy policy;

    // 验证默认值
    QCOMPARE(policy.maxRetries, 0);
    QCOMPARE(policy.initialDelay.count(), 1000);
    QCOMPARE(policy.backoffMultiplier, 2.0);
    QCOMPARE(policy.maxDelay.count(), 30000);
    QCOMPARE(policy.retryableErrors.size(), 8);  // 8 种可重试错误
}

void TestQCNetworkRetry::testRetryPolicyFactoryMethods()
{
    // 测试 noRetry()
    QCNetworkRetryPolicy noRetry = QCNetworkRetryPolicy::noRetry();
    QCOMPARE(noRetry.maxRetries, 0);

    // 测试 standardRetry()
    QCNetworkRetryPolicy standard = QCNetworkRetryPolicy::standardRetry();
    QCOMPARE(standard.maxRetries, 3);
    QCOMPARE(standard.initialDelay.count(), 1000);
    QCOMPARE(standard.backoffMultiplier, 2.0);

    // 测试 aggressiveRetry()
    QCNetworkRetryPolicy aggressive = QCNetworkRetryPolicy::aggressiveRetry();
    QCOMPARE(aggressive.maxRetries, 5);
    QCOMPARE(aggressive.initialDelay.count(), 500);
    QCOMPARE(aggressive.backoffMultiplier, 1.5);
}

void TestQCNetworkRetry::testRetryPolicyDelayCalculation()
{
    QCNetworkRetryPolicy policy;
    policy.initialDelay = std::chrono::milliseconds(1000);
    policy.backoffMultiplier = 2.0;
    policy.maxDelay = std::chrono::milliseconds(10000);

    // 测试指数退避：delay = initialDelay * (backoffMultiplier ^ attemptCount)
    QCOMPARE(policy.delayForAttempt(0).count(), 1000);   // 1000 * 2^0 = 1000
    QCOMPARE(policy.delayForAttempt(1).count(), 2000);   // 1000 * 2^1 = 2000
    QCOMPARE(policy.delayForAttempt(2).count(), 4000);   // 1000 * 2^2 = 4000
    QCOMPARE(policy.delayForAttempt(3).count(), 8000);   // 1000 * 2^3 = 8000
    QCOMPARE(policy.delayForAttempt(4).count(), 10000);  // 限制在 maxDelay
}

void TestQCNetworkRetry::testRetryPolicyShouldRetry()
{
    QCNetworkRetryPolicy policy;
    policy.maxRetries = 3;
    policy.retryableErrors = {
        NetworkError::ConnectionTimeout,
        NetworkError::HttpServiceUnavailable
    };

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
    policy.maxRetries = 0;
    QVERIFY(!policy.shouldRetry(NetworkError::ConnectionTimeout, 0));
}

// ============================================================================
// 实际重试行为测试
// ============================================================================

void TestQCNetworkRetry::testNoRetry()
{
    // 验证默认不重试行为
    QCNetworkRequest request(QUrl(HTTPBIN_BASE_URL + "/status/503"));
    // 不设置 retryPolicy，使用默认的 noRetry()

    auto *reply = manager->sendGet(request);
    QSignalSpy retrySpy(reply, &QCNetworkReply::retryAttempt);

    QVERIFY(waitForSignal(reply, SIGNAL(finished()), 5000));

    // 验证：没有重试
    QCOMPARE(retrySpy.count(), 0);
    QCOMPARE(reply->error(), NetworkError::HttpServiceUnavailable);

    reply->deleteLater();
}

void TestQCNetworkRetry::testBasicRetry()
{
    // 基本重试场景：503 错误重试 3 次
    QCNetworkRequest request = createRequestWithRetry(
        QUrl(HTTPBIN_BASE_URL + "/status/503"), 3);

    auto *reply = manager->sendGet(request);
    QSignalSpy retrySpy(reply, &QCNetworkReply::retryAttempt);

    QVERIFY(waitForSignal(reply, SIGNAL(finished()), 10000));

    // 验证：重试了 3 次
    QCOMPARE(retrySpy.count(), 3);

    // 验证：最终仍然失败（503 错误）
    QCOMPARE(reply->error(), NetworkError::HttpServiceUnavailable);

    // 验证信号参数
    for (int i = 0; i < retrySpy.count(); ++i) {
        auto args = retrySpy.at(i);
        int attemptCount = args.at(0).toInt();
        NetworkError error = qvariant_cast<NetworkError>(args.at(1));

        QCOMPARE(attemptCount, i + 1);  // 1, 2, 3
        QCOMPARE(error, NetworkError::HttpServiceUnavailable);
    }

    reply->deleteLater();
}

void TestQCNetworkRetry::testMaxRetriesExceeded()
{
    // 测试达到最大重试次数后停止
    QCNetworkRequest request = createRequestWithRetry(
        QUrl(HTTPBIN_BASE_URL + "/status/503"), 2);  // 最多重试 2 次

    auto *reply = manager->sendGet(request);
    QSignalSpy retrySpy(reply, &QCNetworkReply::retryAttempt);

    QVERIFY(waitForSignal(reply, SIGNAL(finished()), 10000));

    // 验证：只重试了 2 次（不超过 maxRetries）
    QCOMPARE(retrySpy.count(), 2);
    QCOMPARE(reply->error(), NetworkError::HttpServiceUnavailable);

    reply->deleteLater();
}

void TestQCNetworkRetry::testNonRetryableError()
{
    // 测试不可重试错误（如 404）不会触发重试
    QCNetworkRequest request = createRequestWithRetry(
        QUrl(HTTPBIN_BASE_URL + "/status/404"), 3);

    auto *reply = manager->sendGet(request);
    QSignalSpy retrySpy(reply, &QCNetworkReply::retryAttempt);

    QVERIFY(waitForSignal(reply, SIGNAL(finished()), 5000));

    // 验证：404 错误不重试
    QCOMPARE(retrySpy.count(), 0);
    QCOMPARE(reply->error(), NetworkError::HttpNotFound);

    reply->deleteLater();
}

void TestQCNetworkRetry::testExponentialBackoff()
{
    // 测试延迟时间符合指数退避算法
    QCNetworkRequest request(QUrl(HTTPBIN_BASE_URL + "/status/503"));
    QCNetworkRetryPolicy policy;
    policy.maxRetries = 2;
    policy.initialDelay = std::chrono::milliseconds(200);
    policy.backoffMultiplier = 1.5;
    request.setRetryPolicy(policy);

    QElapsedTimer timer;
    timer.start();

    auto *reply = manager->sendGet(request);
    QVERIFY(waitForSignal(reply, SIGNAL(finished()), 15000));

    qint64 elapsed = timer.elapsed();

    // 预期总延迟：200 + 300 = 500ms
    // 允许 ±500ms 误差（网络请求时间和调度延迟）
    qDebug() << "Actual elapsed time:" << elapsed << "ms";
    QVERIFY(elapsed >= 200);   // 至少等待了初始延迟
    QVERIFY(elapsed <= 2000);  // 不应该超过太多

    reply->deleteLater();
}

void TestQCNetworkRetry::testCancelDuringRetry()
{
    // 测试在重试延迟期间取消请求
    QCNetworkRequest request = createRequestWithRetry(
        QUrl(HTTPBIN_BASE_URL + "/status/503"), 5);

    auto *reply = manager->sendGet(request);
    QSignalSpy retrySpy(reply, &QCNetworkReply::retryAttempt);
    QSignalSpy cancelledSpy(reply, &QCNetworkReply::cancelled);
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);

    // 等待第一次重试完成（约 150ms）
    QTest::qWait(200);

    // 取消请求
    reply->cancel();

    // 等待 finished 或 cancelled 信号（取消后应该会触发其中之一）
    bool signalReceived = waitForSignal(reply, SIGNAL(cancelled()), 2000) ||
                         waitForSignal(reply, SIGNAL(finished()), 100);

    // 验证：至少收到一个完成信号
    QVERIFY(signalReceived || cancelledSpy.count() > 0 || finishedSpy.count() > 0);

    // 验证：重试次数应小于 5（因为被取消了）
    qDebug() << "Retry attempts before cancel:" << retrySpy.count();
    QVERIFY(retrySpy.count() < 5);

    reply->deleteLater();
}

void TestQCNetworkRetry::testCustomRetryPolicy()
{
    // 测试自定义重试策略
    QCNetworkRequest request(QUrl(HTTPBIN_BASE_URL + "/status/500"));
    QCNetworkRetryPolicy policy;
    policy.maxRetries = 4;
    policy.initialDelay = std::chrono::milliseconds(50);
    policy.backoffMultiplier = 1.2;
    policy.maxDelay = std::chrono::milliseconds(1000);
    policy.retryableErrors = {NetworkError::HttpInternalServerError};
    request.setRetryPolicy(policy);

    auto *reply = manager->sendGet(request);
    QSignalSpy retrySpy(reply, &QCNetworkReply::retryAttempt);

    QVERIFY(waitForSignal(reply, SIGNAL(finished()), 10000));

    // 验证：按自定义参数重试了 4 次
    QCOMPARE(retrySpy.count(), 4);
    QCOMPARE(reply->error(), NetworkError::HttpInternalServerError);

    reply->deleteLater();
}

void TestQCNetworkRetry::testSyncRetry()
{
    // 测试同步模式重试
    QCNetworkRequest request(QUrl(HTTPBIN_BASE_URL + "/status/503"));
    QCNetworkRetryPolicy policy;
    policy.maxRetries = 2;
    policy.initialDelay = std::chrono::milliseconds(100);
    policy.backoffMultiplier = 1.5;
    request.setRetryPolicy(policy);

    QElapsedTimer timer;
    timer.start();

    // 使用同步 API
    auto *reply = manager->sendGetSync(request);

    qint64 elapsed = timer.elapsed();

    // 验证：请求失败
    QCOMPARE(reply->error(), NetworkError::HttpServiceUnavailable);

    // 验证：总时间应包含重试延迟（100 + 150 = 250ms）
    qDebug() << "Sync retry elapsed:" << elapsed << "ms";
    QVERIFY(elapsed >= 200);  // 至少等待了延迟时间

    reply->deleteLater();
}

// ============================================================================
// Qt Test 入口
// ============================================================================

QTEST_MAIN(TestQCNetworkRetry)
#include "tst_QCNetworkRetry.moc"
