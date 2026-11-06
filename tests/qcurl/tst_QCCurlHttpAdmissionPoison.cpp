#include "QCCurlMultiManager.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkConnectionPoolManager.h"
#include "QCNetworkReply.h"
#include "QCurlRuntime.h"
#include "private/QCNetworkReplyTransferState_p.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QtTest>

using namespace QCurl;

class tst_QCCurlHttpAdmissionPoison final : public QObject
{
    Q_OBJECT

public:
    tst_QCCurlHttpAdmissionPoison() = default;

private Q_SLOTS:
    void poisonRetainsOwnership();

private:
    Q_DISABLE_COPY_MOVE(tst_QCCurlHttpAdmissionPoison)

    void verifyRejectedOwnership();
    void verifyActiveReplyPoison();
};

void tst_QCCurlHttpAdmissionPoison::verifyRejectedOwnership()
{
    QCNetworkAccessManager manager;
    QCNetworkAccessManager::ShareHandleConfig config;
    config.setShareCookies(true);
    manager.setShareHandleConfig(config);
    const QUrl url(QStringLiteral("http://127.0.0.1:1/rollback"));
    auto state = QSharedPointer<QCNetworkReplyTransferState>::create(
        Internal::compileRequest(Internal::normalizeRequest(QCNetworkRequest(url),
                                                            HttpMethod::Get,
                                                            Internal::makeEmptyRequestBody())));
    QWeakPointer<QCNetworkReplyTransferState> weakState(state);
    auto *reply = new QCNetworkReply(QCNetworkReply::TestOnlyKey{},
                                     QCNetworkRequest(url),
                                     HttpMethod::Get);
    QCCurlHandleManager handle;
    const int cleanups = QCCurlHandleManager::cleanupCountForTest();
    const int pending  = QCCurlMultiManager::quarantinedPendingTransfersForTest();
    qputenv("QCURL_TEST_FORCE_MULTI_FAILURE", "add");
    qputenv("QCURL_TEST_FORCE_COOKIE_OPTION_ERROR", "rollback:CURLOPT_SHARE");
    int completions = 0;
    const auto result = QCCurlMultiManager::instance()->admitHttpReply(std::move(handle),
                                                                       {reply, &manager, state},
                                                                       [&](QCCurlHandleManager &&,
                                                                           CURLcode,
                                                                           long) { ++completions; });
    QCOMPARE(result.state, QCCurlMultiManager::HttpAdmissionState::Quarantined);
    QVERIFY(!result.returnedHandle);
    QCOMPARE(result.token, QCCurlMultiManager::TransferToken(0));
    QVERIFY(result.errorMessage.contains(QStringLiteral("poisoned")));
    QCOMPARE(state->activeEasyHandle, nullptr);
    QCOMPARE(completions, 0);
    QCOMPARE(QCCurlHandleManager::cleanupCountForTest(), cleanups);
    QCOMPARE(QCCurlMultiManager::quarantinedPendingTransfersForTest(), pending + 1);
    delete reply;
    state.clear();
    QVERIFY(!weakState.isNull());
    QCOMPARE(QCCurlHandleManager::cleanupCountForTest(), cleanups + 1);
    QVERIFY(QCCurlMultiManager::quarantineIsNonCallableForTest());
}

void tst_QCCurlHttpAdmissionPoison::verifyActiveReplyPoison()
{
    auto *pool = QCNetworkConnectionPoolManager::instance();
    pool->resetStatistics();
    QCNetworkAccessManager manager;
    auto *reply = manager.get(QCNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:1/active"))));
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    QSignalSpy errors(reply, qOverload<NetworkError>(&QCNetworkReply::error));
    reply->execute();
    QCOMPARE(reply->state(), ReplyState::Running);
    QCOMPARE(QCCurlMultiManager::instance()->runningRequestsCount(), 1);
    const int cleanups = QCCurlHandleManager::cleanupCountForTest();
    QCCurlMultiManager::instance()->processUnknownDoneForTest();
    QCOMPARE(finished.size(), 0);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCOMPARE(finished.size(), 1);
    QCOMPARE(errors.size(), 1);
    QCOMPARE(reply->state(), ReplyState::Error);
    QCOMPARE(reply->error(), NetworkError::Unknown);
    QVERIFY(reply->errorString().contains(QStringLiteral("poisoned")));
    QCOMPARE(pool->statistics().activeRequests(), 0);
    QCOMPARE(pool->statistics().totalRequests(), 1);
    delete reply;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    // 隔离通知不归还活动 handle；reply 析构与后续事件均不能 cleanup 它。
    QCOMPARE(QCCurlHandleManager::cleanupCountForTest(), cleanups);
    QCOMPARE(QCCurlMultiManager::instance()->runningRequestsCount(), 1);
    QCOMPARE(finished.size(), 1);
}

void tst_QCCurlHttpAdmissionPoison::poisonRetainsOwnership()
{
    QCurlRuntime runtime;
    QVERIFY(runtime.isProcessOwner());
    const QByteArray scenario = qgetenv("QCURL_TEST_HTTP_ADMISSION_POISON");
    if (scenario == "rollback") {
        verifyRejectedOwnership();
    } else {
        QCOMPARE(scenario, QByteArrayLiteral("active"));
        verifyActiveReplyPoison();
    }
    QVERIFY(QCCurlMultiManager::instance()->isPoisonedForTest());
    QCOMPARE(runtime.state(), QCurlRuntimeState::Failed);
}

QTEST_GUILESS_MAIN(tst_QCCurlHttpAdmissionPoison)
#include "tst_QCCurlHttpAdmissionPoison.moc"
