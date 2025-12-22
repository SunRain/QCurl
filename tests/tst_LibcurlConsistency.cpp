/**
 * @file tst_LibcurlConsistency.cpp
 * @brief QCurl ↔ libcurl 一致性测试（QCurl 侧执行器）
 *
 * 设计目标：在 pytest 驱动下按“用例/协议/端口”等参数执行单个场景，
 * 并将可观测输出写入当前工作目录（download_*.data），供 Python 侧生成 artifacts。
 *
 * 约定环境变量（由 pytest 注入）：
 * - QCURL_LC_CASE_ID: 用例标识（与 Python case_defs 对齐）
 * - QCURL_LC_PROTO: http/1.1 | h2 | h3
 * - QCURL_LC_HTTPS_PORT: https 端口（同号 TCP+UDP，h3 使用 UDP）
 * - QCURL_LC_WS_PORT: ws echo server 端口
 * - QCURL_LC_COUNT: 下载/上传次数（默认 1）
 * - QCURL_LC_DOCNAME: 下载资源名（如 data-1m）
 * - QCURL_LC_UPLOAD_SIZE: 上传字节数（默认 0）
 * - QCURL_LC_ABORT_OFFSET: 中断点（Range 续传用，默认 0）
 * - QCURL_LC_FILE_SIZE: 资源总长度（Range 续传用，默认 0）
 */

#include <QtTest/QtTest>

#include <QByteArray>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVector>

#include "QCNetworkAccessManager.h"
#include "QCNetworkError.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkSslConfig.h"
#include "QCNetworkTimeoutConfig.h"
#include "QCWebSocket.h"
#include "QCWebSocketCompressionConfig.h"

using namespace QCurl;

namespace {

QCNetworkHttpVersion toHttpVersion(const QString &proto)
{
    if (proto == QStringLiteral("http/1.1")) {
        return QCNetworkHttpVersion::Http1_1;
    }
    if (proto == QStringLiteral("h2")) {
        return QCNetworkHttpVersion::Http2;
    }
    if (proto == QStringLiteral("h3")) {
        return QCNetworkHttpVersion::Http3Only;
    }
    return QCNetworkHttpVersion::HttpAny;
}

QByteArray makeUploadBody(int size)
{
    return QByteArray(size, 'x');
}

QUrl withRequestId(const QUrl &url, const QString &requestId)
{
    if (requestId.isEmpty()) {
        return url;
    }
    QUrl out(url);
    QUrlQuery query(out);
    query.addQueryItem(QStringLiteral("id"), requestId);
    out.setQuery(query);
    return out;
}

bool writeWsEventsToFile(const QString &filePath, const QVector<QPair<QByteArray, QByteArray>> &events)
{
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    for (const auto &e : events) {
        const QByteArray &name = e.first;
        const QByteArray &payload = e.second;
        QByteArray line;
        line.reserve(name.size() + 1 + 20 + 1 + (payload.size() * 2) + 1);
        line.append(name);
        line.append(' ');
        line.append(QByteArray::number(payload.size()));
        line.append(' ');
        line.append(payload.toHex());
        line.append('\n');
        if (f.write(line) != line.size()) {
            f.close();
            return false;
        }
    }
    f.close();
    return true;
}

bool writeAllToFile(const QString &filePath, const QByteArray &data, QIODevice::OpenMode mode)
{
    QFile f(filePath);
    if (!f.open(mode)) {
        return false;
    }
    const qint64 written = f.write(data);
    f.close();
    return written == data.size();
}

struct ProgressSummary {
    qint64 nowMax = 0;
    qint64 totalMax = 0;
    qint64 prevNow = -1;
    bool monotonic = true;
    int eventsCount = 0;

    void update(qint64 now, qint64 total)
    {
        if (prevNow >= 0 && now < prevNow) {
            monotonic = false;
        }
        prevNow = now;
        nowMax = qMax(nowMax, now);
        totalMax = qMax(totalMax, total);
        ++eventsCount;
    }

    QJsonObject toJson() const
    {
        QJsonObject o;
        o.insert(QStringLiteral("monotonic"), monotonic);
        o.insert(QStringLiteral("now_max"), static_cast<qint64>(nowMax));
        o.insert(QStringLiteral("total_max"), static_cast<qint64>(totalMax));
        o.insert(QStringLiteral("events_count"), eventsCount);
        return o;
    }
};

bool writeJsonObjectToFile(const QString &filePath, const QJsonObject &obj)
{
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    const qint64 written = f.write(data);
    f.close();
    return written == data.size();
}

NetworkError httpGetToFile(QCNetworkAccessManager &manager,
                           const QUrl &url,
                           QCNetworkHttpVersion httpVersion,
                           const QString &filePath,
                           qint64 abortAt,
                           qint64 *outBytesWritten)
{
    QCNetworkRequest req(url);
    req.setSslConfig(QCNetworkSslConfig::insecureConfig());
    req.setHttpVersion(httpVersion);

    QFile out(filePath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return NetworkError::Unknown;
    }

    qint64 written = 0;
    QCNetworkReply reply(req, HttpMethod::Get, ExecutionMode::Sync, QByteArray(), &manager);
    reply.setWriteCallback([&](char *buffer, size_t size) -> size_t {
        if (abortAt > 0 && written >= abortAt) {
            return 0;  // 触发 CURLE_WRITE_ERROR -> 中断
        }
        const qint64 want = static_cast<qint64>(size);
        qint64 canWrite = want;
        if (abortAt > 0 && (written + want) > abortAt) {
            canWrite = abortAt - written;
        }
        if (canWrite > 0) {
            const qint64 w = out.write(buffer, canWrite);
            if (w != canWrite) {
                return 0;
            }
            written += w;
        }
        if (abortAt > 0 && written >= abortAt) {
            return 0;
        }
        return size;
    });
    reply.execute();
    out.close();

    if (outBytesWritten) {
        *outBytesWritten = written;
    }
    return reply.error();
}

NetworkError httpRangeToFile(QCNetworkAccessManager &manager,
                             const QUrl &url,
                             QCNetworkHttpVersion httpVersion,
                             const QString &filePath,
                             int rangeStart,
                             int rangeEnd)
{
    QCNetworkRequest req(url);
    req.setSslConfig(QCNetworkSslConfig::insecureConfig());
    req.setHttpVersion(httpVersion);
    req.setRange(rangeStart, rangeEnd);

    QFile out(filePath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Append)) {
        return NetworkError::Unknown;
    }

    QCNetworkReply reply(req, HttpMethod::Get, ExecutionMode::Sync, QByteArray(), &manager);
    reply.setWriteCallback([&](char *buffer, size_t size) -> size_t {
        const qint64 want = static_cast<qint64>(size);
        const qint64 w = out.write(buffer, want);
        if (w != want) {
            return 0;
        }
        return size;
    });
    reply.execute();
    out.close();
    return reply.error();
}

NetworkError httpMethodToFile(QCNetworkAccessManager &manager,
                              HttpMethod method,
                              const QUrl &url,
                              QCNetworkHttpVersion httpVersion,
                              const QByteArray &body,
                              const QString &filePath)
{
    QCNetworkRequest req(url);
    req.setSslConfig(QCNetworkSslConfig::insecureConfig());
    req.setHttpVersion(httpVersion);

    QFile out(filePath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return NetworkError::Unknown;
    }

    QCNetworkReply reply(req, method, ExecutionMode::Sync, body, &manager);
    reply.setWriteCallback([&](char *buffer, size_t size) -> size_t {
        const qint64 want = static_cast<qint64>(size);
        const qint64 w = out.write(buffer, want);
        if (w != want) {
            return 0;
        }
        return size;
    });
    reply.execute();
    out.close();
    return reply.error();
}

}  // namespace

class TestLibcurlConsistency : public QObject
{
    Q_OBJECT

private slots:
    void testCase();
};

void TestLibcurlConsistency::testCase()
{
    const QString caseId = qEnvironmentVariable("QCURL_LC_CASE_ID");
    if (caseId.isEmpty()) {
        QSKIP("QCURL_LC_CASE_ID 未设置");
    }

    const QString proto = qEnvironmentVariable("QCURL_LC_PROTO", "h2");
    const int httpsPort = qEnvironmentVariableIntValue("QCURL_LC_HTTPS_PORT");
    const int wsPort = qEnvironmentVariableIntValue("QCURL_LC_WS_PORT");
    const int count = qMax(1, qEnvironmentVariableIntValue("QCURL_LC_COUNT"));
    const QString docname = qEnvironmentVariable("QCURL_LC_DOCNAME");
    const int uploadSize = qMax(0, qEnvironmentVariableIntValue("QCURL_LC_UPLOAD_SIZE"));
    const int abortOffset = qMax(0, qEnvironmentVariableIntValue("QCURL_LC_ABORT_OFFSET"));
    const int fileSize = qMax(0, qEnvironmentVariableIntValue("QCURL_LC_FILE_SIZE"));
    const QString requestId = qEnvironmentVariable("QCURL_LC_REQ_ID");
    const int httpPort = qEnvironmentVariableIntValue("QCURL_LC_HTTP_PORT");
    const QString cookiePath = qEnvironmentVariable("QCURL_LC_COOKIE_PATH");
    const int proxyPort = qEnvironmentVariableIntValue("QCURL_LC_PROXY_PORT");
    const QString proxyUser = qEnvironmentVariable("QCURL_LC_PROXY_USER");
    const QString proxyPass = qEnvironmentVariable("QCURL_LC_PROXY_PASS");
    const QString proxyTargetUrl = qEnvironmentVariable("QCURL_LC_PROXY_TARGET_URL");
    const int observeHttpPort = qEnvironmentVariableIntValue("QCURL_LC_OBSERVE_HTTP_PORT");
    const int observeStatusCode = qEnvironmentVariableIntValue("QCURL_LC_STATUS_CODE");
    const int observeHttpsPort = qEnvironmentVariableIntValue("QCURL_LC_OBSERVE_HTTPS_PORT");
    const QString caCertPath = qEnvironmentVariable("QCURL_LC_CA_CERT_PATH");
    const QString targetUrl = qEnvironmentVariable("QCURL_LC_TARGET_URL");

    const QString outDir = qEnvironmentVariable("QCURL_LC_OUT_DIR");
    if (!outDir.isEmpty()) {
        QVERIFY(QDir().mkpath(outDir));
        QVERIFY(QDir::setCurrent(outDir));
    }

    QCNetworkAccessManager manager;
    const QCNetworkHttpVersion httpVersion = toHttpVersion(proto);

    if (caseId == QStringLiteral("p2_tls_verify_success")) {
        QVERIFY(observeHttpsPort > 0);
        QVERIFY(!caCertPath.isEmpty());

        QCNetworkSslConfig ssl = QCNetworkSslConfig::defaultConfig();
        ssl.caCertPath = caCertPath;

        QCNetworkRequest req(withRequestId(
            QUrl(QStringLiteral("https://localhost:%1/cookie").arg(observeHttpsPort)),
            requestId));
        req.setSslConfig(ssl);
        req.setHttpVersion(httpVersion);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), NetworkError::NoError);
        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"), *dataOpt, QIODevice::WriteOnly | QIODevice::Truncate));
        delete reply;
        return;
    }

    if (caseId == QStringLiteral("p2_tls_verify_fail_no_ca")) {
        QVERIFY(observeHttpsPort > 0);

        QCNetworkSslConfig ssl = QCNetworkSslConfig::defaultConfig();

        QCNetworkRequest req(withRequestId(
            QUrl(QStringLiteral("https://localhost:%1/cookie").arg(observeHttpsPort)),
            requestId));
        req.setSslConfig(ssl);
        req.setHttpVersion(httpVersion);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), NetworkError::SslHandshakeFailed);
        delete reply;
        return;
    }

    if (caseId == QStringLiteral("p2_error_refused")) {
        QVERIFY(!targetUrl.isEmpty());

        QCNetworkRequest req{QUrl(targetUrl)};
        req.setHttpVersion(httpVersion);
        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), NetworkError::ConnectionRefused);
        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"), *dataOpt, QIODevice::WriteOnly | QIODevice::Truncate));
        delete reply;
        return;
    }

    if (caseId == QStringLiteral("p2_error_malformat")) {
        QVERIFY(!targetUrl.isEmpty());

        QCNetworkRequest req{QUrl(targetUrl)};
        req.setHttpVersion(httpVersion);
        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), NetworkError::InvalidRequest);
        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"), *dataOpt, QIODevice::WriteOnly | QIODevice::Truncate));
        delete reply;
        return;
    }

    if (caseId == QStringLiteral("p1_redirect_nofollow") ||
        caseId == QStringLiteral("p1_redirect_follow")) {
        QVERIFY(observeHttpPort > 0);
        const bool follow = (caseId == QStringLiteral("p1_redirect_follow"));

        QCNetworkRequest req(withRequestId(
            QUrl(QStringLiteral("http://localhost:%1/redir/3").arg(observeHttpPort)),
            requestId));
        req.setHttpVersion(httpVersion);
        req.setFollowLocation(follow);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), NetworkError::NoError);

        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"), *dataOpt, QIODevice::WriteOnly | QIODevice::Truncate));
        delete reply;
        return;
    }

    if (caseId == QStringLiteral("p1_empty_body_200") ||
        caseId == QStringLiteral("p1_empty_body_204")) {
        QVERIFY(observeHttpPort > 0);

        const QString path = (caseId == QStringLiteral("p1_empty_body_200"))
            ? QStringLiteral("/empty_200")
            : QStringLiteral("/no_content");

        QCNetworkRequest req(withRequestId(
            QUrl(QStringLiteral("http://localhost:%1%2").arg(observeHttpPort).arg(path)),
            requestId));
        req.setHttpVersion(httpVersion);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), NetworkError::NoError);

        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"), *dataOpt, QIODevice::WriteOnly | QIODevice::Truncate));
        delete reply;
        return;
    }

    if (caseId == QStringLiteral("p1_resp_headers")) {
        QVERIFY(observeHttpPort > 0);

        QCNetworkRequest req(withRequestId(
            QUrl(QStringLiteral("http://localhost:%1/resp_headers").arg(observeHttpPort)),
            requestId));
        req.setHttpVersion(httpVersion);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), NetworkError::NoError);

        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"), *dataOpt, QIODevice::WriteOnly | QIODevice::Truncate));

        const QByteArray headerData = reply->rawHeaderData();
        QVERIFY(writeAllToFile(QStringLiteral("response_headers_0.data"), headerData, QIODevice::WriteOnly | QIODevice::Truncate));

        delete reply;
        return;
    }

    if (caseId == QStringLiteral("p1_progress_download")) {
        QVERIFY(httpsPort > 0);
        QVERIFY(!docname.isEmpty());

        const QUrl url = withRequestId(
            QUrl(QStringLiteral("https://localhost:%1/%2").arg(httpsPort).arg(docname)),
            requestId);

        QCNetworkRequest req(url);
        req.setSslConfig(QCNetworkSslConfig::insecureConfig());
        req.setHttpVersion(httpVersion);

        QCNetworkReply *reply = manager.sendGet(req);
        QVERIFY(reply);

        ProgressSummary dl;
        ProgressSummary ul;

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        timer.start(60000);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

        connect(reply, &QCNetworkReply::downloadProgress, this, [&](qint64 now, qint64 total) {
            dl.update(now, total);
        });
        connect(reply, &QCNetworkReply::uploadProgress, this, [&](qint64 now, qint64 total) {
            ul.update(now, total);
        });
        connect(reply, &QCNetworkReply::finished, this, [&]() { loop.quit(); });

        loop.exec();
        QVERIFY2(timer.isActive(), "timeout waiting for download progress case");
        QCOMPARE(reply->error(), NetworkError::NoError);

        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        const QByteArray data = *dataOpt;
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"), data, QIODevice::WriteOnly | QIODevice::Truncate));

        QVERIFY(dl.monotonic);
        QCOMPARE(dl.nowMax, data.size());
        QCOMPARE(dl.totalMax, data.size());

        QJsonObject root;
        root.insert(QStringLiteral("download"), dl.toJson());
        root.insert(QStringLiteral("upload"), ul.toJson());
        QVERIFY(writeJsonObjectToFile(QStringLiteral("progress_summary.json"), root));

        reply->deleteLater();
        return;
    }

    if (caseId == QStringLiteral("p1_progress_upload")) {
        QVERIFY(httpsPort > 0);
        QVERIFY(uploadSize > 0);

        const QUrl url = withRequestId(
            QUrl(QStringLiteral("https://localhost:%1/curltest/echo").arg(httpsPort)),
            requestId);
        const QByteArray body = makeUploadBody(uploadSize);

        QCNetworkRequest req(url);
        req.setSslConfig(QCNetworkSslConfig::insecureConfig());
        req.setHttpVersion(httpVersion);

        QCNetworkReply *reply = manager.sendPost(req, body);
        QVERIFY(reply);

        ProgressSummary dl;
        ProgressSummary ul;

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        timer.start(60000);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

        connect(reply, &QCNetworkReply::downloadProgress, this, [&](qint64 now, qint64 total) {
            dl.update(now, total);
        });
        connect(reply, &QCNetworkReply::uploadProgress, this, [&](qint64 now, qint64 total) {
            ul.update(now, total);
        });
        connect(reply, &QCNetworkReply::finished, this, [&]() { loop.quit(); });

        loop.exec();
        QVERIFY2(timer.isActive(), "timeout waiting for upload progress case");
        QCOMPARE(reply->error(), NetworkError::NoError);

        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        const QByteArray data = *dataOpt;
        QCOMPARE(data, body);
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"), data, QIODevice::WriteOnly | QIODevice::Truncate));

        QVERIFY(ul.monotonic);
        QCOMPARE(ul.nowMax, body.size());
        QCOMPARE(ul.totalMax, body.size());

        QJsonObject root;
        root.insert(QStringLiteral("download"), dl.toJson());
        root.insert(QStringLiteral("upload"), ul.toJson());
        QVERIFY(writeJsonObjectToFile(QStringLiteral("progress_summary.json"), root));

        reply->deleteLater();
        return;
    }

    if (caseId == QStringLiteral("p1_method_head")) {
        QVERIFY(observeHttpPort > 0);

        const QUrl url = withRequestId(
            QUrl(QStringLiteral("http://localhost:%1/head").arg(observeHttpPort)),
            requestId);

        QCNetworkRequest req(url);
        req.setHttpVersion(httpVersion);

        QCNetworkReply *reply = manager.sendHead(req);
        QVERIFY(reply);

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        timer.start(20000);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        connect(reply, &QCNetworkReply::finished, &loop, &QEventLoop::quit);

        loop.exec();
        QVERIFY2(timer.isActive(), "timeout waiting for head request");
        QCOMPARE(reply->error(), NetworkError::NoError);

        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(dataOpt->isEmpty());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"), *dataOpt, QIODevice::WriteOnly | QIODevice::Truncate));

        reply->deleteLater();
        return;
    }

    if (caseId == QStringLiteral("p1_method_patch")) {
        QVERIFY(observeHttpPort > 0);
        QVERIFY(uploadSize > 0);

        const QUrl url = withRequestId(
            QUrl(QStringLiteral("http://localhost:%1/method").arg(observeHttpPort)),
            requestId);
        const QByteArray body = makeUploadBody(uploadSize);

        QCNetworkRequest req(url);
        req.setHttpVersion(httpVersion);

        QCNetworkReply *reply = manager.sendPatch(req, body);
        QVERIFY(reply);

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        timer.start(20000);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        connect(reply, &QCNetworkReply::finished, &loop, &QEventLoop::quit);

        loop.exec();
        QVERIFY2(timer.isActive(), "timeout waiting for patch request");
        QCOMPARE(reply->error(), NetworkError::NoError);

        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QCOMPARE(*dataOpt, body);
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"), *dataOpt, QIODevice::WriteOnly | QIODevice::Truncate));

        reply->deleteLater();
        return;
    }

    if (caseId == QStringLiteral("p1_timeout_delay_headers")) {
        QVERIFY(observeHttpPort > 0);

        QCNetworkTimeoutConfig timeout;
        timeout.totalTimeout = std::chrono::milliseconds(200);

        QCNetworkRequest req(withRequestId(
            QUrl(QStringLiteral("http://localhost:%1/delay_headers/1000").arg(observeHttpPort)),
            requestId));
        req.setHttpVersion(httpVersion);
        req.setTimeoutConfig(timeout);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), NetworkError::ConnectionTimeout);

        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"), *dataOpt, QIODevice::WriteOnly | QIODevice::Truncate));
        delete reply;
        return;
    }

    if (caseId == QStringLiteral("p1_timeout_low_speed")) {
        QVERIFY(observeHttpPort > 0);

        QCNetworkTimeoutConfig timeout;
        timeout.lowSpeedTime = std::chrono::seconds(2);
        timeout.lowSpeedLimit = 1024;

        QCNetworkRequest req(withRequestId(
            QUrl(QStringLiteral("http://localhost:%1/stall_body/8192/5000").arg(observeHttpPort)),
            requestId));
        req.setHttpVersion(httpVersion);
        req.setTimeoutConfig(timeout);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), NetworkError::ConnectionTimeout);

        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"), *dataOpt, QIODevice::WriteOnly | QIODevice::Truncate));
        delete reply;
        return;
    }

    if (caseId == QStringLiteral("p1_cancel_after_first_chunk")) {
        QVERIFY(observeHttpPort > 0);

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        timer.start(20000);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

        const qint64 cancelAtBytes = 4096;

        QCNetworkRequest req(withRequestId(
            QUrl(QStringLiteral("http://localhost:%1/slow_body/8192/4096/5000").arg(observeHttpPort)),
            requestId));
        req.setHttpVersion(httpVersion);

        QCNetworkReply *reply = manager.sendGet(req);
        QVERIFY(reply);

        QByteArray received;
        bool cancelRequested = false;
        bool cancelledEmitted = false;
        bool finishedEmitted = false;
        int postCancelReadyRead = 0;
        int postCancelProgress = 0;

        connect(reply, &QCNetworkReply::readyRead, this, [&, reply]() {
            if (cancelRequested) {
                ++postCancelReadyRead;
            }
            const auto dataOpt = reply->readAll();
            if (dataOpt.has_value() && !dataOpt->isEmpty()) {
                received.append(*dataOpt);
            }
        });

        connect(reply, &QCNetworkReply::downloadProgress, this, [&, reply](qint64 bytesReceived, qint64 /*bytesTotal*/) {
            if (!cancelRequested && bytesReceived >= cancelAtBytes) {
                cancelRequested = true;
                reply->cancel();
                return;
            }
            if (cancelRequested) {
                ++postCancelProgress;
            }
        });

        connect(reply, &QCNetworkReply::cancelled, this, [&, reply]() {
            cancelledEmitted = true;
            loop.quit();
        });

        connect(reply, &QCNetworkReply::finished, this, [&, reply]() {
            finishedEmitted = true;
            loop.quit();
        });

        loop.exec();
        QVERIFY2(timer.isActive(), "timeout waiting for cancellation");
        QVERIFY(cancelledEmitted);
        QVERIFY(!finishedEmitted);
        QCOMPARE(reply->error(), NetworkError::OperationCancelled);
        QVERIFY(postCancelReadyRead == 0);
        QVERIFY(postCancelProgress == 0);

        // 兜底：取消后仍可能有少量 buffer 未消费（按当前 readAll 语义应返回 empty/残留字节）
        const auto tailOpt = reply->readAll();
        if (tailOpt.has_value() && !tailOpt->isEmpty()) {
            received.append(*tailOpt);
        }

        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"), received, QIODevice::WriteOnly | QIODevice::Truncate));
        reply->deleteLater();
        return;
    }

    if (caseId == QStringLiteral("p1_login_cookie_flow")) {
        QVERIFY(observeHttpPort > 0);
        QVERIFY(!cookiePath.isEmpty());

        manager.setCookieFilePath(cookiePath, QCNetworkAccessManager::ReadWrite);
        QCNetworkRequest req(withRequestId(
            QUrl(QStringLiteral("http://localhost:%1/login").arg(observeHttpPort)),
            requestId));
        req.setHttpVersion(httpVersion);
        req.setFollowLocation(true);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), NetworkError::NoError);
        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"), *dataOpt, QIODevice::WriteOnly | QIODevice::Truncate));
        delete reply;
        return;
    }

    if (caseId == QStringLiteral("p2_cookie_request_header")) {
        QVERIFY(observeHttpPort > 0);
        QVERIFY(!cookiePath.isEmpty());

        manager.setCookieFilePath(cookiePath, QCNetworkAccessManager::ReadOnly);
        const QUrl url = withRequestId(
            QUrl(QStringLiteral("http://localhost:%1/cookie").arg(observeHttpPort)),
            requestId);

        QCNetworkRequest req(url);
        req.setHttpVersion(httpVersion);
        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), NetworkError::NoError);
        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"), *dataOpt, QIODevice::WriteOnly | QIODevice::Truncate));
        delete reply;
        return;
    }

    if (caseId == QStringLiteral("p2_fixed_http_error")) {
        QVERIFY(observeHttpPort > 0);
        QVERIFY(observeStatusCode > 0);

        const QUrl url = withRequestId(
            QUrl(QStringLiteral("http://localhost:%1/status/%2").arg(observeHttpPort).arg(observeStatusCode)),
            requestId);

        QCNetworkRequest req(url);
        req.setHttpVersion(httpVersion);
        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QVERIFY(reply->error() != NetworkError::NoError);
        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"), *dataOpt, QIODevice::WriteOnly | QIODevice::Truncate));
        delete reply;
        return;
    }

    if (caseId == QStringLiteral("p2_error_proxy_407")) {
        QVERIFY(proxyPort > 0);
        QVERIFY(!proxyTargetUrl.isEmpty());

        QCNetworkRequest req{QUrl(proxyTargetUrl)};
        req.setHttpVersion(httpVersion);

        QCNetworkProxyConfig proxy;
        proxy.type = QCNetworkProxyConfig::ProxyType::Http;
        proxy.hostName = QStringLiteral("localhost");
        proxy.port = proxyPort;
        // 不提供凭据：触发 407（可观测一致性用例）
        proxy.userName = QString();
        proxy.password = QString();
        req.setProxyConfig(proxy);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QVERIFY(reply->error() != NetworkError::NoError);
        QCOMPARE(static_cast<int>(reply->error()), 407);
        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"), *dataOpt, QIODevice::WriteOnly | QIODevice::Truncate));
        delete reply;
        return;
    }

    if (caseId == QStringLiteral("proxy_http_basic_auth") ||
        caseId == QStringLiteral("proxy_https_connect_basic_auth")) {
        QVERIFY(proxyPort > 0);
        QVERIFY(!proxyUser.isEmpty());
        QVERIFY(!proxyPass.isEmpty());
        QVERIFY(!proxyTargetUrl.isEmpty());

        QCNetworkRequest req{QUrl(proxyTargetUrl)};
        req.setSslConfig(QCNetworkSslConfig::insecureConfig());
        req.setHttpVersion(httpVersion);

        QCNetworkProxyConfig proxy;
        proxy.type = QCNetworkProxyConfig::ProxyType::Http;
        proxy.hostName = QStringLiteral("localhost");
        proxy.port = proxyPort;
        proxy.userName = proxyUser;
        proxy.password = proxyPass;
        req.setProxyConfig(proxy);

        const QString outFile = QStringLiteral("download_0.data");
        QFile out(outFile);
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));

        QCNetworkReply reply(req, HttpMethod::Get, ExecutionMode::Sync, QByteArray(), &manager);
        reply.setWriteCallback([&](char *buffer, size_t size) -> size_t {
            const qint64 want = static_cast<qint64>(size);
            const qint64 w = out.write(buffer, want);
            if (w != want) {
                return 0;
            }
            return size;
        });
        reply.execute();
        out.close();

        QCOMPARE(reply.error(), NetworkError::NoError);
        QFile f(outFile);
        QVERIFY(f.exists());
        QVERIFY(f.size() > 0);
        return;
    }

    if (caseId == QStringLiteral("download_serial_resume") ||
        caseId == QStringLiteral("download_parallel_resume")) {
        QVERIFY(!docname.isEmpty());
        for (int i = 0; i < count; ++i) {
            const QUrl url = withRequestId(
                QUrl(QStringLiteral("https://localhost:%1/%2").arg(httpsPort).arg(docname)),
                requestId);
            const QString outFile = QStringLiteral("download_%1.data").arg(i);
            const NetworkError err = httpMethodToFile(manager, HttpMethod::Get, url, httpVersion, QByteArray(), outFile);
            QCOMPARE(err, NetworkError::NoError);
        }
        return;
    }

    if (caseId == QStringLiteral("ext_multi_get4_h2") ||
        caseId == QStringLiteral("ext_multi_get4_h3")) {
        QVERIFY(!docname.isEmpty());
        QVERIFY(httpsPort > 0);

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        timer.start(60000);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

        QVector<NetworkError> errors;
        errors.resize(count);
        int remaining = count;

        for (int i = 0; i < count; ++i) {
            const QString suffix = QStringLiteral("%1").arg(i + 1, 4, 10, QLatin1Char('0'));
            const QUrl url = withRequestId(
                QUrl(QStringLiteral("https://localhost:%1/%2%3").arg(httpsPort).arg(docname).arg(suffix)),
                requestId);

            QCNetworkRequest req(url);
            req.setSslConfig(QCNetworkSslConfig::insecureConfig());
            req.setHttpVersion(httpVersion);

            QCNetworkReply *reply = manager.sendGet(req);
            QVERIFY(reply);
            connect(reply, &QCNetworkReply::finished, this, [&, i, reply]() {
                const QString outFile = QStringLiteral("download_%1.data").arg(i);
                const std::optional<QByteArray> data = reply->readAll();
                QVERIFY(data.has_value());
                QVERIFY(writeAllToFile(outFile, *data, QIODevice::WriteOnly | QIODevice::Truncate));
                errors[i] = reply->error();
                reply->deleteLater();

                --remaining;
                if (remaining == 0) {
                    loop.quit();
                }
            });
        }

        loop.exec();
        QVERIFY2(timer.isActive(), "timeout waiting for multi-get4 downloads");
        for (int i = 0; i < errors.size(); ++i) {
            QCOMPARE(errors[i], NetworkError::NoError);
        }
        return;
    }

    if (caseId == QStringLiteral("ext_download_parallel_stress")) {
        QVERIFY(!docname.isEmpty());
        QVERIFY(httpsPort > 0);

        const QUrl url = withRequestId(
            QUrl(QStringLiteral("https://localhost:%1/%2").arg(httpsPort).arg(docname)),
            requestId);

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        timer.start(60000);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

        QVector<NetworkError> errors;
        errors.resize(count);

        int remaining = count;
        for (int i = 0; i < count; ++i) {
            QCNetworkRequest req(url);
            req.setSslConfig(QCNetworkSslConfig::insecureConfig());
            req.setHttpVersion(httpVersion);

            QCNetworkReply *reply = manager.sendGet(req);
            QVERIFY(reply);
            connect(reply, &QCNetworkReply::finished, this, [&, i, reply]() {
                const QString outFile = QStringLiteral("download_%1.data").arg(i);
                const std::optional<QByteArray> data = reply->readAll();
                QVERIFY(data.has_value());
                QVERIFY(writeAllToFile(outFile, *data, QIODevice::WriteOnly | QIODevice::Truncate));
                errors[i] = reply->error();
                reply->deleteLater();

                --remaining;
                if (remaining == 0) {
                    loop.quit();
                }
            });
        }

        loop.exec();
        QVERIFY2(timer.isActive(), "timeout waiting for parallel downloads");
        for (int i = 0; i < errors.size(); ++i) {
            QCOMPARE(errors[i], NetworkError::NoError);
        }
        return;
    }

    if (caseId == QStringLiteral("upload_put")) {
        const QUrl url = withRequestId(
            QUrl(QStringLiteral("https://localhost:%1/curltest/put").arg(httpsPort)),
            requestId);
        const QByteArray body = makeUploadBody(uploadSize);
        for (int i = 0; i < count; ++i) {
            const QString outFile = QStringLiteral("download_%1.data").arg(i);
            const NetworkError err = httpMethodToFile(manager, HttpMethod::Put, url, httpVersion, body, outFile);
            QCOMPARE(err, NetworkError::NoError);
        }
        return;
    }

    if (caseId == QStringLiteral("upload_post_reuse")) {
        const QUrl url = withRequestId(
            QUrl(QStringLiteral("https://localhost:%1/curltest/echo").arg(httpsPort)),
            requestId);
        const QByteArray body = makeUploadBody(uploadSize);
        for (int i = 0; i < count; ++i) {
            const QString outFile = QStringLiteral("download_%1.data").arg(i);
            const NetworkError err = httpMethodToFile(manager, HttpMethod::Post, url, httpVersion, body, outFile);
            QCOMPARE(err, NetworkError::NoError);
        }
        return;
    }

    if (caseId == QStringLiteral("postfields_binary_1531")) {
        const QUrl url = withRequestId(
            QUrl(QStringLiteral("https://localhost:%1/curltest/echo").arg(httpsPort)),
            requestId);
        QByteArray body;
        body.append(".abc", 4);
        body.append(char('\0'));
        body.append("xyz", 3);

        const QString outFile = QStringLiteral("download_0.data");
        const NetworkError err = httpMethodToFile(manager, HttpMethod::Post, url, httpVersion, body, outFile);
        QCOMPARE(err, NetworkError::NoError);

        QFile f(outFile);
        QVERIFY(f.exists());
        QCOMPARE(f.size(), qint64(body.size()));
        return;
    }

    if (caseId == QStringLiteral("cookiejar_1903")) {
        QVERIFY(httpPort > 0);
        QVERIFY(!cookiePath.isEmpty());
        const QUrl url = withRequestId(
            QUrl(QStringLiteral("http://localhost:%1/we/want/1903").arg(httpPort)),
            requestId);

        manager.setCookieFilePath(cookiePath, QCNetworkAccessManager::ReadOnly);
        {
            QCNetworkRequest req(url);
            req.setHttpVersion(httpVersion);
            auto *reply = manager.sendGetSync(req);
            QVERIFY(reply);
            QCOMPARE(reply->error(), NetworkError::NoError);
            delete reply;
        }

        manager.setCookieFilePath(cookiePath, QCNetworkAccessManager::ReadWrite);
        {
            QCNetworkRequest req(url);
            req.setHttpVersion(httpVersion);
            auto *reply = manager.sendGetSync(req);
            QVERIFY(reply);
            QCOMPARE(reply->error(), NetworkError::NoError);
            delete reply;
        }

        QFile f(cookiePath);
        QVERIFY(f.exists());
        QVERIFY(f.size() > 0);
        return;
    }

    if (caseId == QStringLiteral("ws_pingpong_small")) {
        QVERIFY(wsPort > 0);
        const QByteArray payload = QByteArray(125, 'x');

        QCWebSocket ws(withRequestId(QUrl(QStringLiteral("ws://localhost:%1/").arg(wsPort)), requestId));
        QSignalSpy connectedSpy(&ws, &QCWebSocket::connected);
        QSignalSpy pongSpy(&ws, &QCWebSocket::pongReceived);
        ws.open();
        QVERIFY(connectedSpy.wait(5000));
        ws.ping(payload);
        QVERIFY(pongSpy.wait(5000));
        const QByteArray pongPayload = pongSpy.takeFirst().at(0).toByteArray();
        QCOMPARE(pongPayload, payload);
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"), pongPayload, QIODevice::WriteOnly | QIODevice::Truncate));
        ws.close();
        return;
    }

    if (caseId == QStringLiteral("ws_data_small")) {
        QVERIFY(wsPort > 0);
        QCWebSocket ws(withRequestId(QUrl(QStringLiteral("ws://localhost:%1/").arg(wsPort)), requestId));
        QSignalSpy connectedSpy(&ws, &QCWebSocket::connected);
        QSignalSpy binSpy(&ws, &QCWebSocket::binaryMessageReceived);
        ws.open();
        QVERIFY(connectedSpy.wait(5000));

        QByteArray received;
        QByteArray expected;
        const int minLen = 1;
        const int maxLen = 10;
        const int repeats = 2;  // 对齐 cli_ws_data 默认 count=1 => 发送/接收 2 次
        const QByteArray pattern = QByteArray("0123456789");

        for (int len = minLen; len <= maxLen; ++len) {
            QByteArray msg;
            msg.reserve(len);
            for (int i = 0; i < len; ++i) {
                msg.append(pattern.at(i % pattern.size()));
            }
            for (int r = 0; r < repeats; ++r) {
                ws.sendBinaryMessage(msg);
                QVERIFY(binSpy.wait(5000));
                const QByteArray echoed = binSpy.takeFirst().at(0).toByteArray();
                QCOMPARE(echoed, msg);
                received.append(echoed);
                expected.append(msg);
            }
        }

        QCOMPARE(received, expected);
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"), received, QIODevice::WriteOnly | QIODevice::Truncate));
        ws.close();
        return;
    }

    if (caseId == QStringLiteral("ext_ws_ping_2301")) {
        QVERIFY(wsPort > 0);
        const QUrl url = withRequestId(
            QUrl(QStringLiteral("ws://localhost:%1/?scenario=lc_ping").arg(wsPort)),
            requestId);

        QCWebSocket ws(url);
        ws.setAutoPongEnabled(false);
        QSignalSpy connectedSpy(&ws, &QCWebSocket::connected);
        QSignalSpy pingSpy(&ws, &QCWebSocket::pingReceived);
        QSignalSpy closeSpy(&ws, &QCWebSocket::closeReceived);

        ws.open();
        QVERIFY(connectedSpy.wait(5000));

        QVERIFY(pingSpy.wait(5000));
        const QByteArray pingPayload = pingSpy.takeFirst().at(0).toByteArray();
        QCOMPARE(pingPayload.size(), 0);
        ws.pong(pingPayload);

        QVERIFY(closeSpy.wait(5000));
        const QList<QVariant> closeArgs = closeSpy.takeFirst();
        const int closeCode = closeArgs.at(0).toInt();
        const QString closeReason = closeArgs.at(1).toString();
        QCOMPARE(closeCode, 1000);
        QCOMPARE(closeReason, QStringLiteral("done"));

        QByteArray closePayload;
        closePayload.append(static_cast<char>((closeCode >> 8) & 0xFF));
        closePayload.append(static_cast<char>(closeCode & 0xFF));
        closePayload.append(closeReason.toUtf8());

        QVector<QPair<QByteArray, QByteArray>> events;
        events.append({QByteArrayLiteral("PING"), pingPayload});
        events.append({QByteArrayLiteral("CLOSE"), closePayload});
        QVERIFY(writeWsEventsToFile(QStringLiteral("download_0.data"), events));
        return;
    }

    if (caseId == QStringLiteral("ext_ws_deflate_ping")) {
        QVERIFY(wsPort > 0);
        const QUrl url = withRequestId(
            QUrl(QStringLiteral("ws://localhost:%1/?scenario=lc_ping").arg(wsPort)),
            requestId);

        QCWebSocket ws(url);
        ws.setCompressionConfig(QCWebSocketCompressionConfig::defaultConfig());
        ws.setAutoPongEnabled(false);
        QSignalSpy connectedSpy(&ws, &QCWebSocket::connected);
        QSignalSpy pingSpy(&ws, &QCWebSocket::pingReceived);
        QSignalSpy closeSpy(&ws, &QCWebSocket::closeReceived);

        ws.open();
        QVERIFY(connectedSpy.wait(5000));

        QVERIFY(pingSpy.wait(5000));
        const QByteArray pingPayload = pingSpy.takeFirst().at(0).toByteArray();
        QCOMPARE(pingPayload.size(), 0);
        ws.pong(pingPayload);

        QVERIFY(closeSpy.wait(5000));
        const QList<QVariant> closeArgs = closeSpy.takeFirst();
        const int closeCode = closeArgs.at(0).toInt();
        const QString closeReason = closeArgs.at(1).toString();
        QCOMPARE(closeCode, 1000);
        QCOMPARE(closeReason, QStringLiteral("done"));

        QByteArray closePayload;
        closePayload.append(static_cast<char>((closeCode >> 8) & 0xFF));
        closePayload.append(static_cast<char>(closeCode & 0xFF));
        closePayload.append(closeReason.toUtf8());

        QVector<QPair<QByteArray, QByteArray>> events;
        events.append({QByteArrayLiteral("PING"), pingPayload});
        events.append({QByteArrayLiteral("CLOSE"), closePayload});
        QVERIFY(writeWsEventsToFile(QStringLiteral("download_0.data"), events));
        return;
    }

    if (caseId == QStringLiteral("ext_ws_frame_types_2700")) {
        QVERIFY(wsPort > 0);
        const QUrl url = withRequestId(
            QUrl(QStringLiteral("ws://localhost:%1/?scenario=lc_frame_types").arg(wsPort)),
            requestId);

        QCWebSocket ws(url);
        ws.setAutoPongEnabled(false);
        QSignalSpy connectedSpy(&ws, &QCWebSocket::connected);
        QSignalSpy textSpy(&ws, &QCWebSocket::textMessageReceived);
        QSignalSpy binSpy(&ws, &QCWebSocket::binaryMessageReceived);
        QSignalSpy pingSpy(&ws, &QCWebSocket::pingReceived);
        QSignalSpy pongSpy(&ws, &QCWebSocket::pongReceived);
        QSignalSpy closeSpy(&ws, &QCWebSocket::closeReceived);

        ws.open();
        QVERIFY(connectedSpy.wait(5000));

        QVERIFY(textSpy.wait(5000));
        const QString text = textSpy.takeFirst().at(0).toString();
        QCOMPARE(text, QStringLiteral("txt"));

        QVERIFY(binSpy.wait(5000));
        const QByteArray bin = binSpy.takeFirst().at(0).toByteArray();
        QCOMPARE(bin, QByteArrayLiteral("bin"));

        QVERIFY(pingSpy.wait(5000));
        const QByteArray pingPayload = pingSpy.takeFirst().at(0).toByteArray();
        QCOMPARE(pingPayload, QByteArrayLiteral("ping"));
        ws.pong(pingPayload);

        QVERIFY(pongSpy.wait(5000));
        const QByteArray pongPayload = pongSpy.takeFirst().at(0).toByteArray();
        QCOMPARE(pongPayload, QByteArrayLiteral("pong"));

        QVERIFY(closeSpy.wait(5000));
        const QList<QVariant> closeArgs = closeSpy.takeFirst();
        const int closeCode = closeArgs.at(0).toInt();
        const QString closeReason = closeArgs.at(1).toString();
        QCOMPARE(closeCode, 1000);
        QCOMPARE(closeReason, QStringLiteral("close"));

        QByteArray closePayload;
        closePayload.append(static_cast<char>((closeCode >> 8) & 0xFF));
        closePayload.append(static_cast<char>(closeCode & 0xFF));
        closePayload.append(closeReason.toUtf8());

        QVector<QPair<QByteArray, QByteArray>> events;
        events.append({QByteArrayLiteral("TEXT"), text.toUtf8()});
        events.append({QByteArrayLiteral("BINARY"), bin});
        events.append({QByteArrayLiteral("PING"), pingPayload});
        events.append({QByteArrayLiteral("PONG"), pongPayload});
        events.append({QByteArrayLiteral("CLOSE"), closePayload});
        QVERIFY(writeWsEventsToFile(QStringLiteral("download_0.data"), events));
        return;
    }

    if (caseId == QStringLiteral("ext_reuse_keepalive")) {
        QVERIFY(observeHttpPort > 0);
        QVERIFY(count > 0);

        const QUrl url = withRequestId(
            QUrl(QStringLiteral("http://localhost:%1/empty_200").arg(observeHttpPort)),
            requestId);
        QCNetworkRequest req(url);
        req.setHttpVersion(httpVersion);

        QByteArray last;
        for (int i = 0; i < count; ++i) {
            QCNetworkReply *reply = manager.sendGet(req);
            QVERIFY(reply);

            QEventLoop loop;
            QTimer timer;
            timer.setSingleShot(true);
            timer.start(20000);
            connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
            connect(reply, &QCNetworkReply::finished, &loop, &QEventLoop::quit);

            loop.exec();
            QVERIFY2(timer.isActive(), "timeout waiting for reuse request");
            QCOMPARE(reply->error(), NetworkError::NoError);
            const auto dataOpt = reply->readAll();
            QVERIFY(dataOpt.has_value());
            last = *dataOpt;
            reply->deleteLater();
        }

        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"), last, QIODevice::WriteOnly | QIODevice::Truncate));
        return;
    }

    if (caseId == QStringLiteral("download_range_resume")) {
        QVERIFY(!docname.isEmpty());
        QVERIFY(abortOffset > 0);
        QVERIFY(fileSize > abortOffset);
        const QUrl url = withRequestId(
            QUrl(QStringLiteral("https://localhost:%1/%2").arg(httpsPort).arg(docname)),
            requestId);
        const QString outFile = QStringLiteral("download_0.data");

        qint64 firstBytes = 0;
        const NetworkError firstErr = httpGetToFile(manager, url, httpVersion, outFile, abortOffset, &firstBytes);
        QVERIFY(firstErr != NetworkError::NoError);
        QVERIFY(firstBytes > 0);

        const NetworkError secondErr = httpRangeToFile(manager, url, httpVersion, outFile, abortOffset, fileSize - 1);
        QCOMPARE(secondErr, NetworkError::NoError);

        QFile f(outFile);
        QVERIFY(f.exists());
        QVERIFY(f.size() == fileSize);
        return;
    }

    QFAIL(qPrintable(QStringLiteral("未知 QCURL_LC_CASE_ID: %1").arg(caseId)));
}

QTEST_MAIN(TestLibcurlConsistency)

#include "tst_LibcurlConsistency.moc"
