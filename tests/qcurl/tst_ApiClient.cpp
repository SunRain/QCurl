#include "ApiClient.h"
#include "qcurl_http_script_server.h"

#include <QPointer>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QtTest>

namespace {

QByteArray headerValue(const QByteArray &wire, const QByteArray &name)
{
    for (const auto &line : wire.left(wire.indexOf("\r\n\r\n")).split('\n')) {
        const auto colon = line.indexOf(':');
        if (colon > 0 && line.left(colon).compare(name, Qt::CaseInsensitive) == 0) {
            return line.mid(colon + 1).trimmed();
        }
    }
    return {};
}

void invokeMethod(ApiClient &client,
                  QCurl::HttpMethod method,
                  ApiClient::SuccessCallback success,
                  ApiClient::ErrorCallback error)
{
    const QString endpoint = QStringLiteral("items/7");
    const QJsonObject body{{QStringLiteral("name"), QStringLiteral("updated")}};
    switch (method) {
        case QCurl::HttpMethod::Get:
            client.get(endpoint,
                       success,
                       error,
                       {{QStringLiteral("q"), QStringLiteral("two words")}});
            return;
        case QCurl::HttpMethod::Post:
            client.post(endpoint, body, success, error);
            return;
        case QCurl::HttpMethod::Put:
            client.put(endpoint, body, success, error);
            return;
        case QCurl::HttpMethod::Delete:
            client.del(endpoint, success, error);
            return;
        default:
            QFAIL("测试数据包含未支持的方法");
    }
}

} // namespace

class TestApiClient : public QObject
{
    Q_OBJECT

private:
    Q_DISABLE_COPY_MOVE(TestApiClient)

public:
    TestApiClient() = default;

private Q_SLOTS:
    void wireMethods_data();
    void wireMethods();
    void unsupportedMethods_data();
    void unsupportedMethods();
    void jsonResponses_data();
    void jsonResponses();
    void invalidUrlFailsOnce();
    void timeoutFailsOnce();
    void cancelAllIsReentrant();
    void cancelCallbackCanDestroyClient();
    void destructionIsSilent();
    void callbackCanDestroyClient_data();
    void callbackCanDestroyClient();
};

void TestApiClient::wireMethods_data()
{
    QTest::addColumn<QCurl::HttpMethod>("method");
    QTest::addColumn<QByteArray>("expectedMethod");
    QTest::addColumn<QByteArray>("expectedPath");
    QTest::addColumn<QByteArray>("expectedBody");
    QTest::newRow("GET") << QCurl::HttpMethod::Get << QByteArray("GET")
                         << QByteArray("/items/7?q=two%20words") << QByteArray();
    QTest::newRow("POST") << QCurl::HttpMethod::Post << QByteArray("POST") << QByteArray("/items/7")
                          << QByteArray("{\"name\":\"updated\"}");
    QTest::newRow("PUT") << QCurl::HttpMethod::Put << QByteArray("PUT") << QByteArray("/items/7")
                         << QByteArray("{\"name\":\"updated\"}");
    QTest::newRow("DELETE") << QCurl::HttpMethod::Delete << QByteArray("DELETE")
                            << QByteArray("/items/7") << QByteArray();
}

void TestApiClient::wireMethods()
{
    QFETCH(QCurl::HttpMethod, method);
    QFETCH(QByteArray, expectedMethod);
    QFETCH(QByteArray, expectedPath);
    QFETCH(QByteArray, expectedBody);
    HttpScriptServer server({HttpScriptServer::response(200, "{\"ok\":true}")});
    QVERIFY(server.start());
    ApiClient client(server.url().adjusted(QUrl::RemovePath).toString());
    client.setDefaultHeader(QStringLiteral("X-Custom"), QStringLiteral("kept"));
    client.setBearerToken(QStringLiteral("test-marker"));
    QSignalSpy started(&client, &ApiClient::requestStarted);
    QSignalSpy completed(&client, &ApiClient::requestCompleted);
    int successCount = 0;
    int errorCount   = 0;
    invokeMethod(
        client,
        method,
        [&successCount](const QJsonDocument &response) {
            QCOMPARE(response.object().value(QStringLiteral("ok")).toBool(), true);
            ++successCount;
        },
        [&errorCount](int, const QString &) { ++errorCount; });
    QTRY_COMPARE(completed.count(), 1);
    QCOMPARE(started.count(), 1);
    QCOMPARE(successCount, 1);
    QCOMPARE(errorCount, 0);
    QVERIFY(client.m_activeRequests.isEmpty());
    QCOMPARE(server.requests().size(), 1);
    const auto wire = server.requests().first();
    QCOMPARE(wire.split(' ').at(0), expectedMethod);
    QCOMPARE(wire.split(' ').at(1), expectedPath);
    QCOMPARE(wire.mid(wire.indexOf("\r\n\r\n") + 4), expectedBody);
    QCOMPARE(headerValue(wire, "Content-Type"), QByteArray("application/json"));
    QCOMPARE(headerValue(wire, "X-Custom"), QByteArray("kept"));
    QCOMPARE(headerValue(wire, "Authorization"), QByteArray("Bearer test-marker"));
    const QByteArray expectedOverride = method == QCurl::HttpMethod::Put
                                                || method == QCurl::HttpMethod::Delete
                                            ? expectedMethod
                                            : QByteArray();
    QCOMPARE(headerValue(wire, "X-HTTP-Method-Override"), expectedOverride);
    QCOMPARE(completed.first().at(0).toString(), QStringLiteral("items/7"));
    QCOMPARE(completed.first().at(1).toBool(), true);
}

void TestApiClient::unsupportedMethods_data()
{
    QTest::addColumn<QCurl::HttpMethod>("method");
    QTest::newRow("HEAD") << QCurl::HttpMethod::Head;
    QTest::newRow("PATCH") << QCurl::HttpMethod::Patch;
    QTest::newRow("Custom") << QCurl::HttpMethod::Custom;
    QTest::newRow("invalid") << static_cast<QCurl::HttpMethod>(-1);
}

void TestApiClient::unsupportedMethods()
{
    QFETCH(QCurl::HttpMethod, method);
    HttpScriptServer server({HttpScriptServer::response(200, "{}")});
    QVERIFY(server.start());
    ApiClient client(server.url().toString());
    QCurl::QCNetworkRequest request(server.url());
    ApiClient::RequestContext context;
    context.endpoint = QStringLiteral("unsupported");
    int errors       = 0;
    context.onError  = [&errors](int code, const QString &message) {
        QCOMPARE(code, static_cast<int>(QCurl::NetworkError::InvalidRequest));
        QCOMPARE(message, QStringLiteral("Unsupported HTTP method"));
        ++errors;
    };
    QSignalSpy completed(&client, &ApiClient::requestCompleted);
    client.sendRequest(request, method, context);
    QCOMPARE(errors, 1);
    QCOMPARE(completed.count(), 1);
    QCOMPARE(completed.first().at(1).toBool(), false);
    QVERIFY(client.m_activeRequests.isEmpty());
    QCoreApplication::processEvents();
    QCOMPARE(server.connectionCount(), 0);
    QVERIFY(server.requests().isEmpty());
}

void TestApiClient::jsonResponses_data()
{
    QTest::addColumn<int>("status");
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<bool>("success");
    QTest::addColumn<int>("expectedError");
    QTest::newRow("valid") << 200 << QByteArray("{\"ok\":true}") << true << 0;
    QTest::newRow("invalid-json") << 200 << QByteArray("not-json") << false << 0;
    QTest::newRow("http-error-json") << 404 << QByteArray("{\"error\":\"missing\"}") << false
                                     << static_cast<int>(QCurl::NetworkError::HttpNotFound);
    QTest::newRow("empty-json") << 204 << QByteArray() << false << 0;
}

void TestApiClient::jsonResponses()
{
    QFETCH(int, status);
    QFETCH(QByteArray, body);
    QFETCH(bool, success);
    QFETCH(int, expectedError);
    HttpScriptServer server({HttpScriptServer::response(status, body)});
    QVERIFY(server.start());
    ApiClient client(server.url().adjusted(QUrl::RemovePath).toString());
    QSignalSpy completed(&client, &ApiClient::requestCompleted);
    int successes = 0;
    QList<int> errors;
    QString errorMessage;
    client.get(
        QStringLiteral("result"),
        [&](const QJsonDocument &) { ++successes; },
        [&](int code, const QString &message) {
            errors.append(code);
            errorMessage = message;
        });
    QPointer<QCurl::QCNetworkReply> reply = client.m_activeRequests.first();
    QTRY_VERIFY(reply.isNull());
    QCoreApplication::processEvents();
    QCOMPARE(completed.count(), 1);
    QCOMPARE(completed.first().at(1).toBool(), success);
    QCOMPARE(successes, success ? 1 : 0);
    QCOMPARE(errors.size(), success ? 0 : 1);
    QVERIFY(client.m_activeRequests.isEmpty());
    if (!success) {
        QCOMPARE(errors.first(), expectedError);
        QCOMPARE(errorMessage.startsWith(QStringLiteral("JSON parse error:")), status < 400);
    }
}

void TestApiClient::invalidUrlFailsOnce()
{
    ApiClient client(QStringLiteral("ftp://localhost"));
    QSignalSpy completed(&client, &ApiClient::requestCompleted);
    int successes = 0;
    QList<int> errors;
    client.get(
        QStringLiteral("unsupported"),
        [&](const QJsonDocument &) { ++successes; },
        [&](int code, const QString &) { errors.append(code); });
    QTRY_VERIFY(!completed.isEmpty());
    QCoreApplication::processEvents();
    QCOMPARE(completed.count(), 1);
    QCOMPARE(completed.first().at(1).toBool(), false);
    QCOMPARE(successes, 0);
    QCOMPARE(errors, QList<int>{static_cast<int>(QCurl::NetworkError::InvalidRequest)});
    QVERIFY(client.m_activeRequests.isEmpty());
}

void TestApiClient::timeoutFailsOnce()
{
    auto response    = HttpScriptServer::response(200, "{}");
    response.delayMs = 3000;
    HttpScriptServer server({response});
    QVERIFY(server.start());
    ApiClient client(server.url().adjusted(QUrl::RemovePath).toString());
    client.setTimeout(1);
    QSignalSpy completed(&client, &ApiClient::requestCompleted);
    int successes = 0;
    QList<int> errors;
    client.get(
        QStringLiteral("timeout"),
        [&](const QJsonDocument &) { ++successes; },
        [&](int code, const QString &) { errors.append(code); });
    QTRY_VERIFY(!completed.isEmpty());
    QCoreApplication::processEvents();
    QCOMPARE(completed.count(), 1);
    QCOMPARE(completed.first().at(1).toBool(), false);
    QCOMPARE(successes, 0);
    QCOMPARE(errors, QList<int>{static_cast<int>(QCurl::NetworkError::ConnectionTimeout)});
    QVERIFY(client.m_activeRequests.isEmpty());
}

void TestApiClient::cancelAllIsReentrant()
{
    auto delayed    = HttpScriptServer::response(200, "{}");
    delayed.delayMs = 10000;
    HttpScriptServer server({delayed, delayed, HttpScriptServer::response(200, "{}")});
    QVERIFY(server.start());
    ApiClient client(server.url().adjusted(QUrl::RemovePath).toString());
    QSignalSpy completed(&client, &ApiClient::requestCompleted);
    int successes = 0;
    QList<int> errors;
    const auto error = [&](int code, const QString &) {
        errors.append(code);
        if (errors.size() == 1) {
            client.cancelAll();
            client.get(
                QStringLiteral("after-cancel"),
                [&](const QJsonDocument &) { ++successes; },
                [](int, const QString &) { QFAIL("取消回调中新建的请求不应被外层取消"); });
        }
    };
    client.get(QStringLiteral("first"), [](const QJsonDocument &) { QFAIL("请求应取消"); }, error);
    client.get(QStringLiteral("second"), [](const QJsonDocument &) { QFAIL("请求应取消"); }, error);
    QTRY_COMPARE(server.requests().size(), 2);
    client.cancelAll();
    const int cancelled = static_cast<int>(QCurl::NetworkError::OperationCancelled);
    QCOMPARE(errors, (QList<int>{cancelled, cancelled}));
    QTRY_COMPARE(successes, 1);
    QCoreApplication::processEvents();
    QCOMPARE(completed.count(), 3);
    QCOMPARE(completed.at(0).at(1).toBool(), false);
    QCOMPARE(completed.at(1).at(1).toBool(), false);
    QCOMPARE(completed.at(2).at(0).toString(), QStringLiteral("after-cancel"));
    QCOMPARE(completed.at(2).at(1).toBool(), true);
    QVERIFY(client.m_activeRequests.isEmpty());
}

void TestApiClient::destructionIsSilent()
{
    auto response    = HttpScriptServer::response(200, "{}");
    response.delayMs = 10000;
    HttpScriptServer server({response});
    QVERIFY(server.start());
    QPointer<ApiClient> client(new ApiClient(server.url().adjusted(QUrl::RemovePath).toString()));
    int callbacks = 0;
    QSignalSpy completed(client, &ApiClient::requestCompleted);
    client->get(
        QStringLiteral("pending"),
        [&](const QJsonDocument &) { ++callbacks; },
        [&](int, const QString &) { ++callbacks; });
    QPointer<QCurl::QCNetworkReply> reply = client->m_activeRequests.first();
    client->deleteLater();
    QTRY_VERIFY(client.isNull());
    QVERIFY(reply.isNull());
    QCoreApplication::processEvents();
    QCOMPARE(callbacks, 0);
    QCOMPARE(completed.count(), 0);
}

void TestApiClient::cancelCallbackCanDestroyClient()
{
    HttpScriptServer server({HttpScriptServer::response(200, "{}")});
    QVERIFY(server.start());
    QPointer<QObject> owner(new QObject);
    const auto cleanup = qScopeGuard([&]() {
        if (owner) {
            owner->deleteLater();
        }
    });
    QPointer<ApiClient> client(
        new ApiClient(server.url().adjusted(QUrl::RemovePath).toString(), owner));
    int errors       = 0;
    const auto error = [&](int code, const QString &) {
        QCOMPARE(code, static_cast<int>(QCurl::NetworkError::OperationCancelled));
        ++errors;
        delete owner.data();
    };
    client->get(QStringLiteral("first"), {}, error);
    client->get(QStringLiteral("second"), {}, error);
    client->cancelAll();
    QVERIFY(client.isNull());
    QCoreApplication::processEvents();
    QCOMPARE(errors, 1);
}

void TestApiClient::callbackCanDestroyClient_data()
{
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<bool>("success");
    QTest::newRow("success") << QByteArray("{}") << true;
    QTest::newRow("error") << QByteArray("not-json") << false;
}

void TestApiClient::callbackCanDestroyClient()
{
    QFETCH(QByteArray, body);
    QFETCH(bool, success);
    HttpScriptServer server({HttpScriptServer::response(200, body)});
    QVERIFY(server.start());
    QPointer<QObject> owner(new QObject);
    const auto cleanup = qScopeGuard([&]() {
        if (owner) {
            owner->deleteLater();
        }
    });
    QPointer<ApiClient> client(
        new ApiClient(server.url().adjusted(QUrl::RemovePath).toString(), owner));
    QSignalSpy completed(client, &ApiClient::requestCompleted);
    int successes = 0;
    int errors    = 0;
    // 故意从用户回调销毁 owner，验证返回后不再访问 client 或 reply。
    client->get(
        QStringLiteral("delete-owner"),
        [&](const QJsonDocument &) {
            ++successes;
            delete owner.data();
        },
        [&](int, const QString &) {
            ++errors;
            delete owner.data();
        });
    QTRY_VERIFY(client.isNull());
    QCoreApplication::processEvents();
    QCOMPARE(successes, success ? 1 : 0);
    QCOMPARE(errors, success ? 0 : 1);
    QCOMPARE(completed.count(), 0);
}

QTEST_GUILESS_MAIN(TestApiClient)
#include "tst_ApiClient.moc"
