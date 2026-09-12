#include "QCNetworkAccessManager.h"
#include "QCNetworkDefaultLogger.h"
#include "QCNetworkMiddleware.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "private/QCNetworkMiddlewareInternal_p.h"
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

QCNetworkRequest loggingRequest(QUrl url, int mode)
{
    url.setQuery(QStringLiteral("access_token=synthetic-secret-417&ordinary=visible"));
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
};

void tst_QCNetworkBuiltinLogRedaction::builtinLogs_data()
{
    QTest::addColumn<int>("mode");
    QTest::addColumn<int>("status");
    for (int mode = 0; mode < 8; ++mode) {
        for (int status : {200, 302, 503}) {
            QTest::newRow(qPrintable(QStringLiteral("mode-%1-status-%2").arg(mode).arg(status)))
                << mode << status;
        }
    }
}

void tst_QCNetworkBuiltinLogRedaction::builtinLogs()
{
    QFETCH(int, mode);
    QFETCH(int, status);
    const QByteArray headers
        = status == 302
              ? QByteArray(
                    "Location: /resource?access_token=synthetic-secret-417&ordinary=visible\r\n")
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
    const auto request = loggingRequest(server.url(), mode);
    if (mode == 4) {
        qDebug() << request;
    }
    auto *reply = manager.get(request);
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    QVERIFY(finished.wait());
    QCOMPARE(reply->httpStatusCode(), status);
    QCOMPARE(reply->error() == NetworkError::NoError, status < 400);
    const QString log = s_messages.join('\n');
    QVERIFY(!log.contains(QStringLiteral("synthetic-secret-417")));
    QVERIFY(!log.contains(QStringLiteral("synthetic-password-913")));
    QVERIFY(log.contains(QStringLiteral("ordinary=visible")));
    QVERIFY(log.contains(QStringLiteral("[REDACTED]")));
    if (mode == 2 || mode == 7) {
        QVERIFY(log.contains(QStringLiteral("HEADER_OUT")));
    }
}

QTEST_GUILESS_MAIN(tst_QCNetworkBuiltinLogRedaction)
#include "tst_QCNetworkBuiltinLogRedaction.moc"
