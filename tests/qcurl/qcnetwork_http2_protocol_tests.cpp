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

void TestQCNetworkHttp2::requestConcurrentJson(int requestCount,
                                               const QString &queryName,
                                               int delayMs,
                                               QList<QJsonObject> *responses)
{
    QList<QCNetworkReply *> replies;
    const auto cleanup = qScopeGuard([&replies] {
        for (auto *reply : replies) {
            if (!reply->isFinished()) {
                reply->cancel();
            }
            reply->deleteLater();
        }
    });
    for (int index = 0; index < requestCount; ++index) {
        const QUrl url(m_h2BaseUrl
                       + QStringLiteral("/reqinfo?%1=%2&delay_ms=%3")
                             .arg(queryName)
                             .arg(QString::number(index))
                             .arg(delayMs));
        QCNetworkRequest request(url);
        request.setHttpVersion(QCNetworkHttpVersion::Http2TLS);
        request.setSslConfig(QCNetworkSslConfig::insecureConfig());
        replies.append(m_manager->get(request));
    }
    for (int index = 0; index < replies.size(); ++index) {
        auto *reply = replies.at(index);
        if (!waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 30000)) {
            qWarning() << "HTTP/2 并发请求超时，序号：" << index;
            QFAIL("HTTP/2 并发流测试超时（单请求 30s 未完成）");
        }
        QVERIFY2(reply->error() == NetworkError::NoError, qPrintable(reply->errorString()));
        const auto data = reply->readAll();
        QVERIFY(data.has_value());
        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(*data, &parseError);
        QVERIFY2(parseError.error == QJsonParseError::NoError, qPrintable(parseError.errorString()));
        responses->append(document.object());
    }
}

void TestQCNetworkHttp2::verifySharedHttp2Session(const QList<QJsonObject> &responses)
{
    QSet<int> streamIds;
    int sessionId = -1;
    for (const auto &response : responses) {
        QCOMPARE(response.value(QStringLiteral("httpVersion")).toString(), QStringLiteral("2.0"));
        const int currentSession = response.value(QStringLiteral("sessionId")).toInt();
        const int streamId       = response.value(QStringLiteral("streamId")).toInt();
        QVERIFY(currentSession > 0);
        QVERIFY(streamId > 0);
        if (sessionId < 0) {
            sessionId = currentSession;
        }
        QCOMPARE(currentSession, sessionId);
        QVERIFY(!streamIds.contains(streamId));
        streamIds.insert(streamId);
    }
}

void TestQCNetworkHttp2::testHttp2Support()
{
    // 枚举层和 runtime 能力都必须暴露 HTTP/2。
    QVERIFY(static_cast<int>(QCNetworkHttpVersion::Http2) > 0);
    QVERIFY(static_cast<int>(QCNetworkHttpVersion::Http2TLS) > 0);
    QVERIFY(checkHttp2Support());
}

void TestQCNetworkHttp2::testHttp2Negotiation()
{
    const QUrl url(m_h2BaseUrl + QStringLiteral("/reqinfo?case=negotiation"));
    const QJsonObject obj = requestJson(url, QCNetworkHttpVersion::Http2TLS);
    QVERIFY2(!obj.contains(QString::fromUtf8(kJsonErrorKey)),
             qPrintable(obj.value(QString::fromUtf8(kJsonErrorKey)).toString()));
    QCOMPARE(obj.value(QStringLiteral("httpVersion")).toString(), QStringLiteral("2.0"));
    QVERIFY(obj.value(QStringLiteral("sessionId")).toInt() > 0);
    QVERIFY(obj.value(QStringLiteral("streamId")).toInt() > 0);
}

void TestQCNetworkHttp2::testHttp2Multiplexing()
{
    // 同一 server 下的多请求应共享一个 session，并分配不同 streamId。
    QList<QJsonObject> responses;
    requestConcurrentJson(5, QStringLiteral("id"), 150, &responses);
    QCOMPARE(responses.size(), 5);
    verifySharedHttp2Session(responses);
}

void TestQCNetworkHttp2::testHttp2HeaderCompression()
{
    // 构造高 header 密度请求，验证服务端仍能正确回显。
    QCNetworkRequest request(QUrl(m_h2BaseUrl + QStringLiteral("/reqinfo?case=headers")));
    request.setHttpVersion(QCNetworkHttpVersion::Http2TLS);
    request.setSslConfig(QCNetworkSslConfig::insecureConfig());

    for (int i = 0; i < 10; ++i) {
        request.setRawHeader(QStringLiteral("X-Custom-Header-%1").arg(i).toUtf8(),
                             QStringLiteral("Value-%1-With-Long-Content-For-Compression-Test")
                                 .arg(i)
                                 .toUtf8());
    }

    auto *reply = m_manager->get(request);
    if (!waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 15000)) {
        const QString err = reply->errorString();
        reply->cancel();
        reply->deleteLater();
        QFAIL(qPrintable(QStringLiteral("HTTP/2 头部压缩测试 15s 内未完成：%1").arg(err)));
    }

    if (reply->error() != NetworkError::NoError) {
        const QString err = reply->errorString();
        reply->deleteLater();
        QFAIL(qPrintable(QStringLiteral("HTTP/2 头部压缩测试失败：%1").arg(err)));
    }

    QCOMPARE(reply->error(), NetworkError::NoError);

    const auto data = reply->readAll();
    QVERIFY(data.has_value());

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(*data, &parseError);
    QVERIFY2(parseError.error == QJsonParseError::NoError, qPrintable(parseError.errorString()));
    const QJsonObject obj = doc.object();
    QCOMPARE(obj.value(QStringLiteral("httpVersion")).toString(), QStringLiteral("2.0"));

    const QJsonObject echoed = obj.value(QStringLiteral("headers")).toObject();
    for (int i = 0; i < 10; ++i) {
        const QString k = QStringLiteral("x-custom-header-%1").arg(QString::number(i));
        QVERIFY2(echoed.contains(k), qPrintable(QStringLiteral("未回显 header: %1").arg(k)));
    }

    reply->deleteLater();
}

void TestQCNetworkHttp2::testHttp2Downgrade()
{
    QVERIFY2(!m_disableDowngradeTest,
             "QCURL_HTTP2_DISABLE_DOWNGRADE_TEST 已设置；请在 gate/planner "
             "侧显式排除降级用例，而不是依赖 suite 内跳过。");

    QVERIFY2(
        !m_http1BaseUrl.isEmpty(),
        "HTTP/1.1 base URL 为空：请设置 QCURL_HTTP2_TEST_HTTP1_BASE_URL 或使用默认本地 server。");

    const QUrl url(m_http1BaseUrl + QStringLiteral("/reqinfo?case=downgrade"));
    const QJsonObject obj = requestJson(url, QCNetworkHttpVersion::Http2TLS);
    QVERIFY2(!obj.contains(QString::fromUtf8(kJsonErrorKey)),
             qPrintable(obj.value(QString::fromUtf8(kJsonErrorKey)).toString()));
    QCOMPARE(obj.value(QStringLiteral("httpVersion")).toString(), QStringLiteral("1.1"));
}

void TestQCNetworkHttp2::testHttp2WithSsl()
{
    const QUrl url(m_h2BaseUrl + QStringLiteral("/reqinfo?case=h2tls"));
    const QJsonObject obj = requestJson(url, QCNetworkHttpVersion::Http2TLS);
    QVERIFY2(!obj.contains(QString::fromUtf8(kJsonErrorKey)),
             qPrintable(obj.value(QString::fromUtf8(kJsonErrorKey)).toString()));
    QCOMPARE(obj.value(QStringLiteral("httpVersion")).toString(), QStringLiteral("2.0"));
}

void TestQCNetworkHttp2::testHttp2ConcurrentStreams()
{
    // 共享同一 session 的多请求应获得不同的 streamId。
    QElapsedTimer timer;
    timer.start();
    QList<QJsonObject> responses;
    requestConcurrentJson(10, QStringLiteral("stream"), 200, &responses);
    QCOMPARE(responses.size(), 10);
    verifySharedHttp2Session(responses);
    qDebug() << "总耗时：" << timer.elapsed() << "ms";
}
