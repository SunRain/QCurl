#include "CurlFeatureProbe.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkMemoryCache.h"
#include "QCNetworkMiddleware.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "qcnetwork_mock_test_support.h"
#include "qcurl_http_script_server.h"

#include <QPointer>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QtTest>

#include <memory>

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
    void readyReadCancellationPreservesTerminal_data();
    void readyReadCancellationPreservesTerminal();
    void cacheServiceIsBorrowedAtEachCheckpoint_data();
    void cacheServiceIsBorrowedAtEachCheckpoint();
    void rejectsPreparedPolicy_data();
    void rejectsPreparedPolicy();
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
    QCOMPARE(reply->diagnosticCurlCode(), 0);
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    QSignalSpy failed(reply, qOverload<NetworkError>(&QCNetworkReply::error));
    QVERIFY(finished.wait());
    QCOMPARE(finished.size(), 1);
    QCOMPARE(failed.size(), mode >= 3 ? 1 : 0);
    QCOMPARE(reply->error() == NetworkError::NoError, mode < 3);
    QCOMPARE(reply->diagnosticCurlCode(), 0);
    reply->cancel();
    reply->execute();
    QCoreApplication::processEvents();
    QCOMPARE(finished.size(), 1);
    QCOMPARE(reply->diagnosticCurlCode(), 0);
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

void tst_QCNetworkCompletionContract::readyReadCancellationPreservesTerminal_data()
{
    QTest::addColumn<bool>("revalidate");
    QTest::newRow("cache-hit") << false;
    QTest::newRow("cache-304") << true;
}

void tst_QCNetworkCompletionContract::readyReadCancellationPreservesTerminal()
{
    QFETCH(bool, revalidate);
    const QByteArray firstHeaders = revalidate
                                        ? QByteArrayLiteral(
                                              "Cache-Control: max-age=0\r\nETag: \"entity\"\r\n")
                                        : QByteArrayLiteral("Cache-Control: max-age=300\r\n");
    HttpScriptServer server({HttpScriptServer::response(200, "cached", firstHeaders),
                             HttpScriptServer::response(304, "", "Cache-Control: max-age=300\r\n")});
    QVERIFY(server.start());
    QCNetworkMemoryCache cache;
    QCNetworkAccessManager manager;
    manager.setCache(&cache);
    QCNetworkRequest request(server.url());
    auto *first = manager.get(request);
    QSignalSpy firstDone(first, &QCNetworkReply::finished);
    QVERIFY(firstDone.wait());
    QCOMPARE(first->error(), NetworkError::NoError);
    delete first;

    auto *reply = manager.get(request);
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    QSignalSpy cancelled(reply, &QCNetworkReply::cancelled);
    QSignalSpy states(reply, &QCNetworkReply::stateChanged);
    connect(reply, &QCNetworkReply::readyRead, reply, &QCNetworkReply::cancel);
    QVERIFY(finished.wait());
    QCoreApplication::processEvents();
    QCOMPARE(reply->state(), ReplyState::Cancelled);
    QCOMPARE(reply->error(), NetworkError::OperationCancelled);
    QCOMPARE(reply->diagnosticCurlCode(), 0);
    QCOMPARE(cancelled.size(), 1);
    QCOMPARE(finished.size(), 1);
    QVERIFY(!states.isEmpty());
    QCOMPARE(qvariant_cast<ReplyState>(states.last().at(0)), ReplyState::Cancelled);
}

void tst_QCNetworkCompletionContract::cacheServiceIsBorrowedAtEachCheckpoint_data()
{
    QTest::addColumn<bool>("atStore");
    QTest::addColumn<int>("change");
    QTest::newRow("execute-replace") << false << 0;
    QTest::newRow("execute-unregister") << false << 1;
    QTest::newRow("execute-destroy") << false << 2;
    QTest::newRow("store-replace") << true << 0;
    QTest::newRow("store-unregister") << true << 1;
    QTest::newRow("store-destroy") << true << 2;
}

void tst_QCNetworkCompletionContract::cacheServiceIsBorrowedAtEachCheckpoint()
{
    QFETCH(bool, atStore);
    QFETCH(int, change);
    HttpScriptServer server(
        {HttpScriptServer::response(200, "old", "Cache-Control: max-age=300\r\n"),
         HttpScriptServer::response(200, "new", "Cache-Control: max-age=300\r\n")});
    QVERIFY(server.start());
    auto original = std::make_unique<QCNetworkMemoryCache>();
    QCNetworkMemoryCache replacement;
    QCNetworkAccessManager manager;
    manager.setCache(original.get());
    const QCNetworkRequest request(server.url());
    const QCNetworkCacheRequestKey key(HttpMethod::Get, server.url());
    if (!atStore) {
        auto *first = manager.get(request);
        QSignalSpy done(first, &QCNetworkReply::finished);
        QVERIFY(done.wait());
        QCOMPARE(first->error(), NetworkError::NoError);
        delete first;
        const auto cached = original->lookup(key, QCNetworkCacheReadMode::FreshOnly);
        QVERIFY(cached.hit());
        replacement.insert(key, "replacement", cached.metadata());
    }
    const auto changeService = [&]() {
        if (change == 0) {
            manager.setCache(&replacement);
        } else if (change == 1) {
            manager.setCache(nullptr);
        } else {
            original.reset();
        }
    };
    auto *reply = manager.get(request);
    if (atStore) {
        connect(reply, &QCNetworkReply::readyRead, reply, changeService);
    } else {
        changeService();
    }
    QSignalSpy done(reply, &QCNetworkReply::finished);
    QVERIFY(done.wait());
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->readAll().value(),
             QByteArray(atStore       ? "old"
                        : change == 0 ? "replacement"
                                      : "new"));
    QCOMPARE(manager.cache(), change == 0 ? &replacement : nullptr);
    QCOMPARE(server.requests().size(), atStore || change == 0 ? 1 : 2);
    if (atStore) {
        const auto cached = replacement.lookup(key, QCNetworkCacheReadMode::FreshOnly);
        QCOMPARE(cached.hit(), change == 0);
        if (cached.hit()) {
            QCOMPARE(cached.body(), QByteArrayLiteral("old"));
        }
        if (original) {
            QVERIFY(!original->lookup(key, QCNetworkCacheReadMode::AllowStale).hit());
        }
    }
}

void tst_QCNetworkCompletionContract::rejectsPreparedPolicy_data()
{
    QTest::addColumn<bool>("cached");
    QTest::addColumn<bool>("scheduled");
    QTest::addColumn<bool>("invalidProxy");
    for (bool cached : {false, true}) {
        for (bool scheduled : {false, true}) {
            for (bool invalidProxy : {false, true}) {
                const auto row = QByteArray(cached ? "cache" : "mock")
                                 + (scheduled ? "-scheduled" : "-direct")
                                 + (invalidProxy ? "-proxy" : "-timeout");
                QTest::newRow(row) << cached << scheduled << invalidProxy;
            }
        }
    }
}

namespace {
class InvalidPolicyMiddleware final : public QCNetworkMiddleware
{
public:
    explicit InvalidPolicyMiddleware(bool invalidProxy)
        : m_invalidProxy(invalidProxy)
    {}
    void onRequestPreSend(QCNetworkRequest &request) override
    {
        request.setTimeout(std::chrono::milliseconds(-1));
        if (m_invalidProxy) {
            QCNetworkProxyConfig proxy;
            proxy.setType(QCNetworkProxyConfig::ProxyType::Http);
            request.setProxyConfig(proxy);
        }
    }

private:
    bool m_invalidProxy;
};
} // namespace

void tst_QCNetworkCompletionContract::rejectsPreparedPolicy()
{
    QFETCH(bool, cached);
    QFETCH(bool, scheduled);
    QFETCH(bool, invalidProxy);
    HttpScriptServer server(
        {HttpScriptServer::response(200, "cached", "Cache-Control: max-age=300\r\n")});
    QVERIFY(server.start());
    QCNetworkMemoryCache cache;
    QCNetworkMockHandler mock;
    QCNetworkAccessManager manager;
    const QCNetworkRequest request(server.url());
    if (cached) {
        manager.setCache(&cache);
        auto *first = manager.get(request);
        QSignalSpy done(first, &QCNetworkReply::finished);
        QVERIFY(done.wait());
        QCOMPARE(first->error(), NetworkError::NoError);
        delete first;
    } else {
        mock.mockResponse(HttpMethod::Get, request.url(), "unexpected", 200);
        mock.setCaptureEnabled(true);
        TestSupport::setMockHandler(manager, &mock);
    }
    InvalidPolicyMiddleware middleware(invalidProxy);
    manager.addMiddleware(&middleware);
    manager.enableRequestScheduler(scheduled);
    auto *reply = manager.get(request);
    QVERIFY(!reply->isFinished());
    QSignalSpy done(reply, &QCNetworkReply::finished);
    QSignalSpy failed(reply, qOverload<NetworkError>(&QCNetworkReply::error));
    QVERIFY(done.wait());
    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QVERIFY(reply->errorString().contains(invalidProxy ? QStringLiteral("代理")
                                                       : QStringLiteral("total timeout")));
    QCOMPARE(done.size(), 1);
    QCOMPARE(failed.size(), 1);
    QCOMPARE(server.requests().size(), cached ? 1 : 0);
    QVERIFY(mock.takeCapturedRequests().isEmpty());
    QVERIFY(reply->capabilityWarnings().isEmpty());
}

QTEST_GUILESS_MAIN(tst_QCNetworkCompletionContract)
#include "tst_QCNetworkCompletionContract.moc"
