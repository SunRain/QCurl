/**
 * @file tst_QCNetworkUnifiedPolicyMiddlewareOffline.cpp
 * @brief 基于 QCNetworkMiddleware 的统一策略（重试/脱敏日志/观测）纯离线门禁（MockHandler）
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkDefaultLogger.h"
#include "QCNetworkMiddleware.h"
#include "QCNetworkMiddlewareExtras.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRetryPolicy.h"
#include "private/QCNetworkMiddlewareInternal_p.h"
#include "qcnetwork_mock_test_support.h"
#include "qcnetwork_retry_policy_test_helper.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include <chrono>

using namespace QCurl;

class TestQCNetworkUnifiedPolicyMiddlewareOffline : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void testDefaultRetryPolicyInjected();
    void testExplicitNoRetryNotOverridden();
    void testRedactingLoggingNoLeak();
    void testReplyUsesLoggerSnapshotAfterManagerReplacement();
    void testObservabilityFieldsAndRetryCount();

private:
    QCNetworkAccessManager *m_manager = nullptr;
    QCNetworkMockHandler m_mock;
};

void TestQCNetworkUnifiedPolicyMiddlewareOffline::init()
{
    m_manager = new QCNetworkAccessManager(this);
    QCurl::TestSupport::setMockHandler(*m_manager, &m_mock);
    m_manager->setLogger({});
    m_manager->clearMiddlewares();

    m_mock.clear();
    m_mock.setGlobalDelay(0);
}

void TestQCNetworkUnifiedPolicyMiddlewareOffline::cleanup()
{
    if (m_manager) {
        m_manager->clearMiddlewares();
        m_manager->setLogger({});
        QCurl::TestSupport::setMockHandler(*m_manager, nullptr);
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

    const QCNetworkRetryPolicy defaultPolicy
        = TestSupport::makeRetryPolicyOrFail(1,
                                             std::chrono::milliseconds(1),
                                             1.0,
                                             std::chrono::milliseconds(10));

    QCUnifiedRetryPolicyMiddleware retryMw(defaultPolicy);
    m_manager->addMiddleware(&retryMw);

    QCNetworkRequest request(url); // 未显式 setRetryPolicy
    auto *reply = m_manager->get(request);

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

    const QCNetworkRetryPolicy defaultPolicy
        = TestSupport::makeRetryPolicyOrFail(2,
                                             std::chrono::milliseconds(1),
                                             1.0,
                                             std::chrono::milliseconds(10));

    QCUnifiedRetryPolicyMiddleware retryMw(defaultPolicy);
    m_manager->addMiddleware(&retryMw);

    QCNetworkRequest request(url);
    request.setRetryPolicy(QCNetworkRetryPolicy::noRetry()); // 显式禁用，不应被覆盖

    auto *reply = m_manager->get(request);
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
    QCNetworkDefaultLogger *defaultLogger = nullptr;
    const auto logger                     = QCNetworkLoggerHandle::createWithBorrow(&defaultLogger);
    defaultLogger->enableConsoleOutput(false);
    defaultLogger->setMinLogLevel(NetworkLogLevel::Debug);
    m_manager->setLogger(logger);

    QCRedactingLoggingMiddleware loggingMw;
    m_manager->addMiddleware(&loggingMw);

    const QByteArray secret("VERY_SECRET_TOKEN");
    const QUrl url(QStringLiteral("http://example.com/offline/policy/log_redaction?token=%1")
                       .arg(QString::fromLatin1(secret)));
    m_mock.mockResponse(HttpMethod::Get, url, QByteArray("ok"), 200);

    QCNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + secret);

    auto *reply = m_manager->get(request);
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
    QVERIFY(finishedSpy.wait(2000));

    bool sawRedacted = false;
    for (const auto &entry : defaultLogger->entries()) {
        const QString msg = entry.message();
        QVERIFY2(!msg.contains(QString::fromLatin1(secret)), "日志中泄漏了敏感 token 明文");
        if (msg.contains(QStringLiteral("[REDACTED]"))) {
            sawRedacted = true;
        }
    }
    QVERIFY(sawRedacted);

    m_manager->clearMiddlewares();
    m_manager->setLogger({});
    reply->deleteLater();
}

void TestQCNetworkUnifiedPolicyMiddlewareOffline::testReplyUsesLoggerSnapshotAfterManagerReplacement()
{
    m_mock.setGlobalDelay(20);
    QCNetworkDefaultLogger *originalDefaultLogger = nullptr;
    const auto originalLogger = QCNetworkLoggerHandle::createWithBorrow(&originalDefaultLogger);
    originalDefaultLogger->enableConsoleOutput(false);
    originalDefaultLogger->setMinLogLevel(NetworkLogLevel::Debug);
    m_manager->setLogger(originalLogger);

    QCRedactingLoggingMiddleware loggingMw;
    m_manager->addMiddleware(&loggingMw);

    const QUrl url(QStringLiteral("http://example.com/offline/policy/logger_snapshot"));
    m_mock.mockResponse(HttpMethod::Get, url, QByteArrayLiteral("ok"), 200);

    auto *reply = m_manager->get(QCNetworkRequest(url));
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);

    const auto replacementLogger = QCNetworkLoggerHandle::create<QCNetworkDefaultLogger>();
    m_manager->setLogger(replacementLogger);

    QVERIFY(finishedSpy.wait(2000));
    const QCNetworkLoggerHandle retainedLogger = reply->loggerSnapshot();
    QCOMPARE(retainedLogger, originalLogger);

    bool sawResponse = false;
    for (const auto &entry : originalDefaultLogger->entries()) {
        if (entry.category() == QStringLiteral("Response")) {
            sawResponse = true;
            break;
        }
    }
    QVERIFY(sawResponse);

    m_manager->clearMiddlewares();
    reply->deleteLater();
}

void TestQCNetworkUnifiedPolicyMiddlewareOffline::testObservabilityFieldsAndRetryCount()
{
    QCNetworkDefaultLogger *defaultLogger = nullptr;
    const auto logger                     = QCNetworkLoggerHandle::createWithBorrow(&defaultLogger);
    defaultLogger->enableConsoleOutput(false);
    defaultLogger->setMinLogLevel(NetworkLogLevel::Debug);
    m_manager->setLogger(logger);

    QCObservabilityMiddleware obsMw;
    m_manager->addMiddleware(&obsMw);

    const QByteArray secret("OBS_SECRET_TOKEN");
    const QUrl url(QStringLiteral("http://example.com/offline/policy/observability?token=%1")
                       .arg(QString::fromLatin1(secret)));

    m_mock.enqueueResponse(HttpMethod::Get, url, QByteArray("fail"), 503);
    m_mock.enqueueResponse(HttpMethod::Get, url, QByteArray("ok"), 200);

    const QCNetworkRetryPolicy policy
        = TestSupport::makeRetryPolicyOrFail(1,
                                             std::chrono::milliseconds(1),
                                             1.0,
                                             std::chrono::milliseconds(10));

    QCNetworkRequest request(url);
    request.setRetryPolicy(policy);

    auto *reply = m_manager->get(request);
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
    QVERIFY(finishedSpy.wait(2000));

    bool found = false;
    for (const auto &entry : defaultLogger->entries()) {
        if (entry.category() != QStringLiteral("Observability")) {
            continue;
        }
        const QByteArray jsonBytes = entry.message().toUtf8();
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
    m_manager->setLogger({});
    reply->deleteLater();
}

QTEST_MAIN(TestQCNetworkUnifiedPolicyMiddlewareOffline)
#include "tst_QCNetworkUnifiedPolicyMiddlewareOffline.moc"
