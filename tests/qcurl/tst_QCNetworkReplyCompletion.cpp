#include "QCCurlHandleManager.h"
#include "QCNetworkConnectionPoolManager.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkSslConfig.h"
#include "private/QCNetworkReplyExecution_p.h"
#include "private/QCNetworkReplyTransferState_p.h"
#include "test_source_paths.h"
#include "tst_QCCurlHttpAdmission.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QThread>
#include <QtTest>

#include <memory>
#include <thread>

using namespace QCurl;
using Execution = Internal::QCNetworkReplyExecution;

namespace {

QSharedPointer<QCNetworkReplyTransferState> makeObservedState(
    const QSharedPointer<int> &cleanupsAtRelease)
{
    return QSharedPointer<QCNetworkReplyTransferState>(
        new QCNetworkReplyTransferState(Internal::compileRequest(
            Internal::normalizeRequest(QCNetworkRequest(
                                           QUrl(QStringLiteral("https://localhost/backing"))),
                                       HttpMethod::Get,
                                       Internal::makeEmptyRequestBody()))),
        [cleanupsAtRelease](QCNetworkReplyTransferState *state) {
            *cleanupsAtRelease = QCCurlHandleManager::cleanupCountForTest();
            delete state;
        });
}

bool configureTlsBacking(QCNetworkReplyTransferState &state, QCCurlHandleManager &handle)
{
    const QString prefix = TestSourcePaths::sourcePath(
        QStringLiteral("tests/qcurl/testdata/http2/"));
    QCNetworkSslConfig endpoint;
    endpoint.setCaCertPath(prefix + QStringLiteral("localhost.crt"));
    endpoint.setClientCertPath(prefix + QStringLiteral("localhost.crt"));
    endpoint.setClientKeyPath(prefix + QStringLiteral("localhost-encrypted.key"));
    endpoint.setClientKeyPassword(QStringLiteral("qcurl-test-passphrase"));
    endpoint.setPinnedPublicKey(
        QStringLiteral("sha256//c+b6Q1AtXTCPl1keWd2pfGndgov8AGKedY7U9Nk7Ib8="));
    endpoint.setCipherList(QStringLiteral("ECDHE-RSA-AES128-GCM-SHA256"));
    endpoint.setTls13Ciphers(QStringLiteral("TLS_AES_128_GCM_SHA256"));
    QCNetworkProxyConfig::ProxyTlsConfig proxy;
    proxy.setCaCertPath(prefix + QStringLiteral("localhost.crt"));
    proxy.setCipherList(endpoint.cipherList());
    proxy.setTls13Ciphers(endpoint.tls13Ciphers());
    const auto endpointResult = state.tlsConfiguration.applyEndpoint(handle.handle(), endpoint);
    const auto proxyResult    = state.tlsConfiguration.applyProxy(
        handle.handle(), {QCNetworkProxyConfig::ProxyType::Https, CURLPROXY_HTTPS, proxy});
    return !endpointResult.failure.has_value() && !proxyResult.failure.has_value();
}

} // namespace

void tst_QCCurlHttpAdmission::completionUsesReplyThread_data()
{
    QTest::addColumn<bool>("crossThread");
    QTest::newRow("direct") << false;
    QTest::newRow("queued") << true;
}

void tst_QCCurlHttpAdmission::completionUsesReplyThread()
{
    QFETCH(bool, crossThread);
    auto reply = std::make_unique<QCNetworkReply>(QCNetworkReply::TestOnlyKey{},
                                                  QCNetworkRequest(QUrl(QStringLiteral(
                                                      "http://127.0.0.1:1/completion"))),
                                                  HttpMethod::Get);
    QSignalSpy finished(reply.get(), &QCNetworkReply::finished);
    QThread *completionThread = nullptr;
    connect(reply.get(), &QCNetworkReply::finished, reply.get(), [&]() {
        completionThread = QThread::currentThread();
    });
    const auto cleanupsAtRelease = QSharedPointer<int>::create(-1);
    auto backing                 = makeObservedState(cleanupsAtRelease);
    QWeakPointer<QCNetworkReplyTransferState> weakState(backing);
    auto completion = Execution::completionHandler(reply.get());
    QCCurlHandleManager handle;
    QVERIFY(configureTlsBacking(*backing, handle));
    handle.retainTransferState(backing);
    backing.clear();
    if (crossThread) {
        std::thread worker([&]() { completion(std::move(handle), CURLE_OK, 200); });
        worker.join();
        QCOMPARE(finished.size(), 0);
        QVERIFY(!handle.isValid());
        QVERIFY(!weakState.isNull());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    } else {
        completion(std::move(handle), CURLE_OK, 200);
    }
    QCOMPARE(finished.size(), 1);
    QCOMPARE(reply->state(), ReplyState::Finished);
    QCOMPARE(reply->httpStatusCode(), 200);
    QCOMPARE(completionThread, reply->thread());
    QVERIFY(!weakState.isNull());
    const int cleanups = QCCurlHandleManager::cleanupCountForTest();
    reply.reset();
    QVERIFY(weakState.isNull());
    QCOMPARE(*cleanupsAtRelease, cleanups + 1);
}

void tst_QCCurlHttpAdmission::destroyedReplyDropsCompletion_data()
{
    QTest::addColumn<bool>("deleteAfterQueue");
    QTest::newRow("before-dispatch") << false;
    QTest::newRow("after-queue-before-delivery") << true;
}

void tst_QCCurlHttpAdmission::destroyedReplyDropsCompletion()
{
    QFETCH(bool, deleteAfterQueue);
    auto *reply     = new QCNetworkReply(QCNetworkReply::TestOnlyKey{},
                                         QCNetworkRequest(QUrl(QStringLiteral(
                                             "http://127.0.0.1:1/destroyed-completion"))),
                                         HttpMethod::Get);
    auto completion = Execution::completionHandler(reply);
    const auto cleanupsAtRelease = QSharedPointer<int>::create(-1);
    auto state                   = makeObservedState(cleanupsAtRelease);
    QWeakPointer<QCNetworkReplyTransferState> weakState(state);
    QCCurlHandleManager detached;
    QVERIFY(configureTlsBacking(*state, detached));
    detached.retainTransferState(state);
    state.clear();
    QSignalSpy destroyed(reply, &QObject::destroyed);
    int terminalSignals = 0;
    connect(reply, &QCNetworkReply::finished, this, [&]() { ++terminalSignals; });
    const int cleanups = QCCurlHandleManager::cleanupCountForTest();
    if (!deleteAfterQueue) {
        delete reply;
    }
    std::thread worker([completion, handle = std::move(detached)]() mutable {
        completion(std::move(handle), CURLE_OK, 200);
    });
    worker.join();
    if (deleteAfterQueue) {
        QCOMPARE(QCCurlHandleManager::cleanupCountForTest(), cleanups);
        QVERIFY(!weakState.isNull());
        delete reply;
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCOMPARE(destroyed.size(), 1);
    QCOMPARE(terminalSignals, 0);
    QVERIFY(weakState.isNull());
    // 原 reply handle 与唯一 detached handle 各释放一次；丢弃事件也必须释放 capture。
    QCOMPARE(QCCurlHandleManager::cleanupCountForTest(), cleanups + 2);
    QCOMPARE(*cleanupsAtRelease, cleanups + 2);
}

void tst_QCCurlHttpAdmission::warningsPrecedeQueuedFailure()
{
    QCNetworkReply reply(QCNetworkReply::TestOnlyKey{},
                         QCNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:1/warnings"))),
                         HttpMethod::Get);
    QSignalSpy finished(&reply, &QCNetworkReply::finished);
    QSignalSpy errors(&reply, qOverload<NetworkError>(&QCNetworkReply::error));
    const auto before      = QCNetworkConnectionPoolManager::instance()->statistics();
    QThread *warningThread = nullptr;
    QStringList warningsAtError;
    connect(&reply, qOverload<NetworkError>(&QCNetworkReply::error), &reply, [&]() {
        warningThread   = QThread::currentThread();
        warningsAtError = reply.capabilityWarnings();
    });
    QCCurlMultiManager::HttpAdmissionResult result;
    result.errorMessage = QStringLiteral("admission rejected");
    result.warnings     = {QStringLiteral("share 配置延迟生效")};
    result.returnedHandle.emplace();
    const int cleanups = QCCurlHandleManager::cleanupCountForTest();
    Execution::consumeAdmission(&reply, std::move(result));
    QCOMPARE(finished.size(), 0);
    QCOMPARE(errors.size(), 0);
    QCOMPARE(reply.capabilityWarnings(), QStringList{QStringLiteral("share 配置延迟生效")});
    // 旧 reply handle 已由安全返回的 handle 替换，尚未分发失败事件。
    QCOMPARE(QCCurlHandleManager::cleanupCountForTest(), cleanups + 1);
    QCoreApplication::sendPostedEvents(&reply, QEvent::MetaCall);
    QCOMPARE(finished.size(), 1);
    QCOMPARE(errors.size(), 1);
    QCOMPARE(warningThread, reply.thread());
    QCOMPARE(warningsAtError, reply.capabilityWarnings());
    QCOMPARE(reply.error(), NetworkError::InvalidRequest);
    QCOMPARE(QCNetworkConnectionPoolManager::instance()->statistics().totalRequests(),
             before.totalRequests());
}
