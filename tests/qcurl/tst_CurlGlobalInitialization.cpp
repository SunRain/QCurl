/**
 * @file
 * @brief 验证 libcurl 全局初始化失败的统一传播合同。
 */

#include "QCBlockingNetworkClient.h"
#include "QCCurlHandleManager.h"
#include "QCCurlMultiManager.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "private/QCurlRuntimeState_p.h"

#include <QtTest/QtTest>

#include <curl/curl.h>

using namespace QCurl;

class tst_CurlGlobalInitialization : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void forcedFailurePropagatesOneRootCause();
};

void tst_CurlGlobalInitialization::forcedFailurePropagatesOneRootCause()
{
    qputenv("QCURL_TEST_FORCE_GLOBAL_INIT_ERROR",
            QByteArray::number(static_cast<int>(CURLE_FAILED_INIT)));

    const auto lease = Internal::acquireRuntimeLease();
    QVERIFY(!lease.isValid());
    const QString diagnostic = lease.diagnostic();
    QVERIFY(diagnostic.contains(QStringLiteral("curl_global_init")));
    QVERIFY(diagnostic.contains(QString::fromUtf8(curl_easy_strerror(CURLE_FAILED_INIT))));

    QCCurlHandleManager handleManager;
    QVERIFY(!handleManager.isValid());
    QCOMPARE(handleManager.initializationError(), diagnostic);

    const auto *multiManager = QCCurlMultiManager::instance();
    QVERIFY(!multiManager->isReady());
    QCOMPARE(multiManager->initializationError(), diagnostic);

    QCBlockingNetworkClient::Options options;
    options.setApplicationThreadPolicy(
        QCBlockingNetworkClient::ApplicationThreadPolicy::AllowForCliOrTests);
    const QCBlockingNetworkClient blockingClient(options);
    const QCNetworkRequest request(QUrl(QStringLiteral("http://example.com/global-init")));
    const auto blockingResult = blockingClient.get(request);
    QVERIFY(!blockingResult.isSuccess());
    QCOMPARE(blockingResult.errorMessage(), diagnostic);

    QCNetworkAccessManager manager;
    auto *reply = manager.get(request);
    QVERIFY(reply);
    QCOMPARE(reply->parent(), &manager);
    QVERIFY(!reply->isFinished());
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    QSignalSpy failed(reply, qOverload<NetworkError>(&QCNetworkReply::error));
    QVERIFY(finished.wait());
    QVERIFY(reply->isFinished());
    QCOMPARE(finished.size(), 1);
    QCOMPARE(failed.size(), 1);
    QCOMPARE(reply->errorString(), diagnostic);
    reply->deleteLater();
}

QTEST_MAIN(tst_CurlGlobalInitialization)
#include "tst_CurlGlobalInitialization.moc"
