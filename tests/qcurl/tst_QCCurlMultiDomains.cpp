#include "tst_QCCurlMultiDomains.h"

#include "QCCurlMultiManager.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkConnectionPoolManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "private/QCCurlMultiDomain_p.h"
#include "private/QCCurlMultiManagerTestAccess_p.h"
#include "qcurl_resolve_test_server.h"

#include <QScopeGuard>
#include <QSignalSpy>
#include <QtTest>

using namespace QCurl;
using Access = QCCurlMultiManagerTestAccess;

QCNetworkRequest TestQCCurlMultiDomains::requestFor(const ResolveTestServer &server,
                                                    const QString &address)
{
    QCNetworkRequest request(QUrl(QStringLiteral("http://localhost:%1/").arg(server.serverPort())));
    const auto result = request.setResolveOverride(
        {QStringLiteral("localhost:%1:%2").arg(server.serverPort()).arg(address)});
    Q_ASSERT(result == QCNetworkConfigUpdateResult::Applied);
    return request;
}

QCNetworkAccessManager::ShareHandleConfig TestQCCurlMultiDomains::sharing(bool cookies, bool ssl)
{
    QCNetworkAccessManager::ShareHandleConfig config;
    config.setShareCookies(cookies);
    config.setShareSslSession(ssl);
    return config;
}

void TestQCCurlMultiDomains::verifyCookieSharing(QCNetworkAccessManager &manager,
                                                 ResolveTestServer &server,
                                                 bool expected)
{
    server.setHoldResponses(false);
    server.setResponseHeaders(QByteArrayLiteral("Set-Cookie: final_config=applied; Path=/\r\n"));
    const auto request = requestFor(server, QStringLiteral("127.0.0.2"));
    for (int index = 0; index < 2; ++index) {
        auto *reply = manager.get(request);
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
        QCOMPARE(reply->error(), NetworkError::NoError);
        QCOMPARE(reply->httpStatusCode(), 200);
    }
    QCOMPARE(server.requests().last().contains("Cookie: final_config=applied"), expected);
}

void TestQCCurlMultiDomains::shareTransitions_data()
{
    QTest::addColumn<int>("desiredFlags");
    QTest::addColumn<bool>("intermediateUpdate");
    QTest::addColumn<bool>("submitNext");
    QTest::newRow("nonempty-delayed") << 2 << false << true;
    QTest::newRow("pending-latest") << 3 << true << true;
    QTest::newRow("pending-latest-without-new-request") << 3 << true << false;
    QTest::newRow("pending-withdrawn-without-new-request") << 1 << true << false;
    QTest::newRow("off-immediate") << 0 << false << true;
    QTest::newRow("off-replaces-pending") << 0 << true << true;
    QTest::newRow("off-without-new-request") << 0 << true << false;
}

void TestQCCurlMultiDomains::shareTransitions()
{
    QFETCH(int, desiredFlags);
    QFETCH(bool, intermediateUpdate);
    QFETCH(bool, submitNext);
    ResolveTestServer first(QByteArrayLiteral("first")), second(QByteArrayLiteral("second"));
    QVERIFY(first.start(QStringLiteral("127.0.0.1")));
    QVERIFY(second.start(QStringLiteral("127.0.0.2"), first.serverPort()));
    first.setHoldResponses(true);
    second.setHoldResponses(true);
    QCNetworkAccessManager manager;
    manager.setShareHandleConfig(sharing(true, false));
    auto *engine = QCCurlMultiManager::instance();
    auto *old    = manager.get(requestFor(first, QStringLiteral("127.0.0.1")));
    QTRY_COMPARE_WITH_TIMEOUT(first.requests().size(), 1, 2000);
    QVERIFY(Access::hasShareBinding(engine, old));
    if (intermediateUpdate) {
        manager.setShareHandleConfig(sharing(false, true));
        QVERIFY(Access::hasPendingShareConfig(engine, &manager));
    }
    manager.setShareHandleConfig(sharing(desiredFlags & 1, desiredFlags & 2));
    QCOMPARE(Access::hasPendingShareConfig(engine, &manager), desiredFlags != 1);
    QCNetworkReply *next = nullptr;
    if (submitNext) {
        next = manager.get(requestFor(first, QStringLiteral("127.0.0.2")));
        QTRY_COMPARE_WITH_TIMEOUT(second.requests().size(), 1, 2000);
        QCOMPARE(Access::hasShareBinding(engine, next), desiredFlags != 0);
        if (desiredFlags != 0) {
            QVERIFY(!next->capabilityWarnings().isEmpty());
        }
    }
    const auto applied = Access::appliedShareConfig(engine, &manager);
    QVERIFY(applied.cookies && !applied.sslSession);
    first.releaseResponses();
    QTRY_VERIFY_WITH_TIMEOUT(old->isFinished(), 2000);
    QCOMPARE(old->error(), NetworkError::NoError);
    QCOMPARE(old->readAll().value(), QByteArrayLiteral("first"));
    if (next) {
        QCOMPARE(Access::hasPendingShareConfig(engine, &manager), desiredFlags != 0);
        second.releaseResponses();
        QTRY_VERIFY_WITH_TIMEOUT(next->isFinished(), 2000);
        QCOMPARE(next->error(), NetworkError::NoError);
        QCOMPARE(next->readAll().value(), QByteArrayLiteral("second"));
    }
    const auto final = Access::appliedShareConfig(engine, &manager);
    QCOMPARE(final.cookies, bool(desiredFlags & 1));
    QCOMPARE(final.sslSession, bool(desiredFlags & 2));
    QVERIFY(!Access::hasPendingShareConfig(engine, &manager));
    verifyCookieSharing(manager, second, desiredFlags & 1);
}

void TestQCCurlMultiDomains::limitsAndAdmissionSnapshots()
{
    auto *pool       = QCNetworkConnectionPoolManager::instance();
    const auto saved = pool->config();
    const auto restore = qScopeGuard([pool, saved]() { static_cast<void>(pool->setConfig(saved)); });
    auto config = saved;
    config.setMultiMaxTotalConnections(1);
    config.setMultiMaxHostConnections(1);
    QCOMPARE(pool->setConfig(config), QCNetworkConnectionPoolManager::UpdateResult::Applied);
    ResolveTestServer first(QByteArrayLiteral("first")), second(QByteArrayLiteral("second"));
    QVERIFY(first.start(QStringLiteral("127.0.0.1")));
    QVERIFY(second.start(QStringLiteral("127.0.0.2"), first.serverPort()));
    first.setHoldResponses(true);
    second.setHoldResponses(true);
    QCNetworkAccessManager manager;
    auto *engine        = QCCurlMultiManager::instance();
    const auto requestA = requestFor(first, QStringLiteral("127.0.0.1"));
    const auto requestB = requestFor(first, QStringLiteral("127.0.0.2"));
    auto *active        = manager.get(requestA);
    auto *waiting       = manager.get(requestA);
    auto *other         = manager.get(requestB);
    QTRY_VERIFY_WITH_TIMEOUT(first.requests().size() == 1 && second.requests().size() == 1, 2000);
    const auto domainA = Access::domainForReply(engine, active).lock();
    const auto domainB = Access::domainForReply(engine, other).lock();
    QVERIFY(domainA && domainB && domainA != domainB);
    QCOMPARE(domainA->maxTotalConnections.value(), 1);
    QCOMPARE(domainB->maxTotalConnections.value(), 1);
    waiting->cancel();
    QTRY_VERIFY(waiting->isFinished());
    config.setMultiMaxTotalConnections(2);
    QCOMPARE(pool->setConfig(config), QCNetworkConnectionPoolManager::UpdateResult::Applied);
    QCOMPARE(domainB->maxTotalConnections.value(), 1);
    auto *admitted = manager.get(requestB);
    QTRY_COMPARE(domainB->maxTotalConnections.value(), 2);
    QCOMPARE(domainA->maxTotalConnections.value(), 1);
    first.releaseResponses();
    second.releaseResponses();
    QTRY_VERIFY(active->isFinished() && other->isFinished() && admitted->isFinished());
    QCOMPARE(first.requests().size(), 1);
    QCOMPARE(second.requests().size(), 2);
    config.clearMultiMaxTotalConnections();
    QCOMPARE(pool->setConfig(config), QCNetworkConnectionPoolManager::UpdateResult::Applied);
    auto *cleared = manager.get(requestA);
    QTRY_VERIFY(cleared->isFinished());
    QCOMPARE(domainA->maxTotalConnections.value(), 0);
    QCOMPARE(domainB->maxTotalConnections.value(), 2);
}

void TestQCCurlMultiDomains::idleDomainsAreBoundedAndAgeWithoutAdmission()
{
    ResolveTestServer server(QByteArrayLiteral("bounded"));
    QVERIFY(server.start(QStringLiteral("127.0.0.1")));
    QCNetworkAccessManager manager;
    auto *engine = QCCurlMultiManager::instance();
    for (int index = 0; index < 21; ++index) {
        auto request = requestFor(server, QStringLiteral("127.0.0.1"));
        auto rules   = request.resolveOverride().value();
        rules.append(QStringLiteral("unused-%1.invalid:80:127.0.0.1").arg(index));
        QCOMPARE(request.setResolveOverride(rules), QCNetworkConfigUpdateResult::Applied);
        auto *reply = manager.get(request);
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
        QCOMPARE(reply->error(), NetworkError::NoError);
    }
    Access::reclaimIdleDomains(engine);
    QVERIFY(Access::isolatedDomainCount(engine) <= 16);
    Access::ageIdleDomains(engine);
    // 不发送后续请求：必须由引擎自己的定时回收完成。
    QTRY_COMPARE_WITH_TIMEOUT(Access::isolatedDomainCount(engine), 0, 2000);
}

void TestQCCurlMultiDomains::requiredResourceFailureDoesNotSend_data()
{
    QTest::addColumn<QByteArray>("variable");
    QTest::addColumn<QByteArray>("value");
    QTest::newRow("multi-init") << QByteArray("QCURL_TEST_FORCE_MULTI_FAILURE")
                                << QByteArray("init");
    QTest::newRow("multi-callbacks")
        << QByteArray("QCURL_TEST_FORCE_MULTI_FAILURE") << QByteArray("callbacks");
    QTest::newRow("multi-partial-limits") << QByteArray("QCURL_TEST_FORCE_MULTI_FAILURE")
                                          << QByteArray("CURLMOPT_MAX_HOST_CONNECTIONS");
    QTest::newRow("share-init") << QByteArray("QCURL_TEST_FORCE_SHARE_INIT_ERROR")
                                << QByteArray("1");
    QTest::newRow("share-config") << QByteArray("QCURL_TEST_FORCE_SHARE_OPTION_ERROR")
                                  << QByteArray("CURLSHOPT_SHARE:CURL_LOCK_DATA_SSL_SESSION");
    QTest::newRow("share-bind") << QByteArray("QCURL_TEST_FORCE_COOKIE_OPTION_ERROR")
                                << QByteArray("setup:CURLOPT_SHARE");
}

void TestQCCurlMultiDomains::requiredResourceFailureDoesNotSend()
{
    QFETCH(QByteArray, variable);
    QFETCH(QByteArray, value);
    ResolveTestServer server(QByteArrayLiteral("reachable"));
    QVERIFY(server.start(QStringLiteral("127.0.0.1")));
    QCNetworkAccessManager healthy;
    const auto request = requestFor(server, QStringLiteral("127.0.0.1"));
    auto *control      = healthy.get(request);
    QTRY_VERIFY_WITH_TIMEOUT(control->isFinished(), 2000);
    QCOMPARE(control->error(), NetworkError::NoError);
    QCOMPARE(server.requests().size(), 1);
    Access::ageIdleDomains(QCCurlMultiManager::instance());
    Access::reclaimIdleDomains(QCCurlMultiManager::instance());
    const auto previous = qgetenv(variable.constData());
    const auto restore  = qScopeGuard([variable, previous]() {
        if (previous.isNull()) {
            qunsetenv(variable.constData());
        } else {
            qputenv(variable.constData(), previous);
        }
    });
    qputenv(variable.constData(), value);
    QCNetworkAccessManager manager;
    manager.setShareHandleConfig(sharing(true, true));
    auto *reply = manager.get(request);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    QVERIFY2(reply->error() != NetworkError::NoError, qPrintable(reply->errorString()));
    QTest::qWait(20);
    QCOMPARE(server.requests().size(), 1);
    QCOMPARE(QCCurlMultiManager::instance()->runningRequestsCount(), 0);
    QCOMPARE(Access::activeShareBindings(QCCurlMultiManager::instance()), 0);
    qunsetenv(variable.constData());
    auto *recovery = healthy.get(request);
    QTRY_VERIFY_WITH_TIMEOUT(recovery->isFinished(), 2000);
    QCOMPARE(recovery->error(), NetworkError::NoError);
    QCOMPARE(recovery->readAll().value(), QByteArrayLiteral("reachable"));
    QCOMPARE(server.requests().size(), 2);
}

QTEST_MAIN(TestQCCurlMultiDomains)
