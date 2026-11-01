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
void verifyAuthenticationRequests(const QList<QByteArray> &requests, const QString &userName)
{
    QCOMPARE(requests.size(), 2);
    QVERIFY(!requests.first().contains("Authorization:"));
    const auto credentials = (userName.toUtf8() + QByteArrayLiteral(":synthetic-auth-secret"))
                                 .toBase64();
    QVERIFY(requests.last().contains(QByteArrayLiteral("Authorization: Basic ") + credentials));
    QCOMPARE(requests.first().split('\n').first(), requests.last().split('\n').first());
}

void verifyAuthenticationTrace(const QCNetworkDefaultLogger &capture,
                               const QString &userName,
                               QCNetworkDiagnosticQueryPolicy policy)
{
    QString outgoing;
    bool authTextObserved     = false;
    bool continuationObserved = false;
    const bool multiline      = userName.contains(QLatin1Char('\n'));
    const QString authSummary = multiline ? QStringLiteral("[REDACTED: credential record]")
                                          : QStringLiteral("Server auth using Basic");
    for (const auto &entry : capture.entries()) {
        const QString message = entry.message();
        QVERIFY(!message.contains(QStringLiteral("synthetic-auth-user")));
        QVERIFY(!message.contains(QStringLiteral("synthetic-auth-tail-marker")));
        QVERIFY(!message.contains(QStringLiteral("synthetic-auth-secret")));
        const auto credentials = (userName.toUtf8() + QByteArrayLiteral(":synthetic-auth-secret"))
                                     .toBase64();
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
    // 普通 TEXT 也可能触发疑似截断保护；多行凭据必须确实观察到受保护的尾段。
    if (multiline) {
        QVERIFY(continuationObserved);
    }
    QCOMPARE(outgoing.count(QStringLiteral("GET /resource")), 2);
    const bool full = policy == QCNetworkDiagnosticQueryPolicy::Full;
    QCOMPARE(outgoing.count(QStringLiteral("GET /resource?route=auth-query-marker HTTP/1.1")),
             full ? 1 : 0);
    QCOMPARE(outgoing.count(QStringLiteral("GET /resource?[REDACTED] HTTP/1.1")), full ? 1 : 2);
    QVERIFY(outgoing.contains(QStringLiteral("Authorization: [REDACTED]")));
}
} // namespace

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
    verifyAuthenticationRequests(server.requests(), userName);
    verifyAuthenticationTrace(*capture, userName, policy);
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
