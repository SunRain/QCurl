#include "QCNetworkAccessManager.h"
#include "QCNetworkDefaultLogger.h"
#include "QCNetworkDiagnosticQueryPolicy.h"
#include "QCNetworkMiddleware.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "private/QCNetworkLogRedaction_p.h"
#include "private/QCNetworkMiddlewareInternal_p.h"
#include "qcnetwork_builtin_log_test.h"
#include "qcnetwork_no_proxy_guard.h"
#include "qcnetwork_retry_policy_test_helper.h"
#include "qcnetwork_trace_mode.h"
#include "qcurl_http_script_server.h"

#include <QLoggingCategory>
#include <QRegularExpression>
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

void verifyBuiltinSecrets(const QString &log, QCNetworkDiagnosticQueryPolicy policy)
{
    if (policy == QCNetworkDiagnosticQueryPolicy::Redacted) {
        QVERIFY(!log.contains(QStringLiteral("synthetic-opaque-authorization-417-unique-marker")));
        QVERIFY(!log.contains(QStringLiteral("%2B%2F%26%3D")));
        QVERIFY(!log.contains(QStringLiteral("synthetic-space-authorization-515-unique-marker")));
    }
    for (const auto &secret : {QStringLiteral("synthetic-redirect-authorization-913-unique-marker"),
                               QStringLiteral("synthetic-password-913"),
                               QStringLiteral("synthetic-fragment-secret"),
                               QStringLiteral("header-secret"),
                               QStringLiteral("cookie-secret")}) {
        QVERIFY(!log.contains(secret));
    }
}

void verifyBuiltinWireTarget(const QList<QByteArray> &requests,
                             const QByteArray &target,
                             const QUrl &url,
                             const QString &query,
                             QCNetworkDiagnosticQueryPolicy policy,
                             const QString &log)
{
    QCOMPARE(requests.size(), 1);
    QVERIFY(requests.constFirst().startsWith(QByteArrayLiteral("GET ") + target
                                             + QByteArrayLiteral(" HTTP/1.1\r\n")));
    if (!query.isEmpty()) {
        if (policy == QCNetworkDiagnosticQueryPolicy::Full) {
            QVERIFY(log.contains(url.query(QUrl::FullyEncoded)));
        } else {
            QVERIFY(log.contains(QStringLiteral("?[REDACTED]")));
        }
    }
}

void verifyQuotedRedirectTrace(const QCNetworkDefaultLogger &capture, const QString &expectedText)
{
    bool redirectTextObserved = false;
    for (const auto &entry : capture.entries()) {
        for (const auto &marker : {QStringLiteral("credential-marker"),
                                   QStringLiteral("fragment-marker"),
                                   QStringLiteral("query-marker")}) {
            QVERIFY2(!entry.message().contains(marker), qPrintable(entry.message()));
        }
        if (entry.category() == QStringLiteral("Trace")
            && entry.message().startsWith(QStringLiteral("TEXT: "))
            && entry.message().endsWith(expectedText)) {
            redirectTextObserved = true;
        }
    }
    // 必须实际观察到安全 URL，而不是因关闭 trace 或丢掉全部 TEXT 使断言假绿。
    QVERIFY(redirectTextObserved);
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
    verifyBuiltinSecrets(log, policy);
    QCOMPARE(reply->url(), request.url());
    QCOMPARE(reply->diagnosticQueryPolicy(), policy);
    QVERIFY(log.contains(QStringLiteral("/resource")));
    QVERIFY(log.contains(QStringLiteral("127.0.0.1")));
    const QList<QByteArray> requests = server.requests();
    const QByteArray target = QByteArrayLiteral("/resource")
                              + (query.isEmpty() ? QByteArray()
                                                 : QByteArrayLiteral("?") + query.toLatin1());
    verifyBuiltinWireTarget(requests, target, request.url(), query, policy, log);
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
    const NoProxyGuard environment;
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
    const QString expectedText = QStringLiteral("Issue another request to this URL: '") + safeTarget
                                 + QLatin1Char('\'');
    request.setFollowLocation(false);
    reply = manager.get(request);
    QSignalSpy rawFinished(reply, &QCNetworkReply::finished);
    QVERIFY(rawFinished.wait(5000));
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->httpStatusCode(), 302);
    QCOMPARE(reply->rawHeader(QByteArrayLiteral("Location")), location);
    QCOMPARE(server.requests().size(), 3);
    QCOMPARE(server.requests().last().split('\n').first(), requests.first().split('\n').first());
    verifyQuotedRedirectTrace(*capture, expectedText);
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

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (!configureTraceMode()) {
        return 1;
    }
    tst_QCNetworkBuiltinLogRedaction test;
    const int result = QTest::qExec(&test, argc, argv);
    curl_global_cleanup();
    return result;
}
