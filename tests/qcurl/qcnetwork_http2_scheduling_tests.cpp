#include "QCCurlHandleManager.h"
#include "QCGlobal.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkConnectionPoolManager.h"
#include "QCNetworkDefaultLogger.h"
#include "QCNetworkDiagnosticQueryPolicy.h"
#include "QCNetworkError.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkSslConfig.h"
#include "qcnetwork_http2_test.h"
#include "qcnetwork_trace_mode.h"
#include "test_source_paths.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QProcess>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSet>
#include <QSharedPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QtTest/QtTest>

#include <curl/curl.h>

using namespace QCurl;

void TestQCNetworkHttp2::runSequentialRequests(const QString &baseUrl,
                                               QCNetworkHttpVersion version,
                                               const QString &queryName,
                                               qint64 *elapsed)
{
    constexpr int kRequestCount = 10;
    QElapsedTimer timer;
    timer.start();
    for (int index = 0; index < kRequestCount; ++index) {
        QCNetworkRequest request(
            QUrl(baseUrl + QStringLiteral("/reqinfo?%1=%2").arg(queryName).arg(index)));
        request.setHttpVersion(version);
        request.setSslConfig(QCNetworkSslConfig::insecureConfig());
        auto *reply = m_manager->get(request);
        if (!waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 15000)) {
            const QString error = reply->errorString();
            reply->cancel();
            reply->deleteLater();
            QFAIL(qPrintable(QStringLiteral("顺序请求 15s 内未完成：%1").arg(error)));
        }
        if (reply->error() != NetworkError::NoError) {
            const QString error = reply->errorString();
            reply->deleteLater();
            QFAIL(qPrintable(QStringLiteral("顺序请求失败：%1").arg(error)));
        }
        reply->deleteLater();
    }
    *elapsed = timer.elapsed();
}

void TestQCNetworkHttp2::testHttp2ConnectionReuse()
{
    // 顺序请求也应复用同一 session，但 streamId 仍需递增且不重复。

    int sessionId = -1;
    QSet<int> streamIds;

    for (int i = 0; i < 5; ++i) {
        QCNetworkRequest request(
            QUrl(m_h2BaseUrl + QStringLiteral("/reqinfo?seq=%1").arg(QString::number(i))));
        request.setHttpVersion(QCNetworkHttpVersion::Http2TLS);
        request.setSslConfig(QCNetworkSslConfig::insecureConfig());

        auto *reply = m_manager->get(request);
        if (!waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 15000)) {
            const QString err = reply->errorString();
            reply->cancel();
            reply->deleteLater();
            QFAIL(qPrintable(QStringLiteral("HTTP/2 连接复用测试 15s 内未完成：%1").arg(err)));
        }

        QVERIFY2(reply->error() == NetworkError::NoError, qPrintable(reply->errorString()));
        const auto data = reply->readAll();
        QVERIFY(data.has_value());

        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(*data, &parseError);
        QVERIFY2(parseError.error == QJsonParseError::NoError, qPrintable(parseError.errorString()));
        const QJsonObject obj = doc.object();
        QCOMPARE(obj.value(QStringLiteral("httpVersion")).toString(), QStringLiteral("2.0"));
        const int sid      = obj.value(QStringLiteral("sessionId")).toInt();
        const int streamId = obj.value(QStringLiteral("streamId")).toInt();
        QVERIFY(sid > 0);
        QVERIFY(streamId > 0);

        if (sessionId < 0) {
            sessionId = sid;
        }
        QCOMPARE(sid, sessionId);
        QVERIFY(!streamIds.contains(streamId));
        streamIds.insert(streamId);

        reply->deleteLater();
    }
}

void TestQCNetworkHttp2::testHttp2VsHttp1Performance()
{
    qint64 http1Elapsed = 0;
    qint64 http2Elapsed = 0;
    runSequentialRequests(m_http1BaseUrl,
                          QCNetworkHttpVersion::Http1_1,
                          QStringLiteral("http1"),
                          &http1Elapsed);
    runSequentialRequests(m_h2BaseUrl,
                          QCNetworkHttpVersion::Http2TLS,
                          QStringLiteral("http2"),
                          &http2Elapsed);
    qDebug() << "顺序请求耗时对比(ms): http1=" << http1Elapsed << "http2=" << http2Elapsed;
}

void TestQCNetworkHttp2::testSchedulerAdmissionAndStreamLimit_data()
{
    QTest::addColumn<int>("streamLimit");
    QTest::newRow("one-stream") << 1;
    QTest::newRow("two-streams") << 2;
}

void TestQCNetworkHttp2::testSchedulerAdmissionAndStreamLimit()
{
    QFETCH(int, streamLimit);
    // 使用新连接隔离各行的 HTTP/2 SETTINGS，不把复用旧连接的协商状态当作新配额。
    QVERIFY2(startLocalTestServer(), qPrintable(m_serverError));
    auto *pool          = QCNetworkConnectionPoolManager::instance();
    const auto original = pool->config();
    const auto restore  = qScopeGuard([&]() {
        QCOMPARE(pool->setConfig(original), QCNetworkConnectionPoolManager::UpdateResult::Applied);
    });
    QCNetworkConnectionPoolConfig config;
    config.setMultiMaxTotalConnections(1);
    config.setMultiMaxHostConnections(1);
    config.setMultiMaxConcurrentStreams(streamLimit);
    config.setMultiplexingEnabled(true);
    QCOMPARE(pool->setConfig(config), QCNetworkConnectionPoolManager::UpdateResult::Applied);
    QCNetworkAccessManager manager;
    manager.enableRequestScheduler(true);
    auto policy = manager.schedulerPolicy();
    policy.setMaxConcurrentRequests(3);
    policy.setMaxRequestsPerHost(3);
    QVERIFY(manager.setSchedulerPolicy(policy));
    QList<QCNetworkReply *> replies;
    for (int index = 0; index < 3; ++index) {
        QCNetworkRequest request(QUrl(m_localH2BaseUrl + QStringLiteral("/reqinfo?delay_ms=200")));
        request.setHttpVersion(QCNetworkHttpVersion::Http2TLS);
        request.setSslConfig(QCNetworkSslConfig::insecureConfig());
        replies.append(manager.get(request));
    }
    QCOMPARE(manager.schedulerStatistics().runningRequests(), 3);
    int peak = 0;
    QSet<int> sessions;
    for (auto *reply : replies) {
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 5000);
        QCOMPARE(reply->error(), NetworkError::NoError);
        const auto document = QJsonDocument::fromJson(reply->readAll().value_or(QByteArray()));
        QVERIFY(document.isObject());
        const auto object = document.object();
        QCOMPARE(object.value(QStringLiteral("httpVersion")).toString(), QStringLiteral("2.0"));
        peak = qMax(peak, object.value(QStringLiteral("activeStreams")).toInt());
        sessions.insert(object.value(QStringLiteral("sessionId")).toInt());
    }
    QCOMPARE(sessions.size(), 1);
    QVERIFY(!sessions.contains(0));
    QCOMPARE(peak, streamLimit);
}
