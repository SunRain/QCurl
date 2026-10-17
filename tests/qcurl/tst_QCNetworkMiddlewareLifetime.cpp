#include "QCNetworkAccessManager.h"
#include "QCNetworkMiddleware.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "middleware_lifetime_test_support.h"
#include "qcnetwork_mock_test_support.h"

#include <QSignalSpy>
#include <QtTest>

#include <functional>
#include <memory>
#include <optional>
#include <utility>

using namespace QCurl;

using namespace MiddlewareLifetimeTestSupport;

class tst_QCNetworkMiddlewareLifetime : public QObject
{
    Q_OBJECT

public:
    tst_QCNetworkMiddlewareLifetime() = default;

private:
    Q_DISABLE_COPY_MOVE(tst_QCNetworkMiddlewareLifetime)

private Q_SLOTS:
    void unregisterDuringCallback_data();
    void unregisterDuringCallback();
    void registrationReplacement_data();
    void registrationReplacement();
    void addedMiddlewareOnlyJoinsNewRequests();
    void managerRegistrationsAreIndependent();
    void requestMutationSurvivesUnregister();
};

void tst_QCNetworkMiddlewareLifetime::unregisterDuringCallback_data()
{
    QTest::addColumn<int>("phase");
    for (int phase = 0; phase != 3; ++phase) {
        QTest::newRow(qPrintable(QString::number(phase))) << phase;
    }
}

void tst_QCNetworkMiddlewareLifetime::unregisterDuringCallback()
{
    QFETCH(int, phase);
    QCNetworkAccessManager manager;
    QCNetworkMockHandler mock;
    TestSupport::setMockHandler(manager, &mock);
    const QUrl url("http://example.com/middleware-lifetime");
    mock.mockResponse(HttpMethod::Get, url, "ok", 200);
    CallbackMiddleware first;
    CallbackMiddleware second;
    first.callback = [&](int current) {
        if (current == phase) {
            manager.removeMiddleware(&second);
        }
    };
    manager.addMiddleware(&first);
    manager.addMiddleware(&second);
    auto *reply = manager.get(QCNetworkRequest(url));
    QVERIFY(reply);
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(reply->error(), NetworkError::NoError);
    QList<int> expected;
    for (int previous = 0; previous < phase; ++previous) {
        expected.append(previous);
    }
    QCOMPARE(second.calls, expected);
    QCOMPARE(first.calls, QList<int>({0, 1, 2}));
}

void tst_QCNetworkMiddlewareLifetime::registrationReplacement_data()
{
    QTest::addColumn<int>("phase");
    QTest::addColumn<QString>("action");
    for (int phase = 0; phase != 3; ++phase) {
        for (const auto &action : {"release", "destructor", "clear", "reregister", "replace"}) {
            QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(phase).arg(action)))
                << phase << QString::fromLatin1(action);
        }
    }
}

void tst_QCNetworkMiddlewareLifetime::registrationReplacement()
{
    QFETCH(int, phase);
    QFETCH(QString, action);
    QCNetworkAccessManager manager;
    QCNetworkMockHandler mock;
    TestSupport::setMockHandler(manager, &mock);
    const QUrl url("http://example.com/middleware-replacement");
    mock.mockResponse(HttpMethod::Get, url, "ok", 200);
    CallbackMiddleware first;
    std::optional<CallbackMiddleware> second(std::in_place);
    QList<int> calls;
    const auto record = [&](int p) { calls.append(p); };
    second->callback  = record;
    bool changed      = false;
    first.callback    = [&](int current) {
        if (current != phase || std::exchange(changed, true)) {
            return;
        }
        auto *address = &*second;
        if (action == "clear") {
            manager.clearMiddlewares();
        } else if (action != "destructor") {
            manager.removeMiddleware(address);
        }
        if (action == "release" || action == "destructor" || action == "replace") {
            second.reset();
        }
        if (action == "replace") {
            second.emplace();
            QCOMPARE(&*second, address);
            second->callback = record;
        }
        if (action == "reregister" || action == "replace") {
            manager.addMiddleware(&*second);
        }
    };
    manager.addMiddleware(&first);
    manager.addMiddleware(&*second);
    auto *reply = manager.get(QCNetworkRequest(url));
    QVERIFY(reply);
    QTRY_VERIFY(reply->isFinished());
    QList<int> expected;
    for (int p = 0; p < phase; ++p) {
        expected.append(p);
    }
    QCOMPARE(calls, expected);
    if (action == "reregister" || action == "replace") {
        calls.clear();
        auto *next = manager.get(QCNetworkRequest(url));
        QTRY_VERIFY(next->isFinished());
        QCOMPARE(calls, QList<int>({0, 1, 2}));
    }
}

void tst_QCNetworkMiddlewareLifetime::addedMiddlewareOnlyJoinsNewRequests()
{
    QCNetworkAccessManager manager;
    QCNetworkMockHandler mock;
    TestSupport::setMockHandler(manager, &mock);
    const QUrl url("http://example.com/middleware-nested");
    mock.mockResponse(HttpMethod::Get, url, "ok", 200);
    CallbackMiddleware first;
    CallbackMiddleware second;
    CallbackMiddleware added;
    QCNetworkReply *nested = nullptr;
    bool entered           = false;
    QStringList order;
    first.callback = [&](int phase) {
        order.append(QStringLiteral("A%1").arg(phase));
        if (phase == 0 && !std::exchange(entered, true)) {
            manager.addMiddleware(&added);
            nested = manager.get(QCNetworkRequest(url));
        }
    };
    second.callback = [&](int phase) { order.append(QStringLiteral("B%1").arg(phase)); };
    added.callback  = [&](int phase) { order.append(QStringLiteral("C%1").arg(phase)); };
    manager.addMiddleware(&first);
    manager.addMiddleware(&second);
    auto *outer = manager.get(QCNetworkRequest(url));
    QVERIFY(outer && nested);
    QCOMPARE(order, QStringList({"A0", "A0", "B0", "C0", "A1", "B1", "C1", "B0", "A1", "B1"}));
    QTRY_VERIFY(outer->isFinished() && nested->isFinished());
    QCOMPARE(added.calls, QList<int>({0, 1, 2}));
    QCOMPARE(first.calls, QList<int>({0, 0, 1, 1, 2, 2}));
    auto *next = manager.get(QCNetworkRequest(url));
    QTRY_VERIFY(next->isFinished());
    QCOMPARE(added.calls, QList<int>({0, 1, 2, 0, 1, 2}));
}

void tst_QCNetworkMiddlewareLifetime::managerRegistrationsAreIndependent()
{
    QCNetworkAccessManager first;
    QCNetworkAccessManager second;
    QCNetworkMockHandler mock;
    TestSupport::setMockHandler(first, &mock);
    TestSupport::setMockHandler(second, &mock);
    const QUrl url("http://example.com/middleware-managers");
    mock.mockResponse(HttpMethod::Get, url, "ok", 200);
    CallbackMiddleware middleware;
    first.addMiddleware(&middleware);
    second.addMiddleware(&middleware);
    second.addMiddleware(&middleware);
    first.removeMiddleware(&middleware);
    auto *reply = second.get(QCNetworkRequest(url));
    QTRY_VERIFY(reply->isFinished());
    QCOMPARE(middleware.calls, QList<int>({0, 1, 2}));
    QCOMPARE(first.middlewares().size(), 0);
    QCOMPARE(second.middlewares().size(), 1);
}

void tst_QCNetworkMiddlewareLifetime::requestMutationSurvivesUnregister()
{
    QCNetworkAccessManager manager;
    QCNetworkMockHandler mock;
    TestSupport::setMockHandler(manager, &mock);
    const QUrl url("http://example.com/middleware-mutation");
    mock.mockResponse(HttpMethod::Get, url, "ok", 200);
    mock.setCaptureEnabled(true);
    CallbackMiddleware middleware;
    middleware.callback = [&](int phase) {
        if (phase == 0) {
            manager.removeMiddleware(&middleware);
        }
    };
    manager.addMiddleware(&middleware);
    auto *reply = manager.get(QCNetworkRequest(url));
    QTRY_VERIFY(reply->isFinished());
    QCOMPARE(middleware.calls, QList<int>({0}));
    const auto requests = mock.takeCapturedRequests();
    QCOMPARE(requests.size(), 1);
    QVERIFY(requests[0].headers().contains({QByteArray("X-Lifetime"), QByteArray("retained")}));
}

QTEST_GUILESS_MAIN(tst_QCNetworkMiddlewareLifetime)
#include "tst_QCNetworkMiddlewareLifetime.moc"
