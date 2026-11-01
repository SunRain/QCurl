#include "QCNetworkAccessManager.h"
#include "QCNetworkDefaultLogger.h"
#include "QCNetworkDiagnosticQueryPolicy.h"
#include "QCNetworkMiddleware.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "private/QCNetworkLogRedaction_p.h"
#include "private/QCNetworkMiddlewareInternal_p.h"
#include "qcnetwork_retry_policy_test_helper.h"
#include "qcurl_http_script_server.h"

#include <QLoggingCategory>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QtTest>

using namespace QCurl;

namespace {
QStringList s_messages;

void captureMessage(QtMsgType, const QMessageLogContext &, const QString &message)
{
    s_messages.append(message);
}

QCNetworkRequest loggingRequest(QUrl url, int mode, const QString &query)
{
    if (!query.isEmpty()) {
        url.setQuery(query, QUrl::StrictMode);
    }
    if (mode >= 6) {
        url.setUserName(QStringLiteral("synthetic-user"));
        url.setPassword(QStringLiteral("synthetic-password-913"));
    }
    QCNetworkRequest request(url);
    request.setFollowLocation(false);
    if (mode == 5) {
        QCNetworkHttpAuthConfig auth;
        auth.setUserName(QStringLiteral("synthetic-user"));
        auth.setPassword(QStringLiteral("synthetic-password-913"));
        auth.setMethod(QCNetworkHttpAuthMethod::Basic);
        request.setHttpAuth(auth);
    }
    return request;
}

void configureLogger(QCNetworkAccessManager &manager, int mode)
{
    if (mode != 1 && mode != 2 && mode != 7) {
        return;
    }
    auto logger = QCNetworkLoggerHandle::create<QCNetworkDefaultLogger>();
    static_cast<QCNetworkDefaultLogger *>(logger.get())->setMinLogLevel(NetworkLogLevel::Debug);
    manager.setLogger(logger);
    manager.setDebugTraceEnabled(mode != 1);
}
} // namespace

class tst_QCNetworkBuiltinLogRedaction : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(tst_QCNetworkBuiltinLogRedaction)

public:
    tst_QCNetworkBuiltinLogRedaction() = default;

private Q_SLOTS:
    void builtinLogs_data();
    void builtinLogs();
    void redactsOpaqueQueries();
    void transportFailureKeepsSafeDiagnostics();
    void fullRedirectAndWireIdentity();
    void quotedRedirectUrlsKeepSecretsPrivate_data();
    void quotedRedirectUrlsKeepSecretsPrivate();
    void fullTransportFailureKeepsUrlField();
    void authenticationResendDoesNotReopenFull_data();
    void authenticationResendDoesNotReopenFull();
    void retryStartsNewFullAttempt_data();
    void retryStartsNewFullAttempt();
    void cookieTextKeepsCredentialsPrivate_data();
    void cookieTextKeepsCredentialsPrivate();
    void normalizedInitialTargetPreservesWireAndQuery_data();
    void normalizedInitialTargetPreservesWireAndQuery();
};

void tst_QCNetworkBuiltinLogRedaction::builtinLogs_data()
{
    QTest::addColumn<int>("mode");
    QTest::addColumn<int>("status");
    QTest::addColumn<QString>("query");
    QTest::addColumn<QCNetworkDiagnosticQueryPolicy>("policy");
    const QStringList queries = {
        QString(),
        QStringLiteral("synthetic-opaque-authorization-417-unique-marker"),
        QStringLiteral("ordinary=visible&opaque=synthetic-opaque-authorization-417-unique-marker&"
                       "encoded=%2B%2F%26%3D&ordinary=repeat+value"),
        QStringLiteral("prefix%20synthetic-space-authorization-515-unique-marker"),
    };
    for (int mode = 0; mode < 8; ++mode) {
        for (int status : {200, 302, 401, 403, 503}) {
            for (int index = 0; index < queries.size(); ++index) {
                for (const auto policy : {QCNetworkDiagnosticQueryPolicy::Redacted,
                                          QCNetworkDiagnosticQueryPolicy::Full}) {
                    QTest::newRow(qPrintable(QStringLiteral("mode-%1-status-%2-query-%3-policy-%4")
                                                 .arg(mode)
                                                 .arg(status)
                                                 .arg(index)
                                                 .arg(static_cast<int>(policy))))
                        << mode << status << queries.at(index) << policy;
                }
            }
        }
    }
}

void tst_QCNetworkBuiltinLogRedaction::builtinLogs()
{
    QFETCH(int, mode);
    QFETCH(int, status);
    QFETCH(QString, query);
    QFETCH(QCNetworkDiagnosticQueryPolicy, policy);
    const QByteArray headers
        = status == 302
              ? QByteArrayLiteral(
                    "Location: /redirect?synthetic-redirect-authorization-913-unique-marker\r\n")
              : QByteArray();
    HttpScriptServer server({HttpScriptServer::response(status, "body", headers)});
    QVERIFY(server.start());
    const bool previousDebug = QLoggingCategory::defaultCategory()->isDebugEnabled();
    QLoggingCategory::defaultCategory()->setEnabled(QtDebugMsg, true);
    s_messages.clear();
    const auto previous = qInstallMessageHandler(captureMessage);
    const auto restore  = qScopeGuard([previous, previousDebug]() {
        qInstallMessageHandler(previous);
        QLoggingCategory::defaultCategory()->setEnabled(QtDebugMsg, previousDebug);
    });
    QCNetworkAccessManager manager;
    QCLoggingMiddleware logging;
    configureLogger(manager, mode);
    if (mode == 3) {
        manager.addMiddleware(&logging);
    }
    auto request = loggingRequest(server.url(), mode, query);
    request.setDiagnosticQueryPolicy(policy);
    auto url = request.url();
    url.setFragment(QStringLiteral("synthetic-fragment-secret"));
    request.setUrl(url);
    request.setRawHeader(QByteArrayLiteral("Authorization"),
                         QByteArrayLiteral("Bearer header-secret"));
    request.setRawHeader(QByteArrayLiteral("Cookie"), QByteArrayLiteral("cookie-secret"));
    if (mode == 4) {
        qDebug() << request;
    }
    auto *reply = manager.get(request);
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    QVERIFY(finished.wait());
    QCOMPARE(reply->httpStatusCode(), status);
    QCOMPARE(reply->error() == NetworkError::NoError, status < 400);
    const QString log = s_messages.join('\n');
    const bool full   = policy == QCNetworkDiagnosticQueryPolicy::Full;
    if (!full) {
        QVERIFY(!log.contains(QStringLiteral("synthetic-opaque-authorization-417-unique-marker")));
        QVERIFY(!log.contains(QStringLiteral("%2B%2F%26%3D")));
        QVERIFY(!log.contains(QStringLiteral("synthetic-space-authorization-515-unique-marker")));
    }
    QVERIFY(!log.contains(QStringLiteral("synthetic-redirect-authorization-913-unique-marker")));
    QVERIFY(!log.contains(QStringLiteral("synthetic-password-913")));
    QVERIFY(!log.contains(QStringLiteral("synthetic-fragment-secret")));
    QVERIFY(!log.contains(QStringLiteral("header-secret")));
    QVERIFY(!log.contains(QStringLiteral("cookie-secret")));
    QCOMPARE(reply->url(), request.url());
    QCOMPARE(reply->diagnosticQueryPolicy(), policy);
    QVERIFY(log.contains(QStringLiteral("/resource")));
    QVERIFY(log.contains(QStringLiteral("127.0.0.1")));
    const QList<QByteArray> requests = server.requests();
    QCOMPARE(requests.size(), 1);
    const QByteArray target = QByteArrayLiteral("/resource")
                              + (query.isEmpty() ? QByteArray()
                                                 : QByteArrayLiteral("?") + query.toLatin1());
    QVERIFY(requests.constFirst().startsWith(QByteArrayLiteral("GET ") + target
                                             + QByteArrayLiteral(" HTTP/1.1\r\n")));
    if (!query.isEmpty()) {
        if (full) {
            QVERIFY(log.contains(request.url().query(QUrl::FullyEncoded)));
        } else {
            QVERIFY(log.contains(QStringLiteral("?[REDACTED]")));
        }
    }
    if (mode == 2 || mode == 7) {
        QVERIFY(log.contains(QStringLiteral("HEADER_OUT")));
    }
}

void tst_QCNetworkBuiltinLogRedaction::redactsOpaqueQueries()
{
    QCOMPARE(QCNetworkLogRedaction::redactSensitiveQueryParams(QStringLiteral(
                 "GET /resource?opaque=synthetic-query-secret-417#fragment HTTP/1.1")),
             QStringLiteral("GET /resource?[REDACTED] HTTP/1.1"));
    QCOMPARE(QCNetworkLogRedaction::redactSensitiveQueryParams(
                 QStringLiteral("GET /resource?prefix synthetic-decoded-space-secret HTTP/1.1")),
             QStringLiteral("GET /resource?[REDACTED] HTTP/1.1"));
    QCOMPARE(QCNetworkLogRedaction::redactSensitiveQueryParams(
                 QStringLiteral("Location: /resource?prefix synthetic-decoded-space-secret\r\n"
                                "GET /next?another secret HTTP/2\r\n")),
             QStringLiteral("Location: /resource?[REDACTED]\r\nGET /next?[REDACTED] HTTP/2\r\n"));
    QCOMPARE(QCNetworkLogRedaction::redactUrl(QUrl(QStringLiteral(
                 "https://user:password@example.invalid/path?prefix%20synthetic-space-secret"))),
             QStringLiteral("https://example.invalid/path?[REDACTED]"));
    QCOMPARE(QCNetworkLogRedaction::redactSensitiveQueryParams(
                 QStringLiteral("GET /resource HTTP/1.1")),
             QStringLiteral("GET /resource HTTP/1.1"));
    QCOMPARE(QCNetworkLogRedaction::redactSensitiveQueryParams(
                 QStringLiteral("GET /resource? HTTP/1.1")),
             QStringLiteral("GET /resource?[REDACTED] HTTP/1.1"));
}

void tst_QCNetworkBuiltinLogRedaction::transportFailureKeepsSafeDiagnostics()
{
    HttpScriptServer unused({});
    QVERIFY(unused.start());
    const QUrl url      = unused.url();
    const auto previous = qInstallMessageHandler(captureMessage);
    const auto restore  = qScopeGuard([previous] { qInstallMessageHandler(previous); });
    s_messages.clear();
    QCNetworkAccessManager manager;
    QCLoggingMiddleware middleware;
    manager.addMiddleware(&middleware);
    configureLogger(manager, 2);
    const auto request = loggingRequest(url,
                                        4,
                                        QStringLiteral(
                                            "synthetic-transport-authorization-719-unique-marker"));
    qDebug() << request;
    // 服务端接受后直接关闭，产生真实传输错误，而不是伪造 errorString。
    auto *reply = manager.get(request);
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    QVERIFY(finished.wait());
    QVERIFY(reply->error() != NetworkError::NoError);
    const QString log = s_messages.join(QLatin1Char('\n'));
    QVERIFY(!log.contains(QStringLiteral("synthetic-transport-authorization-719-unique-marker")));
    QVERIFY(log.contains(QStringLiteral("?[REDACTED]")));
    QVERIFY(log.contains(QStringLiteral("/resource")));
}

void tst_QCNetworkBuiltinLogRedaction::fullRedirectAndWireIdentity()
{
    HttpScriptServer server({
        HttpScriptServer::response(
            302,
            {},
            QByteArrayLiteral("Location: /redirect?redirect-secret#redirect-fragment-secret\r\n")),
        HttpScriptServer::response(200, QByteArrayLiteral("OK")),
        HttpScriptServer::response(200, QByteArrayLiteral("OK")),
    });
    QVERIFY(server.start());
    const bool previousDebug = QLoggingCategory::defaultCategory()->isDebugEnabled();
    QLoggingCategory::defaultCategory()->setEnabled(QtDebugMsg, true);
    const auto previous = qInstallMessageHandler(captureMessage);
    const auto restore  = qScopeGuard([previous, previousDebug] {
        qInstallMessageHandler(previous);
        QLoggingCategory::defaultCategory()->setEnabled(QtDebugMsg, previousDebug);
    });
    s_messages.clear();
    QCNetworkAccessManager manager;
    configureLogger(manager, 2);
    auto url = server.url();
    url.setQuery(QStringLiteral("page=2&encoded=%2B"));
    url.setFragment(QStringLiteral("initial-fragment-secret"));
    QCNetworkRequest request(url);
    request.setDiagnosticQueryPolicy(QCNetworkDiagnosticQueryPolicy::Full);
    auto *reply = manager.get(request);
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    QVERIFY(finished.wait());
    QCOMPARE(reply->error(), NetworkError::NoError);
    const QString log = s_messages.join(QLatin1Char('\n'));
    QVERIFY(log.contains(QStringLiteral("GET /resource?page=2&encoded=%2B HTTP/1.1")));
    QVERIFY(log.contains(QStringLiteral("GET /redirect?[REDACTED] HTTP/1.1")));
    QVERIFY(log.contains(QStringLiteral("Location: /redirect?[REDACTED]")));
    QVERIFY(!log.contains(QStringLiteral("redirect-secret")));
    QVERIFY(!log.contains(QStringLiteral("fragment-secret")));
    const auto first = server.requests().first().split('\n').first();
    request.setFollowLocation(false).setDiagnosticQueryPolicy(
        QCNetworkDiagnosticQueryPolicy::Redacted);
    reply = manager.get(request);
    QSignalSpy redactedFinished(reply, &QCNetworkReply::finished);
    QVERIFY(redactedFinished.wait());
    QCOMPARE(server.requests().size(), 3);
    QCOMPARE(server.requests().last().split('\n').first(), first);
    QCOMPARE(reply->url(), url);
}

void tst_QCNetworkBuiltinLogRedaction::quotedRedirectUrlsKeepSecretsPrivate_data()
{
    QTest::addColumn<QByteArray>("location");
    QTest::addColumn<QCNetworkDiagnosticQueryPolicy>("policy");
    const QList<QByteArray> locations = {
        QByteArrayLiteral("/next#prefix'fragment-marker"),
        QByteArrayLiteral("http://user:prefix'credential-marker@diagnostic-target.invalid/next"),
        QByteArrayLiteral("http://user:prefix'credential-marker@diagnostic-target.invalid/next"
                          "?route=query-marker#prefix'fragment-marker"),
        QByteArrayLiteral("/next'path#prefix'fragment-marker"),
    };
    for (qsizetype index = 0; index < locations.size(); ++index) {
        for (const auto policy :
             {QCNetworkDiagnosticQueryPolicy::Redacted, QCNetworkDiagnosticQueryPolicy::Full}) {
            QTest::newRow(qPrintable(
                QStringLiteral("location-%1-policy-%2").arg(index).arg(static_cast<int>(policy))))
                << locations.at(index) << policy;
        }
    }
}

void tst_QCNetworkBuiltinLogRedaction::quotedRedirectUrlsKeepSecretsPrivate()
{
    QFETCH(QByteArray, location);
    QFETCH(QCNetworkDiagnosticQueryPolicy, policy);
    const QByteArray headers = QByteArrayLiteral("Location: ") + location
                               + QByteArrayLiteral("\r\n");
    HttpScriptServer server({HttpScriptServer::response(302, {}, headers),
                             HttpScriptServer::response(200, QByteArrayLiteral("OK")),
                             HttpScriptServer::response(302, {}, headers)});
    QVERIFY(server.start());
    const QByteArray previousNoProxy      = qgetenv("no_proxy");
    const QByteArray previousUpperNoProxy = qgetenv("NO_PROXY");
    const bool hadNoProxy                 = qEnvironmentVariableIsSet("no_proxy");
    const bool hadUpperNoProxy            = qEnvironmentVariableIsSet("NO_PROXY");
    const auto restoreEnvironment         = qScopeGuard([&] {
        if (hadNoProxy) {
            qputenv("no_proxy", previousNoProxy);
        } else {
            qunsetenv("no_proxy");
        }
        if (hadUpperNoProxy) {
            qputenv("NO_PROXY", previousUpperNoProxy);
        } else {
            qunsetenv("NO_PROXY");
        }
    });
    qunsetenv("no_proxy");
    qunsetenv("NO_PROXY");
    QCNetworkDefaultLogger *capture = nullptr;
    const auto logger               = QCNetworkLoggerHandle::createWithBorrow(&capture);
    capture->enableConsoleOutput(false);
    capture->setMinLogLevel(NetworkLogLevel::Debug);
    QCNetworkAccessManager manager;
    manager.setLogger(logger);
    manager.setDebugTraceEnabled(true);
    // 回环夹具兼作 HTTP 代理；absolute Location 的测试域名不解析、不访问外网。
    const QUrl initialUrl(QStringLiteral("http://diagnostic-target.invalid/resource"));
    QCNetworkRequest request(initialUrl);
    request.setDiagnosticQueryPolicy(policy);
    QCNetworkProxyConfig proxy;
    proxy.setType(QCNetworkProxyConfig::ProxyType::Http);
    proxy.setHostName(QStringLiteral("127.0.0.1"));
    proxy.setPort(static_cast<quint16>(server.url().port()));
    request.setProxyConfig(proxy);
    auto *reply = manager.get(request);
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    QVERIFY(finished.wait(5000));
    QVERIFY2(reply->error() == NetworkError::NoError, qPrintable(reply->errorString()));
    QCOMPARE(reply->httpStatusCode(), 200);
    QCOMPARE(reply->url(), initialUrl);
    QCOMPARE(reply->diagnosticQueryPolicy(), policy);
    const auto requests = server.requests();
    QCOMPARE(requests.size(), 2);
    const QUrl targetUrl = initialUrl.resolved(QUrl::fromEncoded(location, QUrl::StrictMode));
    QVERIFY(targetUrl.isValid());
    const QByteArray wireTarget = targetUrl.toEncoded(QUrl::RemoveUserInfo | QUrl::RemoveFragment);
    QCOMPARE(requests.last().split('\n').first(),
             QByteArrayLiteral("GET ") + wireTarget + QByteArrayLiteral(" HTTP/1.1\r"));
    const QString safeTarget   = QCNetworkLogRedaction::redactUrl(targetUrl);
    const QString expectedText = QStringLiteral("TEXT: Issue another request to this URL: '")
                                 + safeTarget + QLatin1Char('\'');
    request.setFollowLocation(false);
    reply = manager.get(request);
    QSignalSpy rawFinished(reply, &QCNetworkReply::finished);
    QVERIFY(rawFinished.wait(5000));
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->httpStatusCode(), 302);
    QCOMPARE(reply->rawHeader(QByteArrayLiteral("Location")), location);
    QCOMPARE(server.requests().size(), 3);
    QCOMPARE(server.requests().last().split('\n').first(), requests.first().split('\n').first());
    bool redirectTextObserved = false;
    for (const auto &entry : capture->entries()) {
        for (const auto &marker : {QStringLiteral("credential-marker"),
                                   QStringLiteral("fragment-marker"),
                                   QStringLiteral("query-marker")}) {
            QVERIFY2(!entry.message().contains(marker), qPrintable(entry.message()));
        }
        if (entry.category() == QStringLiteral("Trace") && entry.message() == expectedText) {
            redirectTextObserved = true;
        }
    }
    // 必须实际观察到安全 URL，而不是因关闭 trace 或丢掉全部 TEXT 使断言假绿。
    QVERIFY(redirectTextObserved);
}

void tst_QCNetworkBuiltinLogRedaction::fullTransportFailureKeepsUrlField()
{
    HttpScriptServer unused({});
    QVERIFY(unused.start());
    const bool previousDebug = QLoggingCategory::defaultCategory()->isDebugEnabled();
    QLoggingCategory::defaultCategory()->setEnabled(QtDebugMsg, true);
    const auto previous = qInstallMessageHandler(captureMessage);
    const auto restore  = qScopeGuard([previous, previousDebug] {
        qInstallMessageHandler(previous);
        QLoggingCategory::defaultCategory()->setEnabled(QtDebugMsg, previousDebug);
    });
    s_messages.clear();
    QCNetworkAccessManager manager;
    QCLoggingMiddleware middleware;
    manager.addMiddleware(&middleware);
    auto url = unused.url();
    url.setQuery(QStringLiteral("page=2"));
    url.setFragment(QStringLiteral("fragment-secret"));
    QCNetworkRequest request(url);
    request.setDiagnosticQueryPolicy(QCNetworkDiagnosticQueryPolicy::Full);
    auto *reply = manager.get(request);
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    QVERIFY(finished.wait());
    QVERIFY(reply->error() != NetworkError::NoError);
    const QString log = s_messages.join(QLatin1Char('\n'));
    QVERIFY(log.contains(QStringLiteral("Response error:")));
    for (const auto &line : s_messages) {
        if (line.contains(QStringLiteral("Response error:"))) {
            QVERIFY(line.contains(QStringLiteral("page=2")));
            QVERIFY(!line.contains(QStringLiteral("fragment-secret")));
        }
    }
}

void tst_QCNetworkBuiltinLogRedaction::authenticationResendDoesNotReopenFull_data()
{
    QTest::addColumn<QCNetworkDiagnosticQueryPolicy>("policy");
    QTest::addColumn<QString>("userName");
    for (const auto &userName :
         {QStringLiteral("synthetic-auth-user"),
          QStringLiteral("synthetic-auth-user\nsynthetic-auth-tail-marker")}) {
        const QString suffix = userName.contains(QLatin1Char('\n')) ? QStringLiteral("-multiline")
                                                                    : QString();
        QTest::newRow(qPrintable(QStringLiteral("redacted") + suffix))
            << QCNetworkDiagnosticQueryPolicy::Redacted << userName;
        QTest::newRow(qPrintable(QStringLiteral("full") + suffix))
            << QCNetworkDiagnosticQueryPolicy::Full << userName;
    }
}

void tst_QCNetworkBuiltinLogRedaction::authenticationResendDoesNotReopenFull()
{
    QFETCH(QCNetworkDiagnosticQueryPolicy, policy);
    QFETCH(QString, userName);
    HttpScriptServer server({
        HttpScriptServer::response(401,
                                   {},
                                   QByteArrayLiteral(
                                       "WWW-Authenticate: Basic realm=\"qcurl-fixture\"\r\n")),
        HttpScriptServer::response(200, QByteArrayLiteral("OK")),
    });
    QVERIFY(server.start());
    QCNetworkDefaultLogger *capture = nullptr;
    const auto logger               = QCNetworkLoggerHandle::createWithBorrow(&capture);
    capture->enableConsoleOutput(false);
    capture->setMinLogLevel(NetworkLogLevel::Debug);
    QCNetworkAccessManager manager;
    manager.setLogger(logger);
    manager.setDebugTraceEnabled(true);
    auto url = server.url();
    url.setQuery(QStringLiteral("route=auth-query-marker"));
    QCNetworkRequest request(url);
    request.setDiagnosticQueryPolicy(policy);
    QCNetworkHttpAuthConfig auth;
    auth.setMethod(QCNetworkHttpAuthMethod::Any);
    auth.setUserName(userName);
    auth.setPassword(QStringLiteral("synthetic-auth-secret"));
    request.setHttpAuth(auth);
    auto *reply = manager.get(request);
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    QVERIFY(finished.wait(5000));
    QVERIFY2(reply->error() == NetworkError::NoError, qPrintable(reply->errorString()));
    QCOMPARE(reply->retryAttemptCount(), 0);
    const auto requests = server.requests();
    QCOMPARE(requests.size(), 2);
    QVERIFY(!requests.first().contains("Authorization:"));
    const auto credentials = (userName.toUtf8() + QByteArrayLiteral(":synthetic-auth-secret"))
                                 .toBase64();
    QVERIFY(requests.last().contains(QByteArrayLiteral("Authorization: Basic ") + credentials));
    QCOMPARE(requests.first().split('\n').first(), requests.last().split('\n').first());
    QString outgoing;
    bool authTextObserved     = false;
    const bool multiline      = userName.contains(QLatin1Char('\n'));
    const QString authSummary = multiline ? QStringLiteral("[REDACTED: credential record]")
                                          : QStringLiteral("Server auth using Basic");
    bool continuationObserved = false;
    for (const auto &entry : capture->entries()) {
        const QString message = entry.message();
        QVERIFY(!message.contains(QStringLiteral("synthetic-auth-user")));
        QVERIFY(!message.contains(QStringLiteral("synthetic-auth-tail-marker")));
        QVERIFY(!message.contains(QStringLiteral("synthetic-auth-secret")));
        QVERIFY(!message.contains(QString::fromLatin1(credentials)));
        if (entry.category() == QStringLiteral("Trace")
            && message.startsWith(QStringLiteral("TEXT:"))) {
            authTextObserved |= message.contains(authSummary);
            continuationObserved |= message.contains(
                QStringLiteral("[REDACTED: credential continuation]"));
        }
        if (entry.category() == QStringLiteral("Trace")
            && message.startsWith(QStringLiteral("HEADER_OUT:"))) {
            outgoing += message + QLatin1Char('\n');
        }
    }
    QVERIFY(authTextObserved);
    QCOMPARE(continuationObserved, multiline);
    QCOMPARE(outgoing.count(QStringLiteral("GET /resource")), 2);
    const bool full = policy == QCNetworkDiagnosticQueryPolicy::Full;
    QCOMPARE(outgoing.count(QStringLiteral("GET /resource?route=auth-query-marker HTTP/1.1")),
             full ? 1 : 0);
    QCOMPARE(outgoing.count(QStringLiteral("GET /resource?[REDACTED] HTTP/1.1")), full ? 1 : 2);
    QVERIFY(outgoing.contains(QStringLiteral("Authorization: [REDACTED]")));
}

void tst_QCNetworkBuiltinLogRedaction::retryStartsNewFullAttempt_data()
{
    QTest::addColumn<QCNetworkDiagnosticQueryPolicy>("policy");
    QTest::newRow("redacted") << QCNetworkDiagnosticQueryPolicy::Redacted;
    QTest::newRow("full") << QCNetworkDiagnosticQueryPolicy::Full;
}

void tst_QCNetworkBuiltinLogRedaction::retryStartsNewFullAttempt()
{
    QFETCH(QCNetworkDiagnosticQueryPolicy, policy);
    HttpScriptServer server({HttpScriptServer::response(503, {}),
                             HttpScriptServer::response(200, QByteArrayLiteral("OK"))});
    QVERIFY(server.start());
    QCNetworkDefaultLogger *capture = nullptr;
    const auto logger               = QCNetworkLoggerHandle::createWithBorrow(&capture);
    capture->enableConsoleOutput(false);
    capture->setMinLogLevel(NetworkLogLevel::Debug);
    QCNetworkAccessManager manager;
    manager.setLogger(logger);
    manager.setDebugTraceEnabled(true);
    auto url = server.url();
    url.setQuery(QStringLiteral("route=retry-query-marker"));
    QCNetworkRequest request(url);
    request.setDiagnosticQueryPolicy(policy);
    request.setRetryPolicy(TestSupport::makeRetryPolicyOrFail(1, std::chrono::milliseconds(1)));
    request.setRawHeader("Authorization", "Bearer retry-header-marker");
    auto *reply = manager.get(request);
    request.setDiagnosticQueryPolicy(QCNetworkDiagnosticQueryPolicy::Redacted);
    QSignalSpy retry(reply, &QCNetworkReply::retryAttempt);
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    QVERIFY(finished.wait(5000));
    QVERIFY2(reply->error() == NetworkError::NoError, qPrintable(reply->errorString()));
    QCOMPARE(retry.size(), 1);
    QCOMPARE(reply->retryAttemptCount(), 1);
    QCOMPARE(reply->diagnosticQueryPolicy(), policy);
    const auto requests = server.requests();
    QCOMPARE(requests.size(), 2);
    QCOMPARE(requests.first().split('\n').first(), requests.last().split('\n').first());
    QString outgoing;
    for (const auto &entry : capture->entries()) {
        const QString message = entry.message();
        QVERIFY(!message.contains(QStringLiteral("retry-header-marker")));
        if (entry.category() == QStringLiteral("Trace")
            && message.startsWith(QStringLiteral("HEADER_OUT:"))) {
            outgoing += message + QLatin1Char('\n');
        }
    }
    const QString target = policy == QCNetworkDiagnosticQueryPolicy::Full
                               ? QStringLiteral("GET /resource?route=retry-query-marker HTTP/1.1")
                               : QStringLiteral("GET /resource?[REDACTED] HTTP/1.1");
    QCOMPARE(outgoing.count(target), 2);
    QVERIFY(outgoing.contains(QStringLiteral("Authorization: [REDACTED]")));
}

void tst_QCNetworkBuiltinLogRedaction::cookieTextKeepsCredentialsPrivate_data()
{
    retryStartsNewFullAttempt_data();
}

void tst_QCNetworkBuiltinLogRedaction::cookieTextKeepsCredentialsPrivate()
{
    QFETCH(QCNetworkDiagnosticQueryPolicy, policy);
    HttpScriptServer server({
        HttpScriptServer::response(
            200,
            QByteArrayLiteral("OK"),
            QByteArrayLiteral("Set-Cookie: session=synthetic-cookie-added-marker; Path=/\r\n")),
        HttpScriptServer::response(
            200,
            QByteArrayLiteral("OK"),
            QByteArrayLiteral("Set-Cookie: session=synthetic-cookie-replaced-marker; Path=/\r\n")),
        HttpScriptServer::response(200, QByteArrayLiteral("OK")),
    });
    QVERIFY(server.start());
    QCNetworkDefaultLogger *capture = nullptr;
    const auto logger               = QCNetworkLoggerHandle::createWithBorrow(&capture);
    capture->enableConsoleOutput(false);
    capture->setMinLogLevel(NetworkLogLevel::Debug);
    QCNetworkAccessManager manager;
    manager.setLogger(logger);
    manager.setDebugTraceEnabled(true);
    QCNetworkAccessManager::ShareHandleConfig share;
    share.setShareCookies(true);
    manager.setShareHandleConfig(share);
    QCNetworkRequest request(server.url());
    request.setDiagnosticQueryPolicy(policy);
    for (int index = 0; index < 3; ++index) {
        auto *reply = manager.get(request);
        QSignalSpy finished(reply, &QCNetworkReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->error() == NetworkError::NoError, qPrintable(reply->errorString()));
        if (index < 2) {
            const QByteArray value = index == 0
                                         ? QByteArrayLiteral("synthetic-cookie-added-marker")
                                         : QByteArrayLiteral("synthetic-cookie-replaced-marker");
            QVERIFY(reply->rawHeader("Set-Cookie").contains(value));
        }
    }
    const auto requests = server.requests();
    QCOMPARE(requests.size(), 3);
    QVERIFY(!requests.first().contains("Cookie:"));
    QVERIFY(requests.at(1).contains("Cookie: session=synthetic-cookie-added-marker"));
    QVERIFY(requests.last().contains("Cookie: session=synthetic-cookie-replaced-marker"));
    QString text;
    for (const auto &entry : capture->entries()) {
        QVERIFY(!entry.message().contains(QStringLiteral("synthetic-cookie-added-marker")));
        QVERIFY(!entry.message().contains(QStringLiteral("synthetic-cookie-replaced-marker")));
        if (entry.category() == QStringLiteral("Trace")
            && entry.message().startsWith(QStringLiteral("TEXT:"))) {
            text += entry.message() + QLatin1Char('\n');
        }
    }
    // 操作摘要证明 Cookie engine 的真实 TEXT 经过日志链，不能用关闭输出造成假绿。
    QVERIFY(text.contains(QStringLiteral("Added cookie [REDACTED]")));
    QVERIFY(text.contains(QStringLiteral("Replaced cookie [REDACTED]")));
}

void tst_QCNetworkBuiltinLogRedaction::normalizedInitialTargetPreservesWireAndQuery_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<QString>("normalizedPath");
    QTest::addColumn<bool>("absoluteForm");
    for (const auto &path : {QStringLiteral("/resource"),
                             QStringLiteral("/a/./resource"),
                             QStringLiteral("/a/../resource")}) {
        for (bool absoluteForm : {false, true}) {
            QTest::newRow(qPrintable(
                path + (absoluteForm ? QStringLiteral("-absolute") : QStringLiteral("-origin"))))
                << path
                << (path == QStringLiteral("/a/./resource") ? QStringLiteral("/a/resource")
                                                            : QStringLiteral("/resource"))
                << absoluteForm;
        }
    }
}

void tst_QCNetworkBuiltinLogRedaction::normalizedInitialTargetPreservesWireAndQuery()
{
    QFETCH(QString, path);
    QFETCH(QString, normalizedPath);
    QFETCH(bool, absoluteForm);
    HttpScriptServer server({HttpScriptServer::response(200, QByteArrayLiteral("OK")),
                             HttpScriptServer::response(200, QByteArrayLiteral("OK"))});
    QVERIFY(server.start());
    const QByteArray previousNoProxy      = qgetenv("no_proxy");
    const QByteArray previousUpperNoProxy = qgetenv("NO_PROXY");
    const bool hadNoProxy                 = qEnvironmentVariableIsSet("no_proxy");
    const bool hadUpperNoProxy            = qEnvironmentVariableIsSet("NO_PROXY");
    const auto restoreEnvironment         = qScopeGuard([&] {
        if (hadNoProxy) {
            qputenv("no_proxy", previousNoProxy);
        } else {
            qunsetenv("no_proxy");
        }
        if (hadUpperNoProxy) {
            qputenv("NO_PROXY", previousUpperNoProxy);
        } else {
            qunsetenv("NO_PROXY");
        }
    });
    qunsetenv("no_proxy");
    qunsetenv("NO_PROXY");
    QCNetworkDefaultLogger *capture = nullptr;
    const auto logger               = QCNetworkLoggerHandle::createWithBorrow(&capture);
    capture->enableConsoleOutput(false);
    capture->setMinLogLevel(NetworkLogLevel::Debug);
    QCNetworkAccessManager manager;
    manager.setLogger(logger);
    manager.setDebugTraceEnabled(true);
    QUrl url = absoluteForm ? QUrl(QStringLiteral("http://diagnostic-target.invalid:80"))
                            : server.url();
    url.setPath(path);
    url.setQuery(QStringLiteral("route=normalized-query-marker&encoded=%2B"));
    url.setFragment(QStringLiteral("normalized-fragment-marker"));
    QCNetworkRequest request(url);
    if (absoluteForm) {
        // 夹具扮演 HTTP 代理，直接回复；目标域名无需解析，也不访问任何外部服务。
        QCNetworkProxyConfig proxy;
        proxy.setType(QCNetworkProxyConfig::ProxyType::Http);
        proxy.setHostName(QStringLiteral("127.0.0.1"));
        proxy.setPort(static_cast<quint16>(server.url().port()));
        request.setProxyConfig(proxy);
    }
    for (const auto policy :
         {QCNetworkDiagnosticQueryPolicy::Full, QCNetworkDiagnosticQueryPolicy::Redacted}) {
        request.setDiagnosticQueryPolicy(policy);
        auto *reply = manager.get(request);
        QSignalSpy finished(reply, &QCNetworkReply::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY2(reply->error() == NetworkError::NoError, qPrintable(reply->errorString()));
        QCOMPARE(reply->url(), url);
    }
    const auto requests = server.requests();
    QCOMPARE(requests.size(), 2);
    const QByteArray target = (absoluteForm ? QByteArrayLiteral("http://diagnostic-target.invalid")
                                            : QByteArray())
                              + normalizedPath.toLatin1();
    const QByteArray initialLine = QByteArrayLiteral("GET ") + target
                                   + QByteArrayLiteral(
                                       "?route=normalized-query-marker&encoded=%2B HTTP/1.1\r");
    QCOMPARE(requests.first().split('\n').first(), initialLine);
    QCOMPARE(requests.last().split('\n').first(), initialLine);
    QString outgoing;
    for (const auto &entry : capture->entries()) {
        QVERIFY(!entry.message().contains(QStringLiteral("normalized-fragment-marker")));
        if (entry.category() == QStringLiteral("Trace")
            && entry.message().startsWith(QStringLiteral("HEADER_OUT:"))) {
            outgoing += entry.message() + QLatin1Char('\n');
        }
    }
    QCOMPARE(outgoing.count(QString::fromLatin1(initialLine.chopped(1))), 1);
    QCOMPARE(outgoing.count(QStringLiteral("GET ") + QString::fromLatin1(target)
                            + QStringLiteral("?[REDACTED] HTTP/1.1")),
             1);
}

QTEST_GUILESS_MAIN(tst_QCNetworkBuiltinLogRedaction)
#include "tst_QCNetworkBuiltinLogRedaction.moc"
