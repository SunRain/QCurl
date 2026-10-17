#include "QCCurlMultiManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRetryPolicy.h"
#include "private/QCCurlMultiDomain_p.h"
#include "private/QCCurlMultiManagerTestAccess_p.h"
#include "private/QCCurlPersistentTransferBridge_p.h"
#include "private/QCNetworkRetryPolicy_p.h"
#include "qcurl_resolve_test_server.h"
#include "tst_QCCurlMultiDomains.h"

#include <QScopeGuard>
#include <QSignalSpy>
#include <QtTest>

#include <array>
#include <optional>

using namespace QCurl;
using namespace std::chrono_literals;
using Access = QCCurlMultiManagerTestAccess;

namespace {

void restoreRemoveFailureInjection(const QByteArray &previous)
{
    if (previous.isNull()) {
        qunsetenv("QCURL_TEST_FORCE_MULTI_REMOVE_ERROR");
    } else {
        qputenv("QCURL_TEST_FORCE_MULTI_REMOVE_ERROR", previous);
    }
}

void verifyPersistentExchange(CURL *easy)
{
    const QByteArray request("GET /persistent HTTP/1.1\r\nHost: localhost\r\n\r\n");
    qsizetype sent  = 0;
    CURLcode result = CURLE_OK;
    const auto send = [&]() {
        if (sent == request.size() || (result != CURLE_OK && result != CURLE_AGAIN)) {
            return true;
        }
        size_t count = 0;
        result       = curl_easy_send(easy,
                                      request.constData() + sent,
                                      static_cast<size_t>(request.size() - sent),
                                      &count);
        sent += static_cast<qsizetype>(count);
        return (result != CURLE_OK && result != CURLE_AGAIN) || sent == request.size();
    };
    QTRY_VERIFY_WITH_TIMEOUT(send(), 2000);
    QCOMPARE(result, CURLE_OK);

    QByteArray response;
    const auto receive = [&]() {
        if (response.endsWith("\r\n\r\npersistent")
            || (result != CURLE_OK && result != CURLE_AGAIN)) {
            return true;
        }
        std::array<char, 1024> buffer{};
        size_t count = 0;
        result       = curl_easy_recv(easy, buffer.data(), buffer.size(), &count);
        response.append(buffer.data(), static_cast<qsizetype>(count));
        return (result != CURLE_OK && result != CURLE_AGAIN)
               || response.endsWith("\r\n\r\npersistent");
    };
    QTRY_VERIFY_WITH_TIMEOUT(receive(), 2000);
    QCOMPARE(result, CURLE_OK);
    QVERIFY(response.startsWith("HTTP/1.1 200 "));
    QVERIFY(response.endsWith("\r\n\r\npersistent"));
}

} // namespace

void TestQCCurlMultiDomains::retryPinsDomainUntilTerminalState_data()
{
    QTest::addColumn<bool>("cancelRetry");
    QTest::newRow("success-retains-snapshot-and-connection") << false;
    QTest::newRow("cancel-releases-waiting-domain") << true;
}

void TestQCCurlMultiDomains::retryPinsDomainUntilTerminalState()
{
    QFETCH(bool, cancelRetry);
    ResolveTestServer first(QByteArrayLiteral("original")), second(QByteArrayLiteral("changed"));
    QVERIFY(first.start(QStringLiteral("127.0.0.1")));
    QVERIFY(second.start(QStringLiteral("127.0.0.2"), first.serverPort()));
    first.setResponseStatus(503);
    first.setHoldResponses(true);
    QCNetworkAccessManager manager;
    QCNetworkRetryPolicy policy;
    QCOMPARE(policy.setMaxRetries(1), QCNetworkRetryPolicy::UpdateResult::Applied);
    QCOMPARE(policy.setInitialDelay(400ms), QCNetworkRetryPolicy::UpdateResult::Applied);
    QCOMPARE(policy.setMaxDelay(400ms), QCNetworkRetryPolicy::UpdateResult::Applied);
    Internal::setRetryJitterFractionForTest(0.0);
    const auto restore = qScopeGuard(
        []() { Internal::setRetryJitterFractionForTest(std::nullopt); });
    auto request = requestFor(first, QStringLiteral("127.0.0.1"));
    request.setRetryPolicy(policy);
    auto *reply  = manager.get(request);
    auto *engine = QCCurlMultiManager::instance();
    QTRY_COMPARE_WITH_TIMEOUT(first.requests().size(), 1, 2000);
    const auto domain = Access::domainForReply(engine, reply);
    QVERIFY(!domain.expired());
    QCOMPARE(request.setResolveOverride(
                 requestFor(second, QStringLiteral("127.0.0.2")).resolveOverride().value()),
             QCNetworkConfigUpdateResult::Applied);
    bool waitingDomainRetained = false;
    connect(reply, &QCNetworkReply::retryAttempt, &manager, [&]() {
        Access::ageIdleDomains(engine);
        Access::reclaimIdleDomains(engine);
        waitingDomainRetained = !domain.expired()
                                && Access::domainForReply(engine, reply).expired();
        first.setResponseStatus(200);
        if (cancelRetry) {
            QTimer::singleShot(0, reply, [reply]() { reply->cancel(); });
        }
    });
    first.releaseResponses();
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 3000);
    QVERIFY(waitingDomainRetained);
    QCOMPARE(reply->retryAttemptCount(), 1);
    QCOMPARE(first.requests().size(), cancelRetry ? 1 : 2);
    QCOMPARE(first.connections(), 1);
    QCOMPARE(second.requests().size(), 0);
    if (cancelRetry) {
        QCOMPARE(reply->state(), ReplyState::Cancelled);
        QTest::qWait(250);
        QCOMPARE(first.requests().size(), 1);
    } else {
        QCOMPARE(reply->error(), NetworkError::NoError);
        QCOMPARE(reply->readAll().value(), QByteArrayLiteral("original"));
    }
    Access::ageIdleDomains(engine);
    Access::reclaimIdleDomains(engine);
    QVERIFY(domain.expired());
}

void TestQCCurlMultiDomains::managerAddressReuseDoesNotInheritRetiredShare()
{
    ResolveTestServer first(QByteArrayLiteral("first")), second(QByteArrayLiteral("second"));
    QVERIFY(first.start(QStringLiteral("127.0.0.1")));
    QVERIFY(second.start(QStringLiteral("127.0.0.2"), first.serverPort()));
    first.setResponseHeaders(QByteArrayLiteral("Set-Cookie: retired=old; Path=/\r\n"));
    std::optional<QCNetworkAccessManager> owner(std::in_place);
    owner->setShareHandleConfig(sharing(true, false));
    const auto firstRequest  = requestFor(first, QStringLiteral("127.0.0.1"));
    const auto secondRequest = requestFor(second, QStringLiteral("127.0.0.2"));
    auto *seed               = owner->get(firstRequest);
    QTRY_VERIFY_WITH_TIMEOUT(seed->isFinished(), 2000);
    QCOMPARE(seed->error(), NetworkError::NoError);
    first.setHoldResponses(true);
    second.setHoldResponses(true);
    QPointer<QCNetworkReply> old = owner->get(firstRequest);
    QCNetworkAccessManager survivor;
    survivor.setShareHandleConfig(sharing(true, false));
    auto *other = survivor.get(secondRequest);
    QTRY_VERIFY_WITH_TIMEOUT(first.requests().size() == 2 && second.requests().size() == 1, 2000);
    QVERIFY(first.requests().last().contains("Cookie: retired=old"));
    auto *engine        = QCCurlMultiManager::instance();
    const auto domain   = Access::domainForReply(engine, old);
    const auto previous = qgetenv("QCURL_TEST_FORCE_MULTI_REMOVE_ERROR");
    const auto restore  = qScopeGuard([previous]() { restoreRemoveFailureInjection(previous); });
    // 延迟旧 record 的 detach，再在同一块存储重建 manager，禁止地址成为旧 store 的身份。
    auto *address = &owner.value();
    qputenv("QCURL_TEST_FORCE_MULTI_REMOVE_ERROR", "recursive");
    owner.reset();
    QVERIFY(!old);
    QCOMPARE(Access::activeShareBindings(engine), 2);
    owner.emplace();
    QCOMPARE(&owner.value(), address);
    owner->setShareHandleConfig(sharing(true, false));
    auto *replacement                  = owner->get(secondRequest);
    bool oldBindingRetainedAtAdmission = false;
    connect(replacement, &QCNetworkReply::stateChanged, &survivor, [&](ReplyState state) {
        if (state == ReplyState::Running) {
            oldBindingRetainedAtAdmission = Access::activeShareBindings(engine) == 2;
            qunsetenv("QCURL_TEST_FORCE_MULTI_REMOVE_ERROR");
        }
    });
    QTRY_COMPARE_WITH_TIMEOUT(second.requests().size(), 2, 2000);
    QVERIFY(oldBindingRetainedAtAdmission);
    QVERIFY(!second.requests().last().contains("Cookie:"));
    second.releaseResponses();
    QTRY_VERIFY_WITH_TIMEOUT(other->isFinished() && replacement->isFinished(), 2000);
    QCOMPARE(other->error(), NetworkError::NoError);
    QCOMPARE(replacement->error(), NetworkError::NoError);
    QCOMPARE(other->readAll().value(), QByteArrayLiteral("second"));
    QCOMPARE(replacement->readAll().value(), QByteArrayLiteral("second"));
    QCOMPARE(Access::activeShareBindings(engine), 0);
    QVERIFY(!engine->isPoisonedForTest());
    Access::ageIdleDomains(engine);
    Access::reclaimIdleDomains(engine);
    QVERIFY(domain.expired());
}

void TestQCCurlMultiDomains::reclamationPreservesCookieStoreAndNativeDomain()
{
    ResolveTestServer server(QByteArrayLiteral("cookie-store"));
    QVERIFY(server.start(QStringLiteral("127.0.0.1")));
    server.setResponseHeaders(QByteArrayLiteral("Set-Cookie: retained=yes; Path=/\r\n"));
    server.setHoldResponses(true);
    QCNetworkAccessManager manager;
    manager.setShareHandleConfig(sharing(true, false));
    const auto request = requestFor(server, QStringLiteral("127.0.0.1"));
    auto *seed         = manager.get(request);
    auto nativeRequest = request;
    QCOMPARE(nativeRequest.setResolveIsolation(QCNetworkResolveIsolation::NativeShared),
             QCNetworkConfigUpdateResult::Applied);
    auto *native = manager.get(nativeRequest);
    QTRY_COMPARE_WITH_TIMEOUT(server.requests().size(), 2, 2000);
    auto *engine              = QCCurlMultiManager::instance();
    const auto isolatedDomain = Access::domainForReply(engine, seed);
    const auto nativeDomain   = Access::domainForReply(engine, native);
    QVERIFY(!isolatedDomain.expired() && !nativeDomain.expired());
    server.releaseResponses();
    QTRY_VERIFY_WITH_TIMEOUT(seed->isFinished() && native->isFinished(), 2000);
    QCOMPARE(seed->error(), NetworkError::NoError);
    QCOMPARE(native->error(), NetworkError::NoError);
    Access::ageIdleDomains(engine);
    Access::reclaimIdleDomains(engine);
    QVERIFY(isolatedDomain.expired());
    QVERIFY(!nativeDomain.expired());
    auto *afterReclamation = manager.get(request);
    QTRY_VERIFY_WITH_TIMEOUT(afterReclamation->isFinished(), 2000);
    QCOMPARE(afterReclamation->error(), NetworkError::NoError);
    QCOMPARE(afterReclamation->readAll().value(), QByteArrayLiteral("cookie-store"));
    QVERIFY(server.requests().last().contains("Cookie: retained=yes"));
    QCOMPARE(server.connections(), 3);
    QCNetworkAccessManager other;
    other.setShareHandleConfig(sharing(true, false));
    auto *unrelated = other.get(request);
    QTRY_VERIFY_WITH_TIMEOUT(unrelated->isFinished(), 2000);
    QCOMPARE(unrelated->error(), NetworkError::NoError);
    QVERIFY(!server.requests().last().contains("Cookie:"));
}

void TestQCCurlMultiDomains::persistentConnectionSurvivesIsolatedDomainReclamation()
{
    ResolveTestServer server(QByteArrayLiteral("persistent"));
    QVERIFY(server.start(QStringLiteral("127.0.0.1")));
    const auto url = QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(server.serverPort()));
    CURL *easy     = nullptr;
    QCCurlTransferToken token = 0;
    std::optional<CURLcode> completed;
    QString error;
    const auto configure = [&](CURL *handle, QString *message) {
        easy        = handle;
        auto result = curl_easy_setopt(handle, CURLOPT_URL, url.toEncoded().constData());
        if (result == CURLE_OK) {
            result = curl_easy_setopt(handle, CURLOPT_PROXY, "");
        }
        if (result == CURLE_OK) {
            result = curl_easy_setopt(handle, CURLOPT_CONNECT_ONLY, 1L);
        }
        if (result != CURLE_OK) {
            *message = QString::fromLatin1(curl_easy_strerror(result));
        }
        return result == CURLE_OK;
    };
    const auto completion = [&](QCCurlTransferToken, CURLcode result, long) { completed = result; };
    QVERIFY2(registerPersistentTransfer(configure, completion, &token, &error), qPrintable(error));
    const auto detach = qScopeGuard([&]() { removePersistentTransfer(token); });
    QTRY_VERIFY_WITH_TIMEOUT(completed.has_value(), 2000);
    QCOMPARE(completed.value(), CURLE_OK);
    auto *engine = QCCurlMultiManager::instance();
    QCOMPARE(engine->runningRequestsCount(), 1);

    QCNetworkAccessManager manager;
    server.setHoldResponses(true);
    auto *reply = manager.get(requestFor(server, QStringLiteral("127.0.0.1")));
    QTRY_COMPARE_WITH_TIMEOUT(server.requests().size(), 1, 2000);
    const auto disposableDomain = Access::domainForReply(engine, reply);
    QVERIFY(!disposableDomain.expired());
    server.releaseResponses();
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    QCOMPARE(reply->error(), NetworkError::NoError);
    Access::ageIdleDomains(engine);
    Access::reclaimIdleDomains(engine);
    QVERIFY(disposableDomain.expired());
    // CURLMSG_DONE 不终结 CONNECT_ONLY lease；真正的收发比引用计数更能识别提前解绑。
    verifyPersistentExchange(easy);
    QCOMPARE(server.requests().size(), 2);
    removePersistentTransfer(token);
    token = 0;
    QCOMPARE(engine->runningRequestsCount(), 0);
}
