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
void verifyNormalizedTargetTrace(const QCNetworkDefaultLogger &capture,
                                 const QByteArray &initialLine,
                                 const QByteArray &target)
{
    QString outgoing;
    for (const auto &entry : capture.entries()) {
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
} // namespace

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
    const NoProxyGuard environment;
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
    verifyNormalizedTargetTrace(*capture, initialLine, target);
}

void tst_QCNetworkBuiltinLogRedaction::libIdsTraceKeepsSafeHeaders_data()
{
    QTest::addColumn<QCNetworkDiagnosticQueryPolicy>("policy");
    QTest::newRow("redacted") << QCNetworkDiagnosticQueryPolicy::Redacted;
    QTest::newRow("full") << QCNetworkDiagnosticQueryPolicy::Full;
}

void tst_QCNetworkBuiltinLogRedaction::libIdsTraceKeepsSafeHeaders()
{
    QFETCH(QCNetworkDiagnosticQueryPolicy, policy);
    HttpScriptServer server({HttpScriptServer::response(
        200,
        "body",
        "Set-Cookie: session=response-secret\r\nX-Auth-Token: response-secret\r\n"
        "X-API-Key: response-secret\r\nX-Signature: response-secret\r\nX-Public: "
        "safe-marker\r\n")});
    QVERIFY(server.start());
    QCNetworkDefaultLogger *capture = nullptr;
    const auto logger               = QCNetworkLoggerHandle::createWithBorrow(&capture);
    capture->enableConsoleOutput(false);
    capture->setMinLogLevel(NetworkLogLevel::Debug);
    QCNetworkAccessManager manager;
    manager.setLogger(logger);
    manager.setDebugTraceEnabled(true);
    QUrl url = server.url();
    url.setQuery(QStringLiteral("route=query-marker&encoded=%2B"));
    QCNetworkRequest request(url);
    request.setDiagnosticQueryPolicy(policy);
    for (const auto &name : {"Authorization",
                             "Proxy-Authorization",
                             "Cookie",
                             "X-Auth-Token",
                             "X-API-Key",
                             "X-Signature"}) {
        request.setRawHeader(name, "request-secret");
    }
    auto *reply = manager.get(request);
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    QVERIFY(finished.wait(5000));
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->rawHeader("Set-Cookie"), QByteArrayLiteral("session=response-secret"));
    QCOMPARE(reply->url(), url);
    const auto wire = server.requests().first();
    QVERIFY(wire.startsWith("GET /resource?route=query-marker&encoded=%2B HTTP/1.1\r\n"));
    QString trace;
    for (const auto &entry : capture->entries()) {
        if (entry.category() == QStringLiteral("Trace")) {
            QVERIFY(entry.message().size() <= 4096);
            trace += entry.message() + QLatin1Char('\n');
        }
    }
    QVERIFY2(!trace.contains(QStringLiteral("session=response-secret")), qPrintable(trace));
    QVERIFY2(!trace.contains(QStringLiteral("response-secret")), qPrintable(trace));
    QVERIFY2(!trace.contains(QStringLiteral("request-secret")), qPrintable(trace));
    for (const auto &name : {"Set-Cookie", "X-Auth-Token", "X-API-Key", "X-Signature"}) {
        QVERIFY2(trace.contains(QString::fromLatin1(name) + QStringLiteral(": [REDACTED]")),
                 qPrintable(trace));
    }
    QVERIFY(trace.contains(QStringLiteral("X-Public: safe-marker")));
    const QString target = policy == QCNetworkDiagnosticQueryPolicy::Full
                               ? QStringLiteral(
                                     "GET /resource?route=query-marker&encoded=%2B HTTP/1.1")
                               : QStringLiteral("GET /resource?[REDACTED] HTTP/1.1");
    QVERIFY2(trace.contains(target), qPrintable(trace));
    const QRegularExpression id(QStringLiteral(R"(HEADER_(?:IN|OUT): \[(\d+)-(\d+)\] )"));
    QCOMPARE(id.match(trace).hasMatch(), qgetenv("QCURL_TEST_LIB_IDS") == "on");
}
