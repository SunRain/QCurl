/**
 * @file tst_QCNetworkUnifiedPolicyMiddlewareOffline.cpp
 * @brief 基于 QCNetworkMiddleware 的统一策略（重试/脱敏日志/观测）纯离线门禁（MockHandler）
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkLogger.h"
#include "QCNetworkMiddleware.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRetryPolicy.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest/QtTest>

using namespace QCurl;

class TestQCNetworkUnifiedPolicyMiddlewareOffline : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testDefaultRetryPolicyInjected();
    void testExplicitNoRetryNotOverridden();
    void testRedactingLoggingNoLeak();
    void testObservabilityFieldsAndRetryCount();

private:
    QCNetworkAccessManager *m_manager = nullptr;
    QCNetworkMockHandler m_mock;
};

void TestQCNetworkUnifiedPolicyMiddlewareOffline::init()
{
    m_manager = new QCNetworkAccessManager(this);
    m_manager->setMockHandler(&m_mock);
    m_manager->setLogger(nullptr);
    m_manager->clearMiddlewares();

    m_mock.clear();
    m_mock.setGlobalDelay(0);
}

void TestQCNetworkUnifiedPolicyMiddlewareOffline::cleanup()
{
    if (m_manager) {
        m_manager->clearMiddlewares();
        m_manager->setLogger(nullptr);
        m_manager->setMockHandler(nullptr);
        m_manager->deleteLater();
        m_manager = nullptr;
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void TestQCNetworkUnifiedPolicyMiddlewareOffline::testDefaultRetryPolicyInjected()
{
    const QUrl url("http://example.com/offline/policy/default_retry_injected");
    m_mock.enqueueResponse(HttpMethod::Get, url, QByteArray("fail"), 500);
    m_mock.enqueueResponse(HttpMethod::Get, url, QByteArray("ok"), 200);

    QCNetworkRetryPolicy defaultPolicy;
    defaultPolicy.maxRetries        = 1;
    defaultPolicy.initialDelay      = std::chrono::milliseconds(1);
    defaultPolicy.backoffMultiplier = 1.0;
    defaultPolicy.maxDelay          = std::chrono::milliseconds(10);

    QCUnifiedRetryPolicyMiddleware retryMw(defaultPolicy);
    m_manager->addMiddleware(&retryMw);

    QCNetworkRequest request(url); // 未显式 setRetryPolicy
    auto *reply = m_manager->sendGet(request);

    QSignalSpy retrySpy(reply, &QCNetworkReply::retryAttempt);
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
    QVERIFY(finishedSpy.wait(2000));

    QCOMPARE(retrySpy.count(), 1);
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->readAll().value_or(QByteArray()), QByteArray("ok"));

    m_manager->clearMiddlewares();
    reply->deleteLater();
}

void TestQCNetworkUnifiedPolicyMiddlewareOffline::testExplicitNoRetryNotOverridden()
{
    const QUrl url("http://example.com/offline/policy/explicit_no_retry");
    m_mock.enqueueResponse(HttpMethod::Get, url, QByteArray("fail"), 500);
    m_mock.enqueueResponse(HttpMethod::Get, url, QByteArray("ok"), 200);

    QCNetworkRetryPolicy defaultPolicy;
    defaultPolicy.maxRetries        = 2;
    defaultPolicy.initialDelay      = std::chrono::milliseconds(1);
    defaultPolicy.backoffMultiplier = 1.0;
    defaultPolicy.maxDelay          = std::chrono::milliseconds(10);

    QCUnifiedRetryPolicyMiddleware retryMw(defaultPolicy);
    m_manager->addMiddleware(&retryMw);

    QCNetworkRequest request(url);
    request.setRetryPolicy(QCNetworkRetryPolicy::noRetry()); // 显式禁用，不应被覆盖

    auto *reply = m_manager->sendGet(request);
    QSignalSpy retrySpy(reply, &QCNetworkReply::retryAttempt);
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
    QVERIFY(finishedSpy.wait(2000));

    QCOMPARE(retrySpy.count(), 0);
    QCOMPARE(reply->error(), NetworkError::HttpInternalServerError);

    m_manager->clearMiddlewares();
    reply->deleteLater();
}

void TestQCNetworkUnifiedPolicyMiddlewareOffline::testRedactingLoggingNoLeak()
{
    QCNetworkDefaultLogger logger;
    logger.enableConsoleOutput(false);
    logger.setMinLogLevel(NetworkLogLevel::Debug);
    m_manager->setLogger(&logger);

    QCRedactingLoggingMiddleware loggingMw;
    m_manager->addMiddleware(&loggingMw);

    const QByteArray secret("VERY_SECRET_TOKEN");
    const QUrl url(QStringLiteral("http://example.com/offline/policy/log_redaction?token=%1")
                       .arg(QString::fromLatin1(secret)));
    m_mock.mockResponse(HttpMethod::Get, url, QByteArray("ok"), 200);

    QCNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + secret);

    auto *reply = m_manager->sendGet(request);
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
    QVERIFY(finishedSpy.wait(2000));

    bool sawRedacted = false;
    for (const auto &entry : logger.entries()) {
        const QString msg = entry.message;
        QVERIFY2(!msg.contains(QString::fromLatin1(secret)), "日志中泄漏了敏感 token 明文");
        if (msg.contains(QStringLiteral("[REDACTED]"))) {
            sawRedacted = true;
        }
    }
    QVERIFY(sawRedacted);

    m_manager->clearMiddlewares();
    m_manager->setLogger(nullptr);
    reply->deleteLater();
}

void TestQCNetworkUnifiedPolicyMiddlewareOffline::testObservabilityFieldsAndRetryCount()
{
    QCNetworkDefaultLogger logger;
    logger.enableConsoleOutput(false);
    logger.setMinLogLevel(NetworkLogLevel::Debug);
    m_manager->setLogger(&logger);

    QCObservabilityMiddleware obsMw;
    m_manager->addMiddleware(&obsMw);

    const QByteArray secret("OBS_SECRET_TOKEN");
    const QUrl url(QStringLiteral("http://example.com/offline/policy/observability?token=%1")
                       .arg(QString::fromLatin1(secret)));

    m_mock.enqueueResponse(HttpMethod::Get, url, QByteArray("fail"), 503);
    m_mock.enqueueResponse(HttpMethod::Get, url, QByteArray("ok"), 200);

    QCNetworkRetryPolicy policy;
    policy.maxRetries        = 1;
    policy.initialDelay      = std::chrono::milliseconds(1);
    policy.backoffMultiplier = 1.0;
    policy.maxDelay          = std::chrono::milliseconds(10);

    QCNetworkRequest request(url);
    request.setRetryPolicy(policy);

    auto *reply = m_manager->sendGet(request);
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
    QVERIFY(finishedSpy.wait(2000));

    bool found = false;
    for (const auto &entry : logger.entries()) {
        if (entry.category != QStringLiteral("Observability")) {
            continue;
        }
        const QByteArray jsonBytes = entry.message.toUtf8();
        const auto doc             = QJsonDocument::fromJson(jsonBytes);
        QVERIFY(doc.isObject());
        const QJsonObject obj = doc.object();

        QVERIFY(obj.contains(QStringLiteral("url")));
        QVERIFY(obj.contains(QStringLiteral("method")));
        QVERIFY(obj.contains(QStringLiteral("httpStatusCode")));
        QVERIFY(obj.contains(QStringLiteral("durationMs")));
        QVERIFY(obj.contains(QStringLiteral("attemptCount")));
        QVERIFY(obj.contains(QStringLiteral("bytesReceived")));
        QVERIFY(obj.contains(QStringLiteral("bytesTotal")));
        QVERIFY(obj.contains(QStringLiteral("error")));

        const QString redactedUrl = obj.value(QStringLiteral("url")).toString();
        QVERIFY2(!redactedUrl.contains(QString::fromLatin1(secret)),
                 "观测事件中泄漏了敏感 token 明文");

        QCOMPARE(obj.value(QStringLiteral("method")).toString(), QStringLiteral("GET"));
        QCOMPARE(obj.value(QStringLiteral("httpStatusCode")).toInt(), 200);
        QCOMPARE(obj.value(QStringLiteral("attemptCount")).toInt(), 1);
        QVERIFY(obj.value(QStringLiteral("durationMs")).toDouble() >= 0.0);

        found = true;
        break;
    }
    QVERIFY(found);

    m_manager->clearMiddlewares();
    m_manager->setLogger(nullptr);
    reply->deleteLater();
}

QTEST_MAIN(TestQCNetworkUnifiedPolicyMiddlewareOffline)
#include "tst_QCNetworkUnifiedPolicyMiddlewareOffline.moc"
