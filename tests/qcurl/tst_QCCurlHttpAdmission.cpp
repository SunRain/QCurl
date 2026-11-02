#include "tst_QCCurlHttpAdmission.h"

#include "QCCurlMultiManager.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkConnectionPoolManager.h"
#include "QCNetworkReply.h"
#include "private/QCCurlMultiManagerTestAccess_p.h"
#include "private/QCNetworkReplyCallbacks_p.h"
#include "private/QCNetworkReplyTransferState_p.h"

#include <QCoreApplication>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QtTest>

#include <optional>
#include <thread>

using namespace QCurl;
using AdmissionState = QCCurlMultiManager::HttpAdmissionState;

namespace {

QSharedPointer<QCNetworkReplyTransferState> makeState()
{
    const QUrl url(QStringLiteral("http://127.0.0.1:1/admission"));
    auto state = QSharedPointer<QCNetworkReplyTransferState>::create(
        url, QCNetworkDiagnosticQueryPolicy::Redacted);
    state->curlPlan = Internal::compileRequest(
        Internal::normalizeRequest(QCNetworkRequest(url),
                                   HttpMethod::Get,
                                   Internal::makeEmptyRequestBody()));
    return state;
}

} // namespace

void tst_QCCurlHttpAdmission::cleanup()
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCOMPARE(QCCurlMultiManager::instance()->runningRequestsCount(), 0);
    QCOMPARE(QCNetworkConnectionPoolManager::instance()->statistics().activeRequests(), 0);
}

void tst_QCCurlHttpAdmission::acceptedReturnsTokenAndDetachedHandle()
{
    auto *engine = QCCurlMultiManager::instance();
    auto state   = makeState();
    QCNetworkReply reply(QCNetworkReply::TestOnlyKey{},
                         state->curlPlan.normalized.request,
                         HttpMethod::Get);
    QSignalSpy observed(engine, &QCCurlMultiManager::requestFinished);
    QCCurlHandleManager handle;
    CURL *easy = handle.handle();
    QCOMPARE(state->activeEasyHandle, nullptr);
    std::optional<QCCurlHandleManager> detached;
    int completions   = 0;
    const auto result = engine->admitHttpReply(
        std::move(handle),
        {&reply, nullptr, state},
        [&](QCCurlHandleManager &&returned, CURLcode code, long status) {
            ++completions;
            QCOMPARE(code, CURLE_ABORTED_BY_CALLBACK);
            QCOMPARE(status, 0L);
            QCOMPARE(state->activeEasyHandle, nullptr);
            QCOMPARE(engine->runningRequestsCount(), 0);
            char *userdata = reinterpret_cast<char *>(quintptr(1));
            QCOMPARE(curl_easy_getinfo(returned.handle(), CURLINFO_PRIVATE, &userdata), CURLE_OK);
            QCOMPARE(userdata, nullptr);
            detached.emplace(std::move(returned));
        });
    QCOMPARE(result.state, AdmissionState::Accepted);
    QVERIFY(result.token != 0);
    QVERIFY(!result.returnedHandle);
    QVERIFY(!handle.isValid());
    QCOMPARE(state->activeEasyHandle, easy);
    const int cleanups = QCCurlHandleManager::cleanupCountForTest();
    engine->removeTransfer(result.token);
    QCOMPARE(completions, 1);
    QCOMPARE(observed.size(), 1);
    QVERIFY(detached && detached->handle() == easy);
    QCOMPARE(QCCurlHandleManager::cleanupCountForTest(), cleanups);
    engine->removeTransfer(result.token);
    QCOMPARE(completions, 1);
    detached.reset();
    QCOMPARE(QCCurlHandleManager::cleanupCountForTest(), cleanups + 1);
}

void tst_QCCurlHttpAdmission::rejectionReturnsSafeHandle_data()
{
    QTest::addColumn<QByteArray>("option");
    QTest::newRow("multi-add") << QByteArray();
    QTest::newRow("multi-limits") << QByteArrayLiteral("CURLMOPT_MAX_TOTAL_CONNECTIONS");
    QTest::newRow("callback-bind") << QByteArrayLiteral("CURLOPT_XFERINFODATA");
}

void tst_QCCurlHttpAdmission::rejectionReturnsSafeHandle()
{
    QFETCH(QByteArray, option);
    auto state = makeState();
    QCNetworkReply reply(QCNetworkReply::TestOnlyKey{},
                         state->curlPlan.normalized.request,
                         HttpMethod::Get);
    QCCurlHandleManager handle;
    CURL *easy                = handle.handle();
    int completions           = 0;
    auto *pool                = QCNetworkConnectionPoolManager::instance();
    const auto previousConfig = pool->config();
    if (option.startsWith("CURLMOPT_")) {
        auto config = previousConfig;
        config.setMultiMaxTotalConnections(7);
        QCOMPARE(pool->setConfig(config), QCNetworkConnectionPoolManager::UpdateResult::Applied);
    }
    const char *variable = option.isEmpty() || option.startsWith("CURLMOPT_")
                               ? "QCURL_TEST_FORCE_MULTI_FAILURE"
                               : "QCURL_TEST_FORCE_SETOPT_ERROR";
    qputenv(variable, option.isEmpty() ? QByteArrayLiteral("add") : option);
    const auto restore = qScopeGuard([variable, pool, previousConfig]() {
        qunsetenv(variable);
        QCOMPARE(pool->setConfig(previousConfig),
                 QCNetworkConnectionPoolManager::UpdateResult::Applied);
    });
    auto result = QCCurlMultiManager::instance()->admitHttpReply(std::move(handle),
                                                                 {&reply, nullptr, state},
                                                                 [&](QCCurlHandleManager &&,
                                                                     CURLcode,
                                                                     long) { ++completions; });
    QCOMPARE(result.state, AdmissionState::Rejected);
    QCOMPARE(result.error, NetworkError::InvalidRequest);
    QVERIFY(!result.errorMessage.isEmpty());
    QCOMPARE(result.token, QCCurlMultiManager::TransferToken(0));
    QVERIFY(result.returnedHandle && result.returnedHandle->handle() == easy);
    QVERIFY(!handle.isValid());
    QCOMPARE(state->activeEasyHandle, nullptr);
    QCOMPARE(completions, 0);
    QVERIFY(!QCCurlMultiManager::instance()->isPoisonedForTest());
    char *userdata = reinterpret_cast<char *>(quintptr(1));
    QCOMPARE(curl_easy_getinfo(easy, CURLINFO_PRIVATE, &userdata), CURLE_OK);
    QCOMPARE(userdata, nullptr);
}

void tst_QCCurlHttpAdmission::callbackFlagsUseSharedState_data()
{
    QTest::addColumn<QByteArray>("option");
    QTest::addColumn<bool>("enabled");
    QTest::newRow("read-off") << QByteArrayLiteral("CURLOPT_READFUNCTION") << false;
    QTest::newRow("read-on") << QByteArrayLiteral("CURLOPT_READFUNCTION") << true;
    QTest::newRow("debug-off") << QByteArrayLiteral("CURLOPT_DEBUGFUNCTION") << false;
    QTest::newRow("debug-on") << QByteArrayLiteral("CURLOPT_DEBUGFUNCTION") << true;
}

void tst_QCCurlHttpAdmission::callbackFlagsUseSharedState()
{
    QFETCH(QByteArray, option);
    QFETCH(bool, enabled);
    auto state = makeState();
    QCNetworkReply reply(QCNetworkReply::TestOnlyKey{},
                         state->curlPlan.normalized.request,
                         HttpMethod::Get);
    state->readCallbackConfigured  = enabled && option.contains("READ");
    state->debugCallbackConfigured = enabled && option.contains("DEBUG");
    QCCurlHandleManager handle;
    qputenv("QCURL_TEST_FORCE_SETOPT_ERROR", option);
    const auto restore = qScopeGuard([]() { qunsetenv("QCURL_TEST_FORCE_SETOPT_ERROR"); });
    const auto result  = QCCurlMultiManager::instance()->admitHttpReply(std::move(handle),
                                                                        {&reply, nullptr, state},
                                                                        [](QCCurlHandleManager &&,
                                                                           CURLcode,
                                                                           long) {});
    QCOMPARE(result.state, enabled ? AdmissionState::Rejected : AdmissionState::Accepted);
    if (enabled) {
        QVERIFY(result.returnedHandle && result.returnedHandle->isValid());
        QVERIFY(result.errorMessage.contains(QString::fromLatin1(option)));
    } else {
        QCCurlMultiManager::instance()->removeTransfer(result.token);
    }
}

void tst_QCCurlHttpAdmission::explicitManagerControlsShare()
{
    QCNetworkAccessManager manager;
    QCNetworkAccessManager::ShareHandleConfig config;
    config.setShareCookies(true);
    manager.setShareHandleConfig(config);
    auto *engine = QCCurlMultiManager::instance();
    auto state   = makeState();
    QCNetworkReply reply(QCNetworkReply::TestOnlyKey{},
                         state->curlPlan.normalized.request,
                         HttpMethod::Get);
    QCCurlHandleManager handle;
    const auto result = engine->admitHttpReply(std::move(handle),
                                               {&reply, &manager, state},
                                               [](QCCurlHandleManager &&, CURLcode, long) {});
    QCOMPARE(result.state, AdmissionState::Accepted);
    QCOMPARE(reply.parent(), nullptr);
    QCOMPARE(QCCurlMultiManagerTestAccess::activeShareUsers(engine), 1);
    engine->removeTransfer(result.token);
    QCOMPARE(QCCurlMultiManagerTestAccess::activeShareUsers(engine), 0);
}

void tst_QCCurlHttpAdmission::warningsStayInAdmissionResult()
{
    QCNetworkAccessManager manager;
    QCNetworkAccessManager::ShareHandleConfig config;
    config.setShareCookies(true);
    manager.setShareHandleConfig(config);
    auto *engine     = QCCurlMultiManager::instance();
    auto firstState  = makeState();
    auto secondState = makeState();
    QCNetworkReply first(QCNetworkReply::TestOnlyKey{},
                         firstState->curlPlan.normalized.request,
                         HttpMethod::Get);
    QCNetworkReply second(QCNetworkReply::TestOnlyKey{},
                          secondState->curlPlan.normalized.request,
                          HttpMethod::Get);
    QCCurlHandleManager firstHandle;
    QCCurlHandleManager secondHandle;
    const auto ignoreCompletion = [](QCCurlHandleManager &&, CURLcode, long) {};
    const auto firstResult      = engine->admitHttpReply(std::move(firstHandle),
                                                         {&first, &manager, firstState},
                                                         ignoreCompletion);
    QCOMPARE(firstResult.state, AdmissionState::Accepted);
    config.setShareSslSession(true);
    manager.setShareHandleConfig(config);
    const auto secondResult = engine->admitHttpReply(std::move(secondHandle),
                                                     {&second, &manager, secondState},
                                                     ignoreCompletion);
    QCOMPARE(secondResult.state, AdmissionState::Accepted);
    QCOMPARE(secondResult.warnings.size(), 1);
    QVERIFY(secondResult.warnings.first().contains(QStringLiteral("延迟生效")));
    QVERIFY(second.capabilityWarnings().isEmpty());
    engine->removeTransfer(firstResult.token);
    engine->removeTransfer(secondResult.token);
}

void tst_QCCurlHttpAdmission::wrongThreadAdmissionIsRejected()
{
    auto *engine = QCCurlMultiManager::instance();
    auto state   = makeState();
    QCNetworkReply reply(QCNetworkReply::TestOnlyKey{},
                         state->curlPlan.normalized.request,
                         HttpMethod::Get);
    QCCurlHandleManager handle;
    CURL *easy = handle.handle();
    std::optional<QCCurlMultiManager::HttpAdmissionResult> result;
    std::thread worker([&]() {
        result.emplace(engine->admitHttpReply(std::move(handle),
                                              {&reply, nullptr, state},
                                              [](QCCurlHandleManager &&, CURLcode, long) {}));
    });
    worker.join();
    QVERIFY(result);
    QCOMPARE(result->state, AdmissionState::Rejected);
    QVERIFY(result->errorMessage.contains(QStringLiteral("owner thread")));
    QVERIFY(result->returnedHandle && result->returnedHandle->handle() == easy);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCOMPARE(engine->runningRequestsCount(), 0);
    QCOMPARE(state->activeEasyHandle, nullptr);
}

void tst_QCCurlHttpAdmission::tokenRemovalFromAnotherThreadIsQueued()
{
    auto *engine = QCCurlMultiManager::instance();
    auto state   = makeState();
    QCNetworkReply reply(QCNetworkReply::TestOnlyKey{},
                         state->curlPlan.normalized.request,
                         HttpMethod::Get);
    QCCurlHandleManager handle;
    int completions           = 0;
    QThread *completionThread = nullptr;
    const auto result         = engine->admitHttpReply(std::move(handle),
                                                       {&reply, nullptr, state},
                                                       [&](QCCurlHandleManager &&, CURLcode, long) {
                                                   ++completions;
                                                   completionThread = QThread::currentThread();
                                                       });
    QCOMPARE(result.state, AdmissionState::Accepted);
    std::thread worker([engine, token = result.token]() { engine->removeTransfer(token); });
    worker.join();
    QCOMPARE(completions, 0);
    QCOMPARE(engine->runningRequestsCount(), 1);
    QCoreApplication::sendPostedEvents(engine, QEvent::MetaCall);
    QCOMPARE(completions, 1);
    QCOMPARE(completionThread, engine->thread());
    QCOMPARE(state->activeEasyHandle, nullptr);
}

void tst_QCCurlHttpAdmission::callbackRemovalPreservesStateUntilDetach()
{
    auto *engine = QCCurlMultiManager::instance();
    auto state   = makeState();
    QCNetworkReply reply(QCNetworkReply::TestOnlyKey{},
                         state->curlPlan.normalized.request,
                         HttpMethod::Get);
    QWeakPointer<QCNetworkReplyTransferState> weakState(state);
    QCCurlHandleManager handle;
    CURL *easy      = handle.handle();
    int completions = 0;
    const auto result
        = engine->admitHttpReply(std::move(handle),
                                 {&reply, nullptr, state},
                                 [&](QCCurlHandleManager &&returned, CURLcode, long) {
                                     ++completions;
                                     QCOMPARE(returned.handle(), easy);
                                     QVERIFY(!weakState.isNull());
                                     QCOMPARE(weakState.toStrongRef()->activeEasyHandle, nullptr);
                                 });
    QCOMPARE(result.state, AdmissionState::Accepted);
    const int cleanups = QCCurlHandleManager::cleanupCountForTest();
    state.clear();
    {
        Internal::ReplyCurlCallbackScope callback;
        engine->removeTransfer(result.token);
        engine->removeTransfer(result.token);
        QCOMPARE(completions, 0);
        QCOMPARE(weakState.toStrongRef()->activeEasyHandle, easy);
        QCOMPARE(QCCurlHandleManager::cleanupCountForTest(), cleanups);
    }
    QCoreApplication::sendPostedEvents(engine, QEvent::MetaCall);
    QCOMPARE(completions, 1);
    QVERIFY(weakState.isNull());
    QCOMPARE(QCCurlHandleManager::cleanupCountForTest(), cleanups + 1);
}

void tst_QCCurlHttpAdmission::stateOutlivesDestroyedObserver()
{
    auto *engine = QCCurlMultiManager::instance();
    auto state   = makeState();
    auto *reply  = new QCNetworkReply(QCNetworkReply::TestOnlyKey{},
                                      state->curlPlan.normalized.request,
                                      HttpMethod::Get);
    QWeakPointer<QCNetworkReplyTransferState> weakState(state);
    QCCurlHandleManager handle;
    int completions   = 0;
    const auto result = engine->admitHttpReply(std::move(handle),
                                               {reply, nullptr, state},
                                               [&](QCCurlHandleManager &&returned, CURLcode, long) {
                                                   ++completions;
                                                   QVERIFY(returned.isValid());
                                                   QVERIFY(!weakState.isNull());
                                               });
    QCOMPARE(result.state, AdmissionState::Accepted);
    delete reply;
    state.clear();
    QVERIFY(!weakState.isNull());
    engine->removeTransfer(result.token);
    QCOMPARE(completions, 1);
    QVERIFY(weakState.isNull());
}

QTEST_GUILESS_MAIN(tst_QCCurlHttpAdmission)
