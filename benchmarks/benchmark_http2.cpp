/**
 * @file benchmark_http2.cpp
 * @brief HTTP/2 性能基准测试
 *
 * 对比 HTTP/1.1 和 HTTP/2 的性能差异：
 * - 单个请求延迟
 * - 并发请求性能（多路复用）
 * - 头部压缩效果
 */

#include <QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QThread>
#include "QCNetworkAccessManager.h"
#include "QCNetworkError.h"
#include "QCNetworkRequest.h"
#include "QCNetworkReply.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkSslConfig.h"

using namespace QCurl;

namespace {

struct BenchmarkEndpoint
{
    QString http1BaseUrl;
    QString http2BaseUrl;

    [[nodiscard]] QUrl urlFor(QCNetworkHttpVersion version) const
    {
        const QString base = version == QCNetworkHttpVersion::Http1_1 ? http1BaseUrl : http2BaseUrl;
        QUrl url(base + QStringLiteral("/reqinfo"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("payload"), QStringLiteral("benchmark"));
        url.setQuery(query);
        return url;
    }
};

struct ReplySummary
{
    bool finished = false;
    NetworkError error = NetworkError::NoError;
    int statusCode = 0;
    QString errorString;
    QString httpVersion;
    qsizetype bodySize = 0;
};

class LocalHttp2Server
{
public:
    ~LocalHttp2Server() { stop(); }

    bool start(QString *errorMessage)
    {
        stop();

        const QString path = scriptPath();
        if (!QFileInfo::exists(path)) {
            setError(errorMessage,
                     QStringLiteral("missing tests/qcurl/http2-test-server.js"));
            return false;
        }
        if (!startProcess(path, errorMessage)) {
            return false;
        }
        if (waitForReadyEndpoint(errorMessage)) {
            return true;
        }
        stop();
        return false;
    }

    void stop()
    {
        if (m_process.state() == QProcess::NotRunning) {
            return;
        }

        m_process.terminate();
        if (!m_process.waitForFinished(1500)) {
            m_process.kill();
            m_process.waitForFinished(1500);
        }
    }

    [[nodiscard]] BenchmarkEndpoint endpoint() const { return m_endpoint; }

private:
    static QString scriptPath()
    {
        return QDir(QCoreApplication::applicationDirPath())
            .absoluteFilePath(QStringLiteral("../../tests/qcurl/http2-test-server.js"));
    }

    bool startProcess(const QString &path, QString *errorMessage)
    {
        m_process.setProgram(QStringLiteral("node"));
        m_process.setArguments({path,
                                QStringLiteral("--h2-port"),
                                QStringLiteral("0"),
                                QStringLiteral("--http1-port"),
                                QStringLiteral("0")});
        m_process.setProcessChannelMode(QProcess::MergedChannels);
        m_process.start();
        if (!m_process.waitForStarted(2000)) {
            setError(errorMessage, QStringLiteral("failed to start node HTTP/2 fixture"));
            return false;
        }
        return true;
    }

    bool waitForReadyEndpoint(QString *errorMessage)
    {
        QByteArray output;
        QElapsedTimer timer;
        timer.start();
        const QRegularExpression re(QStringLiteral(R"(QCURL_HTTP2_TEST_SERVER_READY\s+(\{.*\})\s*)"));
        while (timer.elapsed() < 5000) {
            if (!m_process.waitForReadyRead(200)) {
                continue;
            }

            output += m_process.readAll();
            const QRegularExpressionMatch match = re.match(QString::fromUtf8(output));
            if (!match.hasMatch()) {
                continue;
            }

            if (applyReadyPayload(match.captured(1), errorMessage)) {
                return true;
            }
            return false;
        }

        const QString outputText = QString::fromUtf8(output + m_process.readAll()).right(4096);
        setError(errorMessage,
                 QStringLiteral("fixture did not become ready. output=%1").arg(outputText));
        stop();
        return false;
    }

    bool applyReadyPayload(const QString &payload, QString *errorMessage)
    {
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(payload.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            setError(errorMessage,
                     QStringLiteral("invalid fixture READY payload: %1")
                         .arg(parseError.errorString()));
            return false;
        }

        const QJsonObject obj = doc.object();
        const int h2Port = obj.value(QStringLiteral("h2Port")).toInt(0);
        const int http1Port = obj.value(QStringLiteral("http1Port")).toInt(0);
        if (h2Port <= 0 || http1Port <= 0) {
            setError(errorMessage, QStringLiteral("fixture returned invalid ports"));
            return false;
        }
        if (!waitForPortReady(static_cast<quint16>(h2Port), 3000)
            || !waitForPortReady(static_cast<quint16>(http1Port), 3000)) {
            setError(errorMessage, QStringLiteral("fixture ports are not reachable"));
            return false;
        }

        m_endpoint.http2BaseUrl = QStringLiteral("https://127.0.0.1:%1").arg(h2Port);
        m_endpoint.http1BaseUrl = QStringLiteral("https://127.0.0.1:%1").arg(http1Port);
        return true;
    }

    static void setError(QString *out, const QString &message)
    {
        if (out) {
            *out = message;
        }
    }

    static bool waitForPortReady(quint16 port, int timeoutMs)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            QTcpSocket probe;
            probe.connectToHost(QHostAddress::LocalHost, port);
            if (probe.waitForConnected(100)) {
                probe.disconnectFromHost();
                return true;
            }
            QThread::msleep(50);
        }
        return false;
    }

    QProcess m_process;
    BenchmarkEndpoint m_endpoint;
};

QString expectedHttpVersion(QCNetworkHttpVersion version)
{
    return version == QCNetworkHttpVersion::Http1_1 ? QStringLiteral("1.1")
                                                    : QStringLiteral("2.0");
}

} // namespace

class BenchmarkHttp2 : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // 基准测试
    void benchmarkHttp1Request();
    void benchmarkHttp2Request();
    void benchmarkConcurrentRequests();
    void benchmarkConcurrentRequests_data();

private:
    QCNetworkAccessManager *manager = nullptr;
    LocalHttp2Server localServer;
    BenchmarkEndpoint endpoint;

    ReplySummary waitForReply(QCNetworkReply *reply,
                              QCNetworkHttpVersion expectedVersion,
                              int timeout = 30000);
    QCNetworkRequest makeRequest(QCNetworkHttpVersion version) const;
};

void BenchmarkHttp2::initTestCase()
{
    QString errorMessage;
    QVERIFY2(localServer.start(&errorMessage), qPrintable(errorMessage));
    endpoint = localServer.endpoint();

    qDebug() << "HTTP/1.1 测试 URL:" << endpoint.http1BaseUrl;
    qDebug() << "HTTP/2 测试 URL:" << endpoint.http2BaseUrl;

    manager = new QCNetworkAccessManager(this);
}

void BenchmarkHttp2::cleanupTestCase()
{
    delete manager;
    localServer.stop();
}

QCNetworkRequest BenchmarkHttp2::makeRequest(QCNetworkHttpVersion version) const
{
    QCNetworkRequest request(endpoint.urlFor(version));
    request.setHttpVersion(version);
    request.setSslConfig(QCNetworkSslConfig::insecureConfig());
    return request;
}

ReplySummary BenchmarkHttp2::waitForReply(QCNetworkReply *reply,
                                           QCNetworkHttpVersion expectedVersion,
                                           int timeout)
{
    ReplySummary summary;
    if (!reply) {
        summary.errorString = QStringLiteral("reply is null");
        return summary;
    }

    QSignalSpy spy(reply, &QCNetworkReply::finished);
    summary.finished = reply->isFinished() || spy.wait(timeout);
    summary.error = reply->error();
    summary.statusCode = reply->httpStatusCode();
    summary.errorString = reply->errorString();

    const auto body = reply->readAll();
    if (body) {
        summary.bodySize = body->size();
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(*body, &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            summary.httpVersion = doc.object().value(QStringLiteral("httpVersion")).toString();
        }
    }

    if (summary.finished && summary.error == NetworkError::NoError && summary.statusCode == 200
        && summary.httpVersion == expectedHttpVersion(expectedVersion)) {
        return summary;
    }

    qWarning() << "HTTP benchmark request failed"
               << "finished" << summary.finished
               << "error" << static_cast<int>(summary.error)
               << "message" << summary.errorString
               << "status" << summary.statusCode
               << "httpVersion" << summary.httpVersion
               << "bodySize" << summary.bodySize;
    return summary;
}

void BenchmarkHttp2::benchmarkHttp1Request()
{
    QBENCHMARK {
        QCNetworkRequest request = makeRequest(QCNetworkHttpVersion::Http1_1);

        auto *reply = manager->get(request);
        const ReplySummary summary = waitForReply(reply, QCNetworkHttpVersion::Http1_1, 30000);
        QVERIFY(summary.finished);
        QCOMPARE(summary.error, NetworkError::NoError);
        QCOMPARE(summary.statusCode, 200);
        QCOMPARE(summary.httpVersion, QStringLiteral("1.1"));

        reply->deleteLater();
    }
}

void BenchmarkHttp2::benchmarkHttp2Request()
{
    QBENCHMARK {
        QCNetworkRequest request = makeRequest(QCNetworkHttpVersion::Http2);

        auto *reply = manager->get(request);
        const ReplySummary summary = waitForReply(reply, QCNetworkHttpVersion::Http2, 30000);
        QVERIFY(summary.finished);
        QCOMPARE(summary.error, NetworkError::NoError);
        QCOMPARE(summary.statusCode, 200);
        QCOMPARE(summary.httpVersion, QStringLiteral("2.0"));

        reply->deleteLater();
    }
}

void BenchmarkHttp2::benchmarkConcurrentRequests_data()
{
    QTest::addColumn<QCNetworkHttpVersion>("httpVersion");
    QTest::addColumn<int>("concurrency");

    QTest::newRow("HTTP/1.1 x5") << QCNetworkHttpVersion::Http1_1 << 5;
    QTest::newRow("HTTP/2 x5") << QCNetworkHttpVersion::Http2 << 5;
    QTest::newRow("HTTP/1.1 x10") << QCNetworkHttpVersion::Http1_1 << 10;
    QTest::newRow("HTTP/2 x10") << QCNetworkHttpVersion::Http2 << 10;
}

void BenchmarkHttp2::benchmarkConcurrentRequests()
{
    QFETCH(QCNetworkHttpVersion, httpVersion);
    QFETCH(int, concurrency);

    QBENCHMARK {
        QList<QCNetworkReply*> replies;

        // 发起并发请求
        for (int i = 0; i < concurrency; ++i) {
            QCNetworkRequest req = makeRequest(httpVersion);

            auto *reply = manager->get(req);
            replies.append(reply);
        }

        // 等待所有请求完成
        int completed = 0;
        int failed = 0;
        for (auto *reply : replies) {
            const ReplySummary summary = waitForReply(reply, httpVersion, 30000);
            if (summary.finished && summary.error == NetworkError::NoError
                && summary.statusCode == 200
                && summary.httpVersion == expectedHttpVersion(httpVersion)) {
                completed++;
            } else {
                failed++;
            }
            reply->deleteLater();
        }
        QCOMPARE(completed, concurrency);
        QCOMPARE(failed, 0);
    }
}

QTEST_MAIN(BenchmarkHttp2)
#include "benchmark_http2.moc"
