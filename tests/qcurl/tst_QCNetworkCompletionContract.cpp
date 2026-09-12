#include "CurlFeatureProbe.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkMemoryCache.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "qcnetwork_mock_test_support.h"
#include "qcurl_http_script_server.h"

#include <QPointer>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QtTest>

using namespace QCurl;

class tst_QCNetworkCompletionContract : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(tst_QCNetworkCompletionContract)

public:
    tst_QCNetworkCompletionContract() = default;

private Q_SLOTS:
    void returnedReply_data();
    void returnedReply();
    void managerDestructionBeforeDispatch();
    void directDeletionOnFailure();
};

void tst_QCNetworkCompletionContract::returnedReply_data()
{
    QTest::addColumn<int>("mode");
    QTest::newRow("network") << 0;
    QTest::newRow("mock") << 1;
    QTest::newRow("cache") << 2;
    QTest::newRow("invalid-url") << 3;
    QTest::newRow("invalid-method") << 4;
    QTest::newRow("capability-rejection") << 5;
}

void tst_QCNetworkCompletionContract::returnedReply()
{
    QFETCH(int, mode);
    HttpScriptServer server(
        {HttpScriptServer::response(200, "ok", "Cache-Control: max-age=300\r\n")});
    QVERIFY(server.start());
    QCNetworkMemoryCache cache;
    QCNetworkMockHandler mock;
    QCNetworkAccessManager manager;
    QCNetworkRequest request(server.url());
    if (mode == 1) {
        mock.mockResponse(HttpMethod::Get, server.url(), "ok", 200);
        TestSupport::setMockHandler(manager, &mock);
    }
    if (mode == 2) {
        manager.setCache(&cache);
        auto *first = manager.get(request);
        QSignalSpy firstDone(first, &QCNetworkReply::finished);
        QVERIFY(firstDone.wait());
        QCOMPARE(first->error(), NetworkError::NoError);
        delete first;
    }
    if (mode == 3) {
        request.setUrl(QUrl("file:///invalid"));
    }
    const QByteArray oldVersion = qgetenv("QCURL_TEST_FORCE_RUNTIME_LIBCURL_VERSION_NUM");
    const auto restore          = qScopeGuard([oldVersion]() {
        qputenv("QCURL_TEST_FORCE_RUNTIME_LIBCURL_VERSION_NUM", oldVersion);
        CurlFeatureProbe::instance().refreshForTesting();
    });
    if (mode == 5) {
        qputenv("QCURL_TEST_FORCE_RUNTIME_LIBCURL_VERSION_NUM", "0x075400");
        CurlFeatureProbe::instance().refreshForTesting();
    }
    auto *reply = mode == 4 ? manager.sendCustomRequest(request, "bad\r\nmethod")
                            : manager.get(request);
    QVERIFY(reply);
    QCOMPARE(reply->parent(), &manager);
    QVERIFY(!reply->isFinished());
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    QSignalSpy failed(reply, qOverload<NetworkError>(&QCNetworkReply::error));
    QVERIFY(finished.wait());
    QCOMPARE(finished.size(), 1);
    QCOMPARE(failed.size(), mode >= 3 ? 1 : 0);
    QCOMPARE(reply->error() == NetworkError::NoError, mode < 3);
    reply->cancel();
    reply->execute();
    QCoreApplication::processEvents();
    QCOMPARE(finished.size(), 1);
    if (mode == 2) {
        QCOMPARE(server.requests().size(), 1);
    }
}

void tst_QCNetworkCompletionContract::managerDestructionBeforeDispatch()
{
    auto *manager                  = new QCNetworkAccessManager;
    QPointer<QCNetworkReply> reply = manager->get(QCNetworkRequest(QUrl("file:///invalid")));
    QVERIFY(reply);
    delete manager;
    QVERIFY(reply.isNull());
    QCoreApplication::processEvents();
}

void tst_QCNetworkCompletionContract::directDeletionOnFailure()
{
    auto *manager = new QCNetworkAccessManager;
    QPointer<QCNetworkAccessManager> safeManager(manager);
    QPointer<QCNetworkReply> reply = manager->get(QCNetworkRequest(QUrl("file:///invalid")));
    int errors                     = 0;
    connect(reply, qOverload<NetworkError>(&QCNetworkReply::error), this, [&]() {
        ++errors;
        delete manager;
    });
    QTRY_VERIFY(safeManager.isNull());
    QVERIFY(reply.isNull());
    QCOMPARE(errors, 1);
}

QTEST_GUILESS_MAIN(tst_QCNetworkCompletionContract)
#include "tst_QCNetworkCompletionContract.moc"
