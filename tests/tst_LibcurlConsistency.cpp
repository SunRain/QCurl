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
#include <QFile>
#include <QSignalSpy>
#include <QUrl>
#include <QUrlQuery>

#include "QCNetworkAccessManager.h"
#include "QCNetworkError.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkSslConfig.h"
#include "QCWebSocket.h"

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

    const QString outDir = qEnvironmentVariable("QCURL_LC_OUT_DIR");
    if (!outDir.isEmpty()) {
        QVERIFY(QDir().mkpath(outDir));
        QVERIFY(QDir::setCurrent(outDir));
    }

    QCNetworkAccessManager manager;
    const QCNetworkHttpVersion httpVersion = toHttpVersion(proto);

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
