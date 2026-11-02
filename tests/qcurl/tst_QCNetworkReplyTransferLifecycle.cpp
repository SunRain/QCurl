#include "QCCurlMultiManager.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkConnectionPoolManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "private/QCNetworkReplyCallbacks_p.h"
#include "qcnetwork_retry_policy_test_helper.h"
#include "qcurl_http_script_server.h"
#include "tst_QCCurlHttpAdmission.h"

#include <QCoreApplication>
#include <QPointer>
#include <QSignalSpy>
#include <QtTest>

using namespace QCurl;
using namespace std::chrono_literals;

void tst_QCCurlHttpAdmission::callbackCancelAndAbort_data()
{
    QTest::addColumn<bool>("abort");
    QTest::newRow("cancel-twice") << false;
    QTest::newRow("abort-twice") << true;
}

void tst_QCCurlHttpAdmission::callbackCancelAndAbort()
{
    QFETCH(bool, abort);
    HttpScriptServer server({HttpScriptServer::response(200, QByteArrayLiteral("payload"))});
    QVERIFY(server.start());
    auto *pool = QCNetworkConnectionPoolManager::instance();
    pool->resetStatistics();
    QCNetworkAccessManager manager;
    auto *reply = manager.get(QCNetworkRequest(server.url()));
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    QSignalSpy errors(reply, qOverload<NetworkError>(&QCNetworkReply::error));
    QSignalSpy cancelled(reply, &QCNetworkReply::cancelled);
    int callbacks = 0;
    connect(reply, &QCNetworkReply::readyRead, reply, [&, reply]() {
        ++callbacks;
        const int cleanups = QCCurlHandleManager::cleanupCountForTest();
        if (abort) {
            reply->abortWithError(NetworkError::InvalidRequest, QStringLiteral("test abort"));
            reply->abortWithError(NetworkError::InvalidRequest, QStringLiteral("test abort"));
        } else {
            reply->cancel();
            reply->cancel();
        }
        QCOMPARE(QCCurlMultiManager::instance()->runningRequestsCount(), 1);
        QCOMPARE(QCCurlHandleManager::cleanupCountForTest(), cleanups);
    });
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(QCCurlMultiManager::instance()->runningRequestsCount(), 0, 3000);
    QCOMPARE(callbacks, 1);
    QCOMPARE(reply->state(), abort ? ReplyState::Error : ReplyState::Cancelled);
    QCOMPARE(errors.size(), abort ? 1 : 0);
    QCOMPARE(cancelled.size(), abort ? 0 : 1);
    QCOMPARE(pool->statistics().activeRequests(), 0);
    QCOMPARE(pool->statistics().totalRequests(), 1);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCOMPARE(finished.size(), 1);
}

void tst_QCCurlHttpAdmission::replyDestroyedInCallback()
{
    HttpScriptServer server({HttpScriptServer::response(200, QByteArrayLiteral("payload"))});
    QVERIFY(server.start());
    auto *pool = QCNetworkConnectionPoolManager::instance();
    pool->resetStatistics();
    QCNetworkAccessManager manager;
    QPointer<QCNetworkReply> reply(manager.get(QCNetworkRequest(server.url())));
    int callbacks = 0;
    connect(reply, &QCNetworkReply::readyRead, this, [&]() {
        ++callbacks;
        const int cleanups = QCCurlHandleManager::cleanupCountForTest();
        delete reply.data();
        QVERIFY(reply.isNull());
        QCOMPARE(QCCurlMultiManager::instance()->runningRequestsCount(), 1);
        QCOMPARE(QCCurlHandleManager::cleanupCountForTest(), cleanups);
    });
    QTRY_VERIFY_WITH_TIMEOUT(reply.isNull(), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(QCCurlMultiManager::instance()->runningRequestsCount(), 0, 3000);
    QCOMPARE(callbacks, 1);
    QCOMPARE(pool->statistics().activeRequests(), 0);
    QCOMPARE(pool->statistics().totalRequests(), 1);
}

void tst_QCCurlHttpAdmission::retryCountsOneLogicalRequest()
{
    HttpScriptServer server({HttpScriptServer::response(503, QByteArrayLiteral("retry")),
                             HttpScriptServer::response(200, QByteArrayLiteral("success"))});
    QVERIFY(server.start());
    auto *pool = QCNetworkConnectionPoolManager::instance();
    pool->resetStatistics();
    QCNetworkAccessManager manager;
    QCNetworkRequest request(server.url());
    request.setRetryPolicy(TestSupport::makeRetryPolicyOrFail(1, 1ms));
    auto *reply = manager.get(request);
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    QSignalSpy retried(reply, &QCNetworkReply::retryAttempt);
    connect(reply, &QCNetworkReply::retryAttempt, reply, [pool]() {
        QCOMPARE(pool->statistics().activeRequests(), 1);
        QCOMPARE(pool->statistics().totalRequests(), 0);
    });
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->readAll().value(), QByteArrayLiteral("success"));
    QCOMPARE(retried.size(), 1);
    QCOMPARE(server.requests().size(), 2);
    QCOMPARE(pool->statistics().activeRequests(), 0);
    QCOMPARE(pool->statistics().totalRequests(), 1);
}

void tst_QCCurlHttpAdmission::retryKeepsProgressCallbacksEnabled()
{
    HttpScriptServer server({HttpScriptServer::response(503, QByteArrayLiteral("retry")),
                             HttpScriptServer::response(200, QByteArrayLiteral("success"))});
    QVERIFY(server.start());
    QCNetworkAccessManager manager;
    QCNetworkRequest request(server.url());
    request.setRetryPolicy(TestSupport::makeRetryPolicyOrFail(1, 1ms));
    auto *reply = manager.get(request);
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    QSignalSpy retried(reply, &QCNetworkReply::retryAttempt);
    int attempt              = 0;
    int firstAttemptProgress = 0;
    int retryProgress        = 0;
    connect(reply, &QCNetworkReply::retryAttempt, reply, [&]() { ++attempt; });
    connect(reply, &QCNetworkReply::downloadProgress, reply, [&]() {
        if (attempt == 0) {
            ++firstAttemptProgress;
        } else {
            ++retryProgress;
        }
    });
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->readAll().value(), QByteArrayLiteral("success"));
    QCOMPARE(retried.size(), 1);
    QVERIFY(firstAttemptProgress > 0);
    // detach 会禁用 progress；再次 admission 必须恢复，否则 retry 静默丢失进度事件。
    QVERIFY(retryProgress > 0);
}

void tst_QCCurlHttpAdmission::cancellationBeforeAdmissionDoesNotStartRequest()
{
    HttpScriptServer server({HttpScriptServer::response(200, QByteArrayLiteral("unused"))});
    QVERIFY(server.start());
    auto *pool = QCNetworkConnectionPoolManager::instance();
    pool->resetStatistics();
    QCNetworkAccessManager manager;
    auto *reply = manager.get(QCNetworkRequest(server.url()));
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    connect(reply, &QCNetworkReply::stateChanged, reply, [reply](ReplyState state) {
        if (state == ReplyState::Running) {
            reply->cancel();
        }
    });
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
    QCOMPARE(reply->state(), ReplyState::Cancelled);
    QCOMPARE(QCCurlMultiManager::instance()->runningRequestsCount(), 0);
    QCOMPARE(server.requests().size(), 0);
    QCOMPARE(pool->statistics().activeRequests(), 0);
    QCOMPARE(pool->statistics().totalRequests(), 0);
}

void tst_QCCurlHttpAdmission::acceptedRetryKeepsCompletionTokenCleared()
{
    HttpScriptServer server({HttpScriptServer::response(503, QByteArrayLiteral("retry")),
                             HttpScriptServer::response(200, QByteArrayLiteral("success"))});
    QVERIFY(server.start());
    auto *pool = QCNetworkConnectionPoolManager::instance();
    pool->resetStatistics();
    QCNetworkAccessManager manager;
    QCNetworkRequest request(server.url());
    request.setRetryPolicy(TestSupport::makeRetryPolicyOrFail(1, 1000ms));
    auto *reply = manager.get(request);
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    QSignalSpy retried(reply, &QCNetworkReply::retryAttempt);
    connect(reply, &QCNetworkReply::retryAttempt, reply, [reply]() {
        // 完成后的再次执行必须使用新 admission/token，而不是仍观察旧 transfer。
        QMetaObject::invokeMethod(reply, &QCNetworkReply::execute, Qt::QueuedConnection);
    });
    connect(reply, &QCNetworkReply::readyRead, reply, [reply]() {
        if (reply->httpStatusCode() == 200) {
            reply->cancel();
            // 仅在回调内仍注册且有 token 才能在下一安全点真正移除。
            QVERIFY(Internal::isInReplyCurlCallback());
            QCOMPARE(QCCurlMultiManager::instance()->runningRequestsCount(), 1);
        }
    });
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(QCCurlMultiManager::instance()->runningRequestsCount(), 0, 3000);
    QCOMPARE(retried.size(), 1);
    QCOMPARE(server.requests().size(), 2);
    QCOMPARE(reply->state(), ReplyState::Cancelled);
    QCOMPARE(pool->statistics().activeRequests(), 0);
    QCOMPARE(pool->statistics().totalRequests(), 1);
}
