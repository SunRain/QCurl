#include "QCNetworkAccessManager.h"
#include "QCNetworkCachePolicy.h"
#include "QCNetworkMemoryCache.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "qcurl_http_script_server.h"
#include "tst_QCNetworkNativeDiagnostics.h"

#include <QPointer>
#include <QSignalSpy>
#include <QtTest>

#include <curl/curl.h>

using namespace QCurl;

void tst_QCNetworkNativeDiagnostics::cancelResult_data()
{
    QTest::addColumn<int>("mode");
    QTest::newRow("before-dispatch") << 0;
    QTest::newRow("running") << 1;
    QTest::newRow("scheduled-pending") << 2;
    QTest::newRow("progress-callback") << 3;
}

void tst_QCNetworkNativeDiagnostics::cancelResult()
{
    QFETCH(int, mode);
    auto response    = HttpScriptServer::response(200, QByteArray(65536, 'x'));
    response.delayMs = 150;
    HttpScriptServer server({response});
    QVERIFY(server.start());
    QCNetworkAccessManager manager;
    auto policy = QCNetworkSchedulerPolicy::defaultPolicy();
    policy.setMaxConcurrentRequests(1);
    QVERIFY(manager.setSchedulerPolicy(policy));
    manager.enableRequestScheduler(mode == 2);
    if (mode == 2) {
        manager.get(QCNetworkRequest(server.url()));
        QTRY_COMPARE(server.requests().size(), 1);
    }
    auto *reply = manager.get(QCNetworkRequest(server.url()));
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    if (mode == 1) {
        QTRY_COMPARE(server.requests().size(), 1);
    }
    if (mode == 3) {
        connect(reply, &QCNetworkReply::downloadProgress, this, [reply](qint64 bytes, qint64) {
            if (bytes > 0) {
                reply->cancel();
            }
        });
        QVERIFY(finished.wait(5000));
    } else {
        if (mode == 2) {
            QCoreApplication::processEvents();
            QCOMPARE(server.requests().size(), 1);
        }
        reply->cancel();
    }
    QCOMPARE(reply->state(), ReplyState::Cancelled);
    QCOMPARE(reply->error(), NetworkError::OperationCancelled);
    QCOMPARE(reply->httpStatusCode(), mode == 3 ? 200 : 0);
    QCOMPARE(reply->diagnosticCurlCode(), 0);
    reply->cancel();
    reply->execute();
    // 等待已安排的服务端响应和退订回调，确保迟到事件不能改变终态。
    QTest::qWait(250);
    QCOMPARE(reply->state(), ReplyState::Cancelled);
    QCOMPARE(reply->diagnosticCurlCode(), 0);
    QCOMPARE(finished.size(), 1);
}

void tst_QCNetworkNativeDiagnostics::cacheFallback_data()
{
    QTest::addColumn<bool>("hit");
    QTest::addColumn<bool>("scheduled");
    QTest::newRow("direct-hit") << true << false;
    QTest::newRow("direct-miss") << false << false;
    QTest::newRow("scheduled-hit") << true << true;
    QTest::newRow("scheduled-miss") << false << true;
}

void tst_QCNetworkNativeDiagnostics::cacheFallback()
{
    QFETCH(bool, hit);
    QFETCH(bool, scheduled);
    QList<HttpScriptServer::Response> responses;
    if (hit) {
        responses.append(
            HttpScriptServer::response(200, "cached", "Cache-Control: max-age=300\r\n"));
    }
    responses.append(HttpScriptServer::Response{});
    HttpScriptServer server(responses);
    QVERIFY(server.start());
    QCNetworkMemoryCache cache;
    QCNetworkAccessManager manager;
    manager.setCache(&cache);
    manager.enableRequestScheduler(scheduled);
    QCNetworkRequest request(server.url());
    if (hit) {
        auto *seed = manager.get(request);
        QSignalSpy seeded(seed, &QCNetworkReply::finished);
        QVERIFY(seeded.wait(5000));
        QCOMPARE(seed->error(), NetworkError::NoError);
    }
    request.setCachePolicy(QCNetworkCachePolicy::PreferNetwork);
    auto *reply = manager.get(request);
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    QVERIFY(finished.wait(5000));
    QCOMPARE(reply->state(), hit ? ReplyState::Finished : ReplyState::Error);
    QCOMPARE(reply->error(), hit ? NetworkError::NoError : fromCurlCode(CURLE_GOT_NOTHING));
    QCOMPARE(reply->httpStatusCode(), hit ? 200 : 0);
    QCOMPARE(reply->diagnosticCurlCode(), hit ? 0 : int(CURLE_GOT_NOTHING));
    QCOMPARE(reply->readAll().value(), hit ? QByteArray("cached") : QByteArray());
    QCOMPARE(server.requests().size(), hit ? 2 : 1);
    QCOMPARE(finished.size(), 1);
}

void tst_QCNetworkNativeDiagnostics::copyDiagnosticBeforeDestruction_data()
{
    QTest::addColumn<bool>("deleteParent");
    QTest::newRow("reply") << false;
    QTest::newRow("parent") << true;
}

void tst_QCNetworkNativeDiagnostics::copyDiagnosticBeforeDestruction()
{
    QFETCH(bool, deleteParent);
    HttpScriptServer server({{}});
    QVERIFY(server.start());
    QPointer<QCNetworkAccessManager> manager = new QCNetworkAccessManager(this);
    QPointer<QCNetworkReply> reply           = manager->get(QCNetworkRequest(server.url()));
    int copiedCode                           = -1;
    int notifications                        = 0;
    connect(reply, &QCNetworkReply::finished, this, [&]() {
        ++notifications;
        copiedCode = reply->diagnosticCurlCode();
        if (deleteParent) {
            delete manager.data();
        } else {
            delete reply.data();
        }
    });
    QTRY_VERIFY_WITH_TIMEOUT(reply.isNull(), 5000);
    QCOMPARE(copiedCode, int(CURLE_GOT_NOTHING));
    QTest::qWait(20);
    QCOMPARE(copiedCode, int(CURLE_GOT_NOTHING));
    QCOMPARE(notifications, 1);
    delete manager.data();
}
