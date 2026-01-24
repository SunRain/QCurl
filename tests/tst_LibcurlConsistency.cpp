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
 * - QCURL_LC_EXPECT100_TIMEOUT_MS: Expect: 100-continue 等待超时（ms；可选；用于 p2_expect_100_continue）
 * - QCURL_LC_ABORT_OFFSET: 中断点（Range 续传用，默认 0）
 * - QCURL_LC_FILE_SIZE: 资源总长度（Range 续传用，默认 0）
 * - QCURL_LC_PAUSE_OFFSET: pause/resume 触发阈值（字节）
 * - QCURL_LC_REFERER: 显式 Referer（M1）
 * - QCURL_LC_SHARE_HANDLE: multi share handle（测试注入；默认不设置=关闭；off|dns,cookie,ssl_session）
 * - QCURL_LC_SOAK_DURATION_S: 长稳压测时长秒（lc_soak_parallel_get）
 * - QCURL_LC_SOAK_PARALLEL: 长稳并发数（lc_soak_parallel_get）
 * - QCURL_LC_SOAK_MAX_ERRORS: 长稳允许错误数（lc_soak_parallel_get）
 */

#include "QCMultipartFormData.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkConnectionPoolConfig.h"
#include "QCNetworkConnectionPoolManager.h"
#include "QCNetworkError.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkProxyConfig.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRetryPolicy.h"
#include "QCNetworkSslConfig.h"
#include "QCNetworkTimeoutConfig.h"
#include "QCWebSocket.h"
#include "QCWebSocketCompressionConfig.h"

#include <QBuffer>
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QScopedPointer>
#include <QSignalSpy>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVector>
#include <QtTest/QtTest>

#include <algorithm>
#include <cstring>
#include <curl/curl.h>
#include <optional>

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

bool waitForSpyCountAtLeast(QSignalSpy &spy, int targetCount, int timeoutMs)
{
    if (targetCount <= 0) {
        return true;
    }
    if (spy.count() >= targetCount) {
        return true;
    }

    QElapsedTimer timer;
    timer.start();
    while (spy.count() < targetCount) {
        const int remainingMs = timeoutMs - static_cast<int>(timer.elapsed());
        if (remainingMs <= 0) {
            break;
        }
        const int stepMs = std::min(remainingMs, 50);
        spy.wait(stepMs);
    }
    return spy.count() >= targetCount;
}

class SequentialReadDevice final : public QIODevice
{
public:
    explicit SequentialReadDevice(QByteArray data, QObject *parent = nullptr)
        : QIODevice(parent)
        , m_data(std::move(data))
    {}

    bool isSequential() const override { return true; }

    qint64 bytesAvailable() const override
    {
        return (m_data.size() - m_pos) + QIODevice::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        if (maxSize <= 0) {
            return 0;
        }
        const qint64 remaining = m_data.size() - m_pos;
        if (remaining <= 0) {
            return 0;
        }
        const qint64 n = qMin(maxSize, remaining);
        std::memcpy(data, m_data.constData() + m_pos, static_cast<size_t>(n));
        m_pos += n;
        return n;
    }

    qint64 writeData(const char *, qint64) override { return -1; }

private:
    QByteArray m_data;
    qint64 m_pos = 0;
};

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

void deleteReplyLater(QCNetworkReply *reply)
{
    if (!reply) {
        return;
    }
    reply->deleteLater();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

bool writeWsEventsToFile(const QString &filePath,
                         const QVector<QPair<QByteArray, QByteArray>> &events)
{
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    for (const auto &e : events) {
        const QByteArray &name    = e.first;
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

struct ProgressSummary
{
    qint64 nowMax   = 0;
    qint64 totalMax = 0;
    qint64 prevNow  = -1;
    bool monotonic  = true;
    int eventsCount = 0;

    void update(qint64 now, qint64 total)
    {
        if (prevNow >= 0 && now < prevNow) {
            monotonic = false;
        }
        prevNow  = now;
        nowMax   = qMax(nowMax, now);
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
    const qint64 written  = f.write(data);
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
            return 0; // 触发 CURLE_WRITE_ERROR -> 中断
        }
        const qint64 want = static_cast<qint64>(size);
        qint64 canWrite   = want;
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
        const qint64 w    = out.write(buffer, want);
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
        const qint64 w    = out.write(buffer, want);
        if (w != want) {
            return 0;
        }
        return size;
    });
    reply.execute();
    out.close();
    return reply.error();
}

} // namespace

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

    const QString proto   = qEnvironmentVariable("QCURL_LC_PROTO", "h2");
    const int httpsPort   = qEnvironmentVariableIntValue("QCURL_LC_HTTPS_PORT");
    const int wsPort      = qEnvironmentVariableIntValue("QCURL_LC_WS_PORT");
    const int count       = qMax(1, qEnvironmentVariableIntValue("QCURL_LC_COUNT"));
    const QString docname = qEnvironmentVariable("QCURL_LC_DOCNAME");
    const int uploadSize  = qMax(0, qEnvironmentVariableIntValue("QCURL_LC_UPLOAD_SIZE"));
    const bool hasExpect100TimeoutMs = qEnvironmentVariableIsSet("QCURL_LC_EXPECT100_TIMEOUT_MS");
    const int expect100TimeoutMs  = qEnvironmentVariableIntValue("QCURL_LC_EXPECT100_TIMEOUT_MS");
    const int abortOffset         = qMax(0, qEnvironmentVariableIntValue("QCURL_LC_ABORT_OFFSET"));
    const int fileSize            = qMax(0, qEnvironmentVariableIntValue("QCURL_LC_FILE_SIZE"));
    const int pauseOffset         = qMax(0, qEnvironmentVariableIntValue("QCURL_LC_PAUSE_OFFSET"));
    const QString requestId       = qEnvironmentVariable("QCURL_LC_REQ_ID");
    const int httpPort            = qEnvironmentVariableIntValue("QCURL_LC_HTTP_PORT");
    const QString cookiePath      = qEnvironmentVariable("QCURL_LC_COOKIE_PATH");
    const int proxyPort           = qEnvironmentVariableIntValue("QCURL_LC_PROXY_PORT");
    const QString proxyUser       = qEnvironmentVariable("QCURL_LC_PROXY_USER");
    const QString proxyPass       = qEnvironmentVariable("QCURL_LC_PROXY_PASS");
    const QString proxyTargetUrl  = qEnvironmentVariable("QCURL_LC_PROXY_TARGET_URL");
    const int socks5Port          = qEnvironmentVariableIntValue("QCURL_LC_SOCKS5_PORT");
    const int observeHttpPort     = qEnvironmentVariableIntValue("QCURL_LC_OBSERVE_HTTP_PORT");
    const int observeStatusCode   = qEnvironmentVariableIntValue("QCURL_LC_STATUS_CODE");
    const int observeHttpsPort    = qEnvironmentVariableIntValue("QCURL_LC_OBSERVE_HTTPS_PORT");
    const QString caCertPath      = qEnvironmentVariable("QCURL_LC_CA_CERT_PATH");
    const QString pinnedPublicKey = qEnvironmentVariable("QCURL_LC_PINNED_PUBLIC_KEY");
    const QString hstsPath        = qEnvironmentVariable("QCURL_LC_HSTS_PATH");
    const QString altSvcPath      = qEnvironmentVariable("QCURL_LC_ALTSVC_PATH");
    const QString targetUrl       = qEnvironmentVariable("QCURL_LC_TARGET_URL");
    const QString authUser        = qEnvironmentVariable("QCURL_LC_AUTH_USER");
    const QString authPass        = qEnvironmentVariable("QCURL_LC_AUTH_PASS");
    const QString referer         = qEnvironmentVariable("QCURL_LC_REFERER");
    const int bpLimitBytes  = qMax(0, qEnvironmentVariableIntValue("QCURL_LC_BP_LIMIT_BYTES"));
    const int bpResumeBytes = qMax(0, qEnvironmentVariableIntValue("QCURL_LC_BP_RESUME_BYTES"));

    const QString outDir = qEnvironmentVariable("QCURL_LC_OUT_DIR");
    if (!outDir.isEmpty()) {
        QVERIFY(QDir().mkpath(outDir));
        QVERIFY(QDir::setCurrent(outDir));
    }

    QCNetworkAccessManager manager;
    const QCNetworkHttpVersion httpVersion = toHttpVersion(proto);

    const QString shareHandleEnv = qEnvironmentVariable("QCURL_LC_SHARE_HANDLE");
    if (!shareHandleEnv.isEmpty()) {
        QCNetworkAccessManager::ShareHandleConfig shareCfg;
        const QString normalized = shareHandleEnv.trimmed().toLower();
        if (normalized == QStringLiteral("off") || normalized == QStringLiteral("0")
            || normalized == QStringLiteral("false")) {
            // 保持默认关闭
        } else {
            const QStringList tokens = normalized.split(QChar(','), Qt::SkipEmptyParts);
            if (tokens.isEmpty()) {
                QFAIL("QCURL_LC_SHARE_HANDLE 解析失败：空值");
            }
            for (const QString &raw : tokens) {
                const QString t = raw.trimmed();
                if (t == QStringLiteral("dns") || t == QStringLiteral("dns_cache")
                    || t == QStringLiteral("dns-cache")) {
                    shareCfg.shareDnsCache = true;
                } else if (t == QStringLiteral("cookie") || t == QStringLiteral("cookies")) {
                    shareCfg.shareCookies = true;
                } else if (t == QStringLiteral("ssl") || t == QStringLiteral("ssl_session")
                           || t == QStringLiteral("ssl-session")) {
                    shareCfg.shareSslSession = true;
                } else {
                    QFAIL(qPrintable(
                        QStringLiteral("QCURL_LC_SHARE_HANDLE 解析失败：未知 token=%1").arg(t)));
                }
            }
        }
        manager.setShareHandleConfig(shareCfg);
    }

    if (!hstsPath.isEmpty() || !altSvcPath.isEmpty()) {
        QCNetworkAccessManager::HstsAltSvcCacheConfig cacheCfg;
        cacheCfg.hstsFilePath   = hstsPath;
        cacheCfg.altSvcFilePath = altSvcPath;
        manager.setHstsAltSvcCacheConfig(cacheCfg);
    }

    if (caseId == QStringLiteral("p1_resolve_override")) {
        QVERIFY(observeHttpPort > 0);
        const QString host = QStringLiteral("example.invalid");
        const QUrl url     = withRequestId(QUrl(QStringLiteral("http://%1:%2/status/200")
                                                .arg(host)
                                                .arg(observeHttpPort)),
                                       requestId);

        QCNetworkRequest req(url);
        req.setHttpVersion(httpVersion);
        req.setResolveOverride(QStringList{
            QStringLiteral("%1:%2:127.0.0.1").arg(host).arg(observeHttpPort),
        });

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), NetworkError::NoError);
        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        deleteReplyLater(reply);
        return;
    }

    if (caseId == QStringLiteral("p1_connect_to")) {
        QVERIFY(observeHttpPort > 0);
        const QString host    = QStringLiteral("example.invalid");
        const int logicalPort = 18080;
        const QUrl url        = withRequestId(
            QUrl(QStringLiteral("http://%1:%2/status/200").arg(host).arg(logicalPort)), requestId);

        QCNetworkRequest req(url);
        req.setHttpVersion(httpVersion);
        req.setConnectTo(QStringList{
            QStringLiteral("%1:%2:127.0.0.1:%3").arg(host).arg(logicalPort).arg(observeHttpPort),
        });

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), NetworkError::NoError);
        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        deleteReplyLater(reply);
        return;
    }

    if (caseId == QStringLiteral("p2_protocols_block_http")) {
        QVERIFY(observeHttpPort > 0);
        const QUrl url = withRequestId(QUrl(QStringLiteral("http://localhost:%1/status/200")
                                                .arg(observeHttpPort)),
                                       requestId);

        QCNetworkRequest req(url);
        req.setHttpVersion(httpVersion);
        req.setAllowedProtocols(QStringList{QStringLiteral("https")});
        req.setUnsupportedSecurityOptionPolicy(QCUnsupportedSecurityOptionPolicy::Fail);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), fromCurlCode(CURLE_UNSUPPORTED_PROTOCOL));
        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        deleteReplyLater(reply);
        return;
    }

    if (caseId == QStringLiteral("p2_redir_protocols_block_http")) {
        QVERIFY(observeHttpPort > 0);
        const QUrl url = withRequestId(QUrl(QStringLiteral("http://localhost:%1/redir/1")
                                                .arg(observeHttpPort)),
                                       requestId);

        QCNetworkRequest req(url);
        req.setHttpVersion(httpVersion);
        req.setFollowLocation(true);
        req.setMaxRedirects(10);
        req.setAllowedRedirectProtocols(QStringList{QStringLiteral("https")});
        req.setUnsupportedSecurityOptionPolicy(QCUnsupportedSecurityOptionPolicy::Fail);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), fromCurlCode(CURLE_UNSUPPORTED_PROTOCOL));
        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        deleteReplyLater(reply);
        return;
    }

    if (caseId == QStringLiteral("p2_tls_verify_success")) {
        QVERIFY(observeHttpsPort > 0);
        QVERIFY(!caCertPath.isEmpty());

        QCNetworkSslConfig ssl = QCNetworkSslConfig::defaultConfig();
        ssl.caCertPath         = caCertPath;

        QCNetworkRequest req(
            withRequestId(QUrl(QStringLiteral("https://localhost:%1/cookie").arg(observeHttpsPort)),
                          requestId));
        req.setSslConfig(ssl);
        req.setHttpVersion(httpVersion);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), NetworkError::NoError);
        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        deleteReplyLater(reply);
        return;
    }

    if (caseId == QStringLiteral("p2_tls_verify_fail_no_ca")) {
        QVERIFY(observeHttpsPort > 0);

        QCNetworkSslConfig ssl = QCNetworkSslConfig::defaultConfig();

        QCNetworkRequest req(
            withRequestId(QUrl(QStringLiteral("https://localhost:%1/cookie").arg(observeHttpsPort)),
                          requestId));
        req.setSslConfig(ssl);
        req.setHttpVersion(httpVersion);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), NetworkError::SslHandshakeFailed);
        deleteReplyLater(reply);
        return;
    }

    if (caseId == QStringLiteral("p2_tls_pinned_public_key_match")) {
        QVERIFY(observeHttpsPort > 0);
        QVERIFY(!caCertPath.isEmpty());
        QVERIFY(!pinnedPublicKey.isEmpty());

        QCNetworkSslConfig ssl = QCNetworkSslConfig::defaultConfig();
        ssl.caCertPath         = caCertPath;
        ssl.pinnedPublicKey    = pinnedPublicKey;

        QCNetworkRequest req(
            withRequestId(QUrl(QStringLiteral("https://localhost:%1/cookie").arg(observeHttpsPort)),
                          requestId));
        req.setSslConfig(ssl);
        req.setHttpVersion(httpVersion);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), NetworkError::NoError);
        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        deleteReplyLater(reply);
        return;
    }

    if (caseId == QStringLiteral("p2_tls_pinned_public_key_mismatch")) {
        QVERIFY(observeHttpsPort > 0);
        QVERIFY(!caCertPath.isEmpty());
        QVERIFY(!pinnedPublicKey.isEmpty());

        QCNetworkSslConfig ssl = QCNetworkSslConfig::defaultConfig();
        ssl.caCertPath         = caCertPath;
        ssl.pinnedPublicKey    = pinnedPublicKey;

        QCNetworkRequest req(
            withRequestId(QUrl(QStringLiteral("https://localhost:%1/cookie").arg(observeHttpsPort)),
                          requestId));
        req.setSslConfig(ssl);
        req.setHttpVersion(httpVersion);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QVERIFY(reply->error() != NetworkError::NoError);
        deleteReplyLater(reply);
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
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        deleteReplyLater(reply);
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
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        deleteReplyLater(reply);
        return;
    }

    if (caseId == QStringLiteral("p2_socks5_proxy_connect_fail")) {
        QVERIFY(!targetUrl.isEmpty());
        QVERIFY(socks5Port > 0);

        QCNetworkProxyConfig proxy;
        proxy.type     = QCNetworkProxyConfig::ProxyType::Socks5;
        proxy.hostName = QStringLiteral("127.0.0.1");
        proxy.port     = static_cast<quint16>(socks5Port);

        QCNetworkRequest req{QUrl(targetUrl)};
        req.setHttpVersion(httpVersion);
        req.setProxyConfig(proxy);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), fromCurlCode(CURLE_PROXY));
        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        deleteReplyLater(reply);
        return;
    }

    if (caseId == QStringLiteral("p1_redirect_nofollow")
        || caseId == QStringLiteral("p1_redirect_follow")) {
        QVERIFY(observeHttpPort > 0);
        const bool follow = (caseId == QStringLiteral("p1_redirect_follow"));

        QCNetworkRequest req(
            withRequestId(QUrl(QStringLiteral("http://localhost:%1/redir/3").arg(observeHttpPort)),
                          requestId));
        req.setHttpVersion(httpVersion);
        req.setFollowLocation(follow);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), NetworkError::NoError);

        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        deleteReplyLater(reply);
        return;
    }

    if (caseId == QStringLiteral("p1_redirect_auto_referer")) {
        QVERIFY(observeHttpPort > 0);

        QCNetworkRequest req(
            withRequestId(QUrl(QStringLiteral("http://localhost:%1/redir/1").arg(observeHttpPort)),
                          requestId));
        req.setHttpVersion(httpVersion);
        req.setFollowLocation(true);
        req.setAutoRefererEnabled(true);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), NetworkError::NoError);

        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        deleteReplyLater(reply);
        return;
    }

    if (caseId == QStringLiteral("p1_redirect_max_redirs_too_many")) {
        QVERIFY(observeHttpPort > 0);

        QCNetworkRequest req(
            withRequestId(QUrl(QStringLiteral("http://localhost:%1/redir/3").arg(observeHttpPort)),
                          requestId));
        req.setHttpVersion(httpVersion);
        req.setFollowLocation(true);
        req.setMaxRedirects(1);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), NetworkError::TooManyRedirects);

        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        deleteReplyLater(reply);
        return;
    }

    if (caseId == QStringLiteral("p1_accept_encoding_gzip")) {
        QVERIFY(observeHttpPort > 0);

        QCNetworkRequest req(
            withRequestId(QUrl(QStringLiteral("http://localhost:%1/enc").arg(observeHttpPort)),
                          requestId));
        req.setHttpVersion(httpVersion);
        req.setAcceptedEncodings(QStringList{QStringLiteral("gzip")});

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), NetworkError::NoError);

        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        deleteReplyLater(reply);
        return;
    }

    if (caseId == QStringLiteral("p1_referer_explicit")) {
        QVERIFY(observeHttpPort > 0);
        QVERIFY(!referer.isEmpty());

        QCNetworkRequest req(
            withRequestId(QUrl(QStringLiteral("http://localhost:%1/abs_target").arg(observeHttpPort)),
                          requestId));
        req.setHttpVersion(httpVersion);
        req.setReferer(referer);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), NetworkError::NoError);

        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        deleteReplyLater(reply);
        return;
    }

    if (caseId == QStringLiteral("p1_unrestricted_sensitive_headers_redirect_off")
        || caseId == QStringLiteral("p1_unrestricted_sensitive_headers_redirect_on")) {
        QVERIFY(!targetUrl.isEmpty());
        QVERIFY(!authUser.isEmpty());
        QVERIFY(!authPass.isEmpty());

        const bool unrestricted = (caseId
                                   == QStringLiteral(
                                       "p1_unrestricted_sensitive_headers_redirect_on"));

        QCNetworkRequest req(withRequestId(QUrl(targetUrl), requestId));
        req.setHttpVersion(httpVersion);
        req.setFollowLocation(true);
        if (unrestricted) {
            req.setAllowUnrestrictedSensitiveHeadersOnRedirect(true);
        }

        QCNetworkHttpAuthConfig auth;
        auth.userName              = authUser;
        auth.password              = authPass;
        auth.method                = QCNetworkHttpAuthMethod::Basic;
        auth.allowUnrestrictedAuth = false;
        req.setHttpAuth(auth);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), NetworkError::NoError);

        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        deleteReplyLater(reply);
        return;
    }

    if (caseId == QStringLiteral("p1_httpauth_any_basic")
        || caseId == QStringLiteral("p1_httpauth_any_basic_wrong_pass")
        || caseId == QStringLiteral("p1_httpauth_anysafe_digest")
        || caseId == QStringLiteral("p1_httpauth_anysafe_digest_wrong_pass")
        || caseId == QStringLiteral("p1_unrestricted_auth_redirect_off")
        || caseId == QStringLiteral("p1_unrestricted_auth_redirect_on")) {
        QVERIFY(!targetUrl.isEmpty());
        QVERIFY(!authUser.isEmpty());
        QVERIFY(!authPass.isEmpty());

        const bool expectHttp401 = (caseId == QStringLiteral("p1_httpauth_any_basic_wrong_pass")
                                    || caseId
                                           == QStringLiteral(
                                               "p1_httpauth_anysafe_digest_wrong_pass"));
        const bool follow        = (caseId == QStringLiteral("p1_unrestricted_auth_redirect_off")
                             || caseId == QStringLiteral("p1_unrestricted_auth_redirect_on"));
        const bool unrestricted  = (caseId == QStringLiteral("p1_unrestricted_auth_redirect_on"));

        QCNetworkRequest req(withRequestId(QUrl(targetUrl), requestId));
        req.setHttpVersion(httpVersion);
        req.setFollowLocation(follow);

        QCNetworkHttpAuthConfig auth;
        auth.userName              = authUser;
        auth.password              = authPass;
        auth.allowUnrestrictedAuth = unrestricted;
        if (caseId == QStringLiteral("p1_httpauth_any_basic")
            || caseId == QStringLiteral("p1_httpauth_any_basic_wrong_pass")) {
            auth.method = QCNetworkHttpAuthMethod::Any;
        } else if (caseId == QStringLiteral("p1_httpauth_anysafe_digest")
                   || caseId == QStringLiteral("p1_httpauth_anysafe_digest_wrong_pass")) {
            auth.method = QCNetworkHttpAuthMethod::AnySafe;
        } else {
            auth.method = QCNetworkHttpAuthMethod::Basic;
        }
        req.setHttpAuth(auth);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        if (expectHttp401) {
            QCOMPARE(reply->error(), NetworkError::HttpUnauthorized);
            QCOMPARE(reply->httpStatusCode(), 401);
        } else {
            QCOMPARE(reply->error(), NetworkError::NoError);
        }

        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        deleteReplyLater(reply);
        return;
    }

    if (caseId == QStringLiteral("p1_redirect_post_301_to_get")) {
        QVERIFY(observeHttpPort > 0);
        QVERIFY(uploadSize > 0);

        const QUrl url = withRequestId(QUrl(QStringLiteral("http://localhost:%1/redir_post_301")
                                                .arg(observeHttpPort)),
                                       requestId);
        const QByteArray body = makeUploadBody(uploadSize);

        QCNetworkRequest req(url);
        req.setHttpVersion(httpVersion);
        req.setFollowLocation(true);

        QCNetworkReply *reply = manager.sendPost(req, body);
        QVERIFY(reply);

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        timer.start(20000);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        connect(reply, &QCNetworkReply::finished, &loop, &QEventLoop::quit);

        loop.exec();
        QVERIFY2(timer.isActive(), "timeout waiting for post redirect request");
        QCOMPARE(reply->error(), NetworkError::NoError);

        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));

        reply->deleteLater();
        return;
    }

    if (caseId == QStringLiteral("p1_redirect_postredir_keep_post_301")) {
        QVERIFY(observeHttpPort > 0);
        QVERIFY(uploadSize > 0);

        const QUrl url = withRequestId(QUrl(QStringLiteral("http://localhost:%1/redir_post_301")
                                                .arg(observeHttpPort)),
                                       requestId);
        const QByteArray body = makeUploadBody(uploadSize);

        QCNetworkRequest req(url);
        req.setHttpVersion(httpVersion);
        req.setFollowLocation(true);
        req.setPostRedirectPolicy(QCNetworkPostRedirectPolicy::KeepPost301);

        QCNetworkReply *reply = manager.sendPost(req, body);
        QVERIFY(reply);

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        timer.start(20000);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        connect(reply, &QCNetworkReply::finished, &loop, &QEventLoop::quit);

        loop.exec();
        QVERIFY2(timer.isActive(), "timeout waiting for post redirect request");
        QCOMPARE(reply->error(), NetworkError::NoError);

        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));

        reply->deleteLater();
        return;
    }

    if (caseId == QStringLiteral("p1_stream_body_redirect_307_post_seekable")
        || caseId == QStringLiteral("p1_stream_body_redirect_307_post_nonseekable")
        || caseId == QStringLiteral("p1_stream_body_redirect_307_put_seekable")
        || caseId == QStringLiteral("p1_stream_body_redirect_307_put_nonseekable")
        || caseId == QStringLiteral("p1_stream_body_httpauth_anysafe_digest_post_seekable")
        || caseId == QStringLiteral("p1_stream_body_httpauth_anysafe_digest_post_nonseekable")) {
        QVERIFY(observeHttpPort > 0);

        const bool seekable      = caseId.endsWith(QStringLiteral("_seekable"));
        const bool isRedirect307 = caseId.startsWith(
            QStringLiteral("p1_stream_body_redirect_307_"));
        const bool isDigestAnySafe = caseId.startsWith(
            QStringLiteral("p1_stream_body_httpauth_anysafe_digest_"));

        const int uploadSize  = 4096;
        const QByteArray body = makeUploadBody(uploadSize);

        QScopedPointer<QIODevice> device;
        if (seekable) {
            auto *buf = new QBuffer();
            buf->setData(body);
            QVERIFY(buf->open(QIODevice::ReadOnly));
            device.reset(buf);
        } else {
            auto *seq = new SequentialReadDevice(body);
            QVERIFY(seq->open(QIODevice::ReadOnly));
            device.reset(seq);
        }

        QUrl url;
        HttpMethod method = HttpMethod::Post;
        QCNetworkRequest req;

        if (isRedirect307) {
            const bool isPut = caseId.contains(QStringLiteral("_put_"));
            method           = isPut ? HttpMethod::Put : HttpMethod::Post;
            url              = withRequestId(QUrl(QStringLiteral("http://localhost:%1/redir_307")
                                         .arg(observeHttpPort)),
                                requestId);

            req = QCNetworkRequest(url);
            req.setHttpVersion(httpVersion);
            req.setFollowLocation(true);
        } else if (isDigestAnySafe) {
            method = HttpMethod::Post;
            url    = withRequestId(QUrl(QStringLiteral("http://localhost:%1/auth/digest")
                                         .arg(observeHttpPort)),
                                requestId);

            req = QCNetworkRequest(url);
            req.setHttpVersion(httpVersion);

            QCNetworkHttpAuthConfig cfg;
            cfg.userName = QStringLiteral("user");
            cfg.password = QStringLiteral("passwd");
            cfg.method   = QCNetworkHttpAuthMethod::AnySafe;
            req.setHttpAuth(cfg);
        } else {
            QFAIL("unexpected stream-body case id");
        }

        req.setUploadDevice(device.data(), std::optional<qint64>(static_cast<qint64>(uploadSize)));

        QCNetworkReply reply(req, method, ExecutionMode::Sync, QByteArray(), &manager);
        reply.execute();

        if (seekable) {
            QCOMPARE(reply.error(), NetworkError::NoError);
        } else {
            QCOMPARE(reply.error(), NetworkError::InvalidRequest);
            QVERIFY2(reply.errorString().contains(QStringLiteral("无法重发 body")),
                     "expected non-seekable replay to fail with a rewind/seek diagnostic");
            if (isDigestAnySafe) {
                QCOMPARE(reply.httpStatusCode(), 401);
            }
        }

        QByteArray out;
        if (seekable) {
            const auto dataOpt = reply.readAll();
            QVERIFY(dataOpt.has_value());
            out = dataOpt.value();
        }

        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               out,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        return;
    }

    if (caseId == QStringLiteral("p2_stream_body_post_chunked_unknown_size")) {
        QVERIFY(observeHttpPort > 0);

        const int uploadSize  = 4096;
        const QByteArray body = makeUploadBody(uploadSize);

        QScopedPointer<QIODevice> device(new SequentialReadDevice(body));
        QVERIFY(device->open(QIODevice::ReadOnly));

        const QUrl url
            = withRequestId(QUrl(QStringLiteral("http://localhost:%1/method").arg(observeHttpPort)),
                            requestId);

        QCNetworkRequest req(url);
        req.setHttpVersion(httpVersion);
        req.setAllowChunkedUploadForPost(true);
        req.setUploadDevice(device.data(), std::nullopt);

        QCNetworkReply reply(req, HttpMethod::Post, ExecutionMode::Sync, QByteArray(), &manager);
        reply.execute();
        QCOMPARE(reply.error(), NetworkError::NoError);

        const auto dataOpt = reply.readAll();
        QVERIFY(dataOpt.has_value());
        QCOMPARE(dataOpt.value(), body);
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               dataOpt.value(),
                               QIODevice::WriteOnly | QIODevice::Truncate));
        return;
    }

    if (caseId == QStringLiteral("p1_empty_body_200")
        || caseId == QStringLiteral("p1_empty_body_204")) {
        QVERIFY(observeHttpPort > 0);

        const QString path = (caseId == QStringLiteral("p1_empty_body_200"))
                                 ? QStringLiteral("/empty_200")
                                 : QStringLiteral("/no_content");

        QCNetworkRequest req(
            withRequestId(QUrl(
                              QStringLiteral("http://localhost:%1%2").arg(observeHttpPort).arg(path)),
                          requestId));
        req.setHttpVersion(httpVersion);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), NetworkError::NoError);

        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        deleteReplyLater(reply);
        return;
    }

    if (caseId == QStringLiteral("p1_resp_headers")) {
        QVERIFY(observeHttpPort > 0);

        QCNetworkRequest req(withRequestId(QUrl(QStringLiteral("http://localhost:%1/resp_headers")
                                                    .arg(observeHttpPort)),
                                           requestId));
        req.setHttpVersion(httpVersion);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), NetworkError::NoError);

        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));

        const QByteArray headerData = reply->rawHeaderData();
        QVERIFY(writeAllToFile(QStringLiteral("response_headers_0.data"),
                               headerData,
                               QIODevice::WriteOnly | QIODevice::Truncate));

        deleteReplyLater(reply);
        return;
    }

    if (caseId == QStringLiteral("resp_headers_unfold_1940")) {
        QVERIFY(observeHttpPort > 0);

        const QUrl url = withRequestId(QUrl(QStringLiteral(
                                                "http://localhost:%1/resp_headers?scenario=1940")
                                                .arg(observeHttpPort)),
                                       requestId);

        QCNetworkRequest req(url);
        req.setHttpVersion(httpVersion);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), NetworkError::NoError);

        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));

        const QByteArray headerData = reply->rawHeaderData();
        QVERIFY(writeAllToFile(QStringLiteral("response_headers_0.data"),
                               headerData,
                               QIODevice::WriteOnly | QIODevice::Truncate));

        // 解析 raw headers（unfold + trim；保留重复头），并落盘为 JSON（供 pytest 对比）
        auto parseUnfolded = [](const QByteArray &raw) -> QVector<QPair<QByteArray, QByteArray>> {
            QVector<QPair<QByteArray, QByteArray>> out;

            auto flush = [&](const QByteArray &name, const QList<QByteArray> &segments) {
                if (name.isEmpty()) {
                    return;
                }
                QByteArray value;
                for (const QByteArray &seg : segments) {
                    const QByteArray part = seg.trimmed();
                    if (part.isEmpty()) {
                        continue;
                    }
                    if (!value.isEmpty()) {
                        value.append(' ');
                    }
                    value.append(part);
                }
                out.append(qMakePair(name, value));
            };

            QByteArray currentName;
            QList<QByteArray> currentSegments;

            const QList<QByteArray> lines = raw.split('\n');
            for (QByteArray line : lines) {
                if (line.endsWith('\r')) {
                    line.chop(1);
                }
                if (line.isEmpty()) {
                    flush(currentName, currentSegments);
                    currentName.clear();
                    currentSegments.clear();
                    continue;
                }
                if (line.startsWith("HTTP/")) {
                    flush(currentName, currentSegments);
                    currentName.clear();
                    currentSegments.clear();
                    continue;
                }

                const bool isContinuation = !currentName.isEmpty()
                                            && (line.startsWith(' ') || line.startsWith('\t'));
                if (isContinuation) {
                    currentSegments.append(line);
                    continue;
                }

                const int colonPos = line.indexOf(':');
                if (colonPos <= 0) {
                    continue;
                }
                flush(currentName, currentSegments);
                currentName = line.left(colonPos).trimmed();
                currentSegments.clear();
                currentSegments.append(line.mid(colonPos + 1));
            }
            flush(currentName, currentSegments);
            return out;
        };

        const QVector<QPair<QByteArray, QByteArray>> unfoldedPairs = parseUnfolded(headerData);

        QJsonObject unfoldedJson;
        QMap<QString, QStringList> multi;
        QMap<QString, QString> single;

        for (const auto &p : unfoldedPairs) {
            const QString name  = QString::fromUtf8(p.first);
            const QString value = QString::fromUtf8(p.second);
            if (multi.contains(name)) {
                multi[name].append(value);
            } else if (single.contains(name)) {
                // 第 2 次出现：转为 multi
                const QString firstValue = single.take(name);
                multi[name]              = QStringList{firstValue, value};
            } else {
                single.insert(name, value);
            }
        }

        for (auto it = single.cbegin(); it != single.cend(); ++it) {
            unfoldedJson.insert(it.key(), it.value());
        }
        for (auto it = multi.cbegin(); it != multi.cend(); ++it) {
            QJsonArray arr;
            for (const QString &v : it.value()) {
                arr.append(v);
            }
            unfoldedJson.insert(it.key(), arr);
        }

        // 关键字段 sanity check：确保 QCNetworkReply 的 header 解析已正确 unfold
        const QList<RawHeaderPair> parsed = reply->rawHeaders();
        auto headerValue                  = [&parsed](const QByteArray &key) -> QByteArray {
            for (const auto &kv : parsed) {
                if (kv.first == key) {
                    return kv.second;
                }
            }
            return QByteArray();
        };
        QCOMPARE(headerValue("Server"), QByteArray("test with trailing space"));
        QCOMPARE(headerValue("Fold"), QByteArray("is folding a line"));
        QCOMPARE(headerValue("Test"), QByteArray("word"));

        QVERIFY(writeJsonObjectToFile(QStringLiteral("headers_unfolded_1940.json"), unfoldedJson));
        deleteReplyLater(reply);
        return;
    }

    if (caseId == QStringLiteral("p1_progress_download")) {
        QVERIFY(httpsPort > 0);
        QVERIFY(!docname.isEmpty());

        const QUrl url = withRequestId(
            QUrl(QStringLiteral("https://localhost:%1/%2").arg(httpsPort).arg(docname)), requestId);

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
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               data,
                               QIODevice::WriteOnly | QIODevice::Truncate));

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

        const QUrl url = withRequestId(QUrl(QStringLiteral("https://localhost:%1/curltest/echo")
                                                .arg(httpsPort)),
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
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               data,
                               QIODevice::WriteOnly | QIODevice::Truncate));

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

        const QUrl url = withRequestId(QUrl(QStringLiteral("http://localhost:%1/head_with_body")
                                                .arg(observeHttpPort)),
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
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));

        reply->deleteLater();
        return;
    }

    if (caseId == QStringLiteral("p1_method_patch")) {
        QVERIFY(observeHttpPort > 0);
        QVERIFY(uploadSize > 0);

        const QUrl url
            = withRequestId(QUrl(QStringLiteral("http://localhost:%1/method").arg(observeHttpPort)),
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
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));

        reply->deleteLater();
        return;
    }

    if (caseId == QStringLiteral("p1_method_delete")) {
        QVERIFY(observeHttpPort > 0);

        const QUrl url
            = withRequestId(QUrl(QStringLiteral("http://localhost:%1/method").arg(observeHttpPort)),
                            requestId);

        QCNetworkRequest req(url);
        req.setHttpVersion(httpVersion);

        QCNetworkReply *reply = manager.sendDelete(req);
        QVERIFY(reply);

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        timer.start(20000);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        connect(reply, &QCNetworkReply::finished, &loop, &QEventLoop::quit);

        loop.exec();
        QVERIFY2(timer.isActive(), "timeout waiting for delete request");
        QCOMPARE(reply->error(), NetworkError::NoError);

        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(dataOpt->isEmpty());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));

        reply->deleteLater();
        return;
    }

    if (caseId == QStringLiteral("p2_expect_100_continue")) {
        QVERIFY(observeHttpPort > 0);
        QVERIFY(uploadSize > 0);

        const QUrl url        = withRequestId(QUrl(QStringLiteral("http://localhost:%1/expect_417")
                                                .arg(observeHttpPort)),
                                       requestId);
        const QByteArray body = makeUploadBody(uploadSize);

        QCNetworkRequest req(url);
        req.setHttpVersion(httpVersion);
        if (hasExpect100TimeoutMs) {
            QVERIFY2(expect100TimeoutMs >= 0, "QCURL_LC_EXPECT100_TIMEOUT_MS must be >= 0");
            req.setExpect100ContinueTimeout(std::chrono::milliseconds(expect100TimeoutMs));
        }
        QCNetworkReply *reply = manager.sendPut(req, body);
        QVERIFY(reply);

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        timer.start(60000);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        connect(reply, &QCNetworkReply::finished, &loop, &QEventLoop::quit);

        loop.exec();
        QVERIFY2(timer.isActive(), "timeout waiting for expect-100-continue case");
        QCOMPARE(reply->error(), NetworkError::NoError);

        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QCOMPARE(*dataOpt, body);
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));

        reply->deleteLater();
        return;
    }

    if (caseId == QStringLiteral("p1_multipart_formdata")) {
        QVERIFY(observeHttpPort > 0);

        const QUrl url = withRequestId(QUrl(QStringLiteral("http://localhost:%1/multipart")
                                                .arg(observeHttpPort)),
                                       requestId);

        QCMultipartFormData formData;
        formData.addTextField(QStringLiteral("alpha"), QStringLiteral("hello"));
        formData.addTextField(QStringLiteral("beta"), QStringLiteral("world"));

        QByteArray binary;
        binary.resize(256);
        for (int i = 0; i < 256; ++i) {
            binary[i] = static_cast<char>(i);
        }
        formData.addFileField(QStringLiteral("file"),
                              QStringLiteral("a.bin"),
                              binary,
                              QStringLiteral("application/octet-stream"));

        QCNetworkRequest req(url);
        req.setHttpVersion(httpVersion);
        req.setRawHeader(QByteArrayLiteral("Content-Type"), formData.contentType().toUtf8());

        QCNetworkReply *reply = manager.sendPost(req, formData.toByteArray());
        QVERIFY(reply);

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        timer.start(20000);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        connect(reply, &QCNetworkReply::finished, &loop, &QEventLoop::quit);

        loop.exec();
        QVERIFY2(timer.isActive(), "timeout waiting for multipart request");
        QCOMPARE(reply->error(), NetworkError::NoError);

        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));

        reply->deleteLater();
        return;
    }

    if (caseId == QStringLiteral("p1_timeout_delay_headers")) {
        QVERIFY(observeHttpPort > 0);

        QCNetworkTimeoutConfig timeout;
        timeout.totalTimeout = std::chrono::milliseconds(200);

        QCNetworkRequest req(
            withRequestId(QUrl(QStringLiteral("http://localhost:%1/delay_headers/1000")
                                   .arg(observeHttpPort)),
                          requestId));
        req.setHttpVersion(httpVersion);
        req.setTimeoutConfig(timeout);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), NetworkError::ConnectionTimeout);

        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        deleteReplyLater(reply);
        return;
    }

    if (caseId == QStringLiteral("p1_timeout_low_speed")) {
        QVERIFY(observeHttpPort > 0);

        QCNetworkTimeoutConfig timeout;
        timeout.lowSpeedTime  = std::chrono::seconds(2);
        timeout.lowSpeedLimit = 1024;

        QCNetworkRequest req(
            withRequestId(QUrl(QStringLiteral("http://localhost:%1/stall_body/8192/5000")
                                   .arg(observeHttpPort)),
                          requestId));
        req.setHttpVersion(httpVersion);
        req.setTimeoutConfig(timeout);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), NetworkError::ConnectionTimeout);

        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        deleteReplyLater(reply);
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

        QCNetworkRequest req(
            withRequestId(QUrl(QStringLiteral("http://localhost:%1/slow_body/8192/4096/5000")
                                   .arg(observeHttpPort)),
                          requestId));
        req.setHttpVersion(httpVersion);

        QCNetworkReply *reply = manager.sendGet(req);
        QVERIFY(reply);

        QByteArray received;
        bool cancelRequested    = false;
        bool cancelledEmitted   = false;
        bool finishedEmitted    = false;
        int postCancelReadyRead = 0;
        int postCancelProgress  = 0;

        connect(reply, &QCNetworkReply::readyRead, this, [&, reply]() {
            if (cancelRequested) {
                ++postCancelReadyRead;
            }
            const auto dataOpt = reply->readAll();
            if (dataOpt.has_value() && !dataOpt->isEmpty()) {
                received.append(*dataOpt);
            }
        });

        connect(reply,
                &QCNetworkReply::downloadProgress,
                this,
                [&, reply](qint64 bytesReceived, qint64 /*bytesTotal*/) {
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
        QVERIFY(finishedEmitted);
        QCOMPARE(reply->error(), NetworkError::OperationCancelled);
        QVERIFY(postCancelReadyRead == 0);
        QVERIFY(postCancelProgress == 0);

        // 兜底：取消后仍可能有少量 buffer 未消费（按当前 readAll 语义应返回 empty/残留字节）
        const auto tailOpt = reply->readAll();
        if (tailOpt.has_value() && !tailOpt->isEmpty()) {
            received.append(*tailOpt);
        }

        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               received,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        reply->deleteLater();
        return;
    }

    if (caseId == QStringLiteral("ext_speed_limit_smoke")) {
        QVERIFY(observeHttpPort > 0);

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        timer.start(20000);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

        const qint64 cancelAtBytes = 32768;
        const qint64 maxRecvSpeed  = 65536;
        const qint64 maxSendSpeed  = 65536;

        QCNetworkRequest req(
            withRequestId(QUrl(QStringLiteral("http://localhost:%1/slow_body/131072/4096/50")
                                   .arg(observeHttpPort)),
                          requestId));
        req.setHttpVersion(httpVersion);
        req.setMaxDownloadBytesPerSec(maxRecvSpeed);
        req.setMaxUploadBytesPerSec(maxSendSpeed);

        QCNetworkReply *reply = manager.sendGet(req);
        QVERIFY(reply);

        QByteArray received;
        bool cancelRequested  = false;
        bool cancelledEmitted = false;
        bool finishedEmitted  = false;

        connect(reply, &QCNetworkReply::readyRead, this, [&, reply]() {
            const auto dataOpt = reply->readAll();
            if (dataOpt.has_value() && !dataOpt->isEmpty()) {
                received.append(*dataOpt);
            }
        });

        connect(reply,
                &QCNetworkReply::downloadProgress,
                this,
                [&, reply](qint64 bytesReceived, qint64 /*bytesTotal*/) {
                    if (!cancelRequested && bytesReceived >= cancelAtBytes) {
                        cancelRequested = true;
                        reply->cancel();
                    }
                });

        connect(reply, &QCNetworkReply::cancelled, this, [&]() {
            cancelledEmitted = true;
            loop.quit();
        });

        connect(reply, &QCNetworkReply::finished, this, [&]() {
            finishedEmitted = true;
            loop.quit();
        });

        loop.exec();
        QVERIFY2(timer.isActive(), "timeout waiting for cancellation");
        QVERIFY(cancelledEmitted);
        QVERIFY(finishedEmitted);
        QCOMPARE(reply->error(), NetworkError::OperationCancelled);

        const auto tailOpt = reply->readAll();
        if (tailOpt.has_value() && !tailOpt->isEmpty()) {
            received.append(*tailOpt);
        }

        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               received,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        reply->deleteLater();
        return;
    }

    if (caseId == QStringLiteral("ext_api_reported_status")) {
        QVERIFY(observeHttpPort > 0);
        QVERIFY(observeStatusCode > 0);

        const QUrl url = withRequestId(QUrl(QStringLiteral("http://localhost:%1/status/%2")
                                                .arg(observeHttpPort)
                                                .arg(observeStatusCode)),
                                       requestId);

        QCNetworkRequest req(url);
        req.setHttpVersion(httpVersion);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);

        QJsonObject meta;
        meta.insert(QStringLiteral("httpStatusCode"), reply->httpStatusCode());
        QVERIFY(writeJsonObjectToFile(QStringLiteral("reported_meta.json"), meta));

        deleteReplyLater(reply);
        return;
    }

    if (caseId == QStringLiteral("p2_pause_resume")) {
        QVERIFY(!docname.isEmpty());
        QVERIFY(httpsPort > 0);
        QVERIFY(pauseOffset > 0);

        const QUrl url = withRequestId(
            QUrl(QStringLiteral("https://localhost:%1/%2").arg(httpsPort).arg(docname)), requestId);

        QCNetworkRequest req(url);
        req.setSslConfig(QCNetworkSslConfig::insecureConfig());
        req.setHttpVersion(httpVersion);

        QFile out(QStringLiteral("download_0.data"));
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));

        QCNetworkReply *reply = manager.sendGet(req);
        QVERIFY(reply);

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        timer.start(60000);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

        bool pauseRequested              = false;
        bool pausedActive                = false;
        bool resumeSeen                  = false;
        int pauseCount                   = 0;
        int resumeCount                  = 0;
        int pausedDataEvents             = 0;
        int pausedProgressIncreaseEvents = 0;
        qint64 lastBytesReceived         = 0;
        qint64 pausedBytesReceived       = -1;
        QJsonArray eventSeq;

        QPointer<QCNetworkReply> safeReply(reply);
        bool resumeScheduled = false;

        connect(reply, &QCNetworkReply::stateChanged, this, [&](ReplyState newState) {
            if (newState == ReplyState::Paused) {
                ++pauseCount;
                pausedActive        = true;
                pausedBytesReceived = lastBytesReceived;
                eventSeq.append(QStringLiteral("pause"));
                if (!resumeScheduled) {
                    resumeScheduled = true;
                    QTimer::singleShot(50, this, [safeReply]() {
                        if (safeReply) {
                            safeReply->resumeTransport();
                        }
                    });
                }
                return;
            }
            if (newState == ReplyState::Running) {
                if (pauseCount > 0 && !resumeSeen) {
                    resumeSeen   = true;
                    pausedActive = false;
                    ++resumeCount;
                    eventSeq.append(QStringLiteral("resume"));
                }
                return;
            }
        });

        connect(reply, &QCNetworkReply::readyRead, this, [&]() {
            const auto dataOpt = reply->readAll();
            if (pausedActive && !resumeSeen && dataOpt.has_value() && !dataOpt->isEmpty()) {
                ++pausedDataEvents;
            }
            if (dataOpt.has_value() && !dataOpt->isEmpty()) {
                const qint64 w = out.write(*dataOpt);
                QVERIFY(w == dataOpt->size());
            }
        });

        connect(reply,
                &QCNetworkReply::downloadProgress,
                this,
                [&](qint64 bytesReceived, qint64 /*bytesTotal*/) {
                    lastBytesReceived = bytesReceived;
                    if (pausedActive && !resumeSeen) {
                        if (pausedBytesReceived >= 0 && bytesReceived > pausedBytesReceived) {
                            ++pausedProgressIncreaseEvents;
                            pausedBytesReceived = bytesReceived;
                        }
                    }
                    if (!pauseRequested && bytesReceived >= pauseOffset) {
                        pauseRequested = true;
                        reply->pauseTransport(PauseMode::Recv);
                    }
                });

        connect(reply, &QCNetworkReply::finished, this, [&]() {
            eventSeq.append(QStringLiteral("finished"));
            loop.quit();
        });

        loop.exec();
        QVERIFY2(timer.isActive(), "timeout waiting for pause/resume download");
        QCOMPARE(reply->error(), NetworkError::NoError);

        const auto tailOpt = reply->readAll();
        if (tailOpt.has_value() && !tailOpt->isEmpty()) {
            const qint64 w = out.write(*tailOpt);
            QVERIFY(w == tailOpt->size());
        }
        out.close();

        QJsonObject pr;
        pr.insert(QStringLiteral("pause_offset"), pauseOffset);
        pr.insert(QStringLiteral("pause_count"), pauseCount);
        pr.insert(QStringLiteral("resume_count"), resumeCount);
        pr.insert(QStringLiteral("paused_data_events"), pausedDataEvents);
        pr.insert(QStringLiteral("paused_progress_events"), pausedProgressIncreaseEvents);
        pr.insert(QStringLiteral("event_seq"), eventSeq);
        QVERIFY(writeJsonObjectToFile(QStringLiteral("pause_resume.json"), pr));

        reply->deleteLater();
        return;
    }

    if (caseId == QStringLiteral("p2_pause_resume_strict")) {
        QVERIFY(!docname.isEmpty());
        QVERIFY(httpsPort > 0);
        QVERIFY(pauseOffset > 0);

        int resumeDelayMs = qEnvironmentVariableIntValue("QCURL_LC_RESUME_DELAY_MS");
        if (resumeDelayMs <= 0) {
            resumeDelayMs = 50;
        }

        const QUrl url = withRequestId(
            QUrl(QStringLiteral("https://localhost:%1/%2").arg(httpsPort).arg(docname)), requestId);

        QCNetworkRequest req(url);
        req.setSslConfig(QCNetworkSslConfig::insecureConfig());
        req.setHttpVersion(httpVersion);

        QFile out(QStringLiteral("download_0.data"));
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));

        QCNetworkReply *reply = manager.sendGet(req);
        QVERIFY(reply);

        QElapsedTimer elapsed;
        elapsed.start();

        int nextSeq = 1;
        QJsonArray events;
        qint64 bytesDeliveredTotal = 0;
        qint64 bytesWrittenTotal   = 0;
        bool firstByteRecorded     = false;

        auto record = [&](const QString &type) {
            QJsonObject e;
            e.insert(QStringLiteral("seq"), nextSeq++);
            e.insert(QStringLiteral("t_us"), static_cast<qint64>(elapsed.nsecsElapsed() / 1000));
            e.insert(QStringLiteral("type"), type);
            e.insert(QStringLiteral("bytes_delivered_total"), bytesDeliveredTotal);
            e.insert(QStringLiteral("bytes_written_total"), bytesWrittenTotal);
            events.append(e);
        };
        record(QStringLiteral("start"));

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        timer.start(60000);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

        bool pauseRequested            = false;
        bool pauseStateSeen            = false;
        bool pauseEffectiveRecorded    = false;
        bool pauseQuiescentOnce        = false;
        qint64 bytesWrittenAtLastCheck = -1;

        bool resumeRequested         = false;
        bool resumeEffectiveRecorded = false;
        bool resumeScheduled         = false;

        QPointer<QCNetworkReply> safeReply(reply);

        auto drainToFile = [&]() {
            while (true) {
                const auto dataOpt = reply->readAll();
                if (!dataOpt.has_value() || dataOpt->isEmpty()) {
                    break;
                }
                bytesDeliveredTotal += dataOpt->size();
                const qint64 w = out.write(*dataOpt);
                QVERIFY(w == dataOpt->size());
                bytesWrittenTotal += w;
                if (!firstByteRecorded) {
                    firstByteRecorded = true;
                    record(QStringLiteral("first_byte"));
                }
            }
        };

        std::function<void()> checkPauseEffective;
        checkPauseEffective = [&]() {
            if (!safeReply || pauseEffectiveRecorded || !pauseStateSeen) {
                return;
            }

            drainToFile();

            // PauseEffective 定义（语义合同边界）：
            // - 已进入 Paused 状态
            // - 已消费/写盘所有已交付数据（bytesAvailable=0）
            // - 连续两个 event loop tick 内写盘累计不再增长（吸收 PauseReq→PauseEffective 的收尾交付/写盘）
            if (safeReply->bytesAvailable() > 0) {
                QTimer::singleShot(0, this, checkPauseEffective);
                return;
            }
            if (bytesWrittenAtLastCheck < 0) {
                bytesWrittenAtLastCheck = bytesWrittenTotal;
                QTimer::singleShot(0, this, checkPauseEffective);
                return;
            }
            if (bytesWrittenTotal != bytesWrittenAtLastCheck) {
                bytesWrittenAtLastCheck = bytesWrittenTotal;
                pauseQuiescentOnce      = false;
                QTimer::singleShot(0, this, checkPauseEffective);
                return;
            }
            if (!pauseQuiescentOnce) {
                pauseQuiescentOnce = true;
                QTimer::singleShot(0, this, checkPauseEffective);
                return;
            }

            pauseEffectiveRecorded = true;
            record(QStringLiteral("pause_effective"));

            if (!resumeScheduled) {
                resumeScheduled = true;
                QTimer::singleShot(resumeDelayMs, this, [&, safeReply]() {
                    if (!safeReply) {
                        return;
                    }
                    if (resumeRequested) {
                        return;
                    }
                    resumeRequested = true;
                    record(QStringLiteral("resume_req"));
                    safeReply->resumeTransport();
                });
            }
        };

        connect(reply, &QCNetworkReply::stateChanged, this, [&](ReplyState newState) {
            if (newState == ReplyState::Paused) {
                pauseStateSeen = true;
                QTimer::singleShot(0, this, checkPauseEffective);
                return;
            }
            if (newState == ReplyState::Running) {
                if (resumeRequested && !resumeEffectiveRecorded) {
                    resumeEffectiveRecorded = true;
                    record(QStringLiteral("resume_effective"));
                }
                return;
            }
        });

        connect(reply, &QCNetworkReply::readyRead, this, [&]() { drainToFile(); });

        connect(reply,
                &QCNetworkReply::downloadProgress,
                this,
                [&](qint64 bytesReceived, qint64 /*bytesTotal*/) {
                    if (!pauseRequested && bytesReceived >= pauseOffset) {
                        pauseRequested = true;
                        record(QStringLiteral("pause_req"));
                        reply->pauseTransport(PauseMode::Recv);
                    }
                });

        connect(reply, &QCNetworkReply::finished, this, [&]() {
            drainToFile();
            record(QStringLiteral("finished"));
            loop.quit();
        });

        loop.exec();
        QVERIFY2(timer.isActive(), "timeout waiting for strict pause/resume download");
        QCOMPARE(reply->error(), NetworkError::NoError);

        out.close();

        QJsonObject root;
        root.insert(QStringLiteral("schema"), QStringLiteral("qcurl-lc/pause-resume@v1"));
        root.insert(QStringLiteral("proto"), proto);
        root.insert(QStringLiteral("url"), url.toString());
        root.insert(QStringLiteral("pause_offset"), pauseOffset);
        root.insert(QStringLiteral("resume_delay_ms"), resumeDelayMs);
        QJsonObject result;
        result.insert(QStringLiteral("qcurl_error"), static_cast<int>(reply->error()));
        root.insert(QStringLiteral("result"), result);
        root.insert(QStringLiteral("events"), events);
        QVERIFY(writeJsonObjectToFile(QStringLiteral("pause_resume_events.json"), root));

        reply->deleteLater();
        return;
    }

    if (caseId == QStringLiteral("p2_backpressure_contract")) {
        QVERIFY(!docname.isEmpty());
        QVERIFY(httpsPort > 0);
        QVERIFY(bpLimitBytes > 0);
        QVERIFY(bpResumeBytes > 0);
        QVERIFY(bpResumeBytes < bpLimitBytes);

        const QUrl url = withRequestId(
            QUrl(QStringLiteral("https://localhost:%1/%2").arg(httpsPort).arg(docname)), requestId);

        QCNetworkRequest req(url);
        req.setSslConfig(QCNetworkSslConfig::insecureConfig());
        req.setHttpVersion(httpVersion);
        req.setBackpressureLimitBytes(bpLimitBytes);
        req.setBackpressureResumeBytes(bpResumeBytes);

        QFile out(QStringLiteral("download_0.data"));
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));

        QCNetworkReply *reply = manager.sendGet(req);
        QVERIFY(reply);

        QJsonArray eventSeq;
        bool sawOn  = false;
        bool sawOff = false;

        QPointer<QCNetworkReply> safeReply(reply);

        auto drainToFile = [&]() {
            if (!safeReply) {
                return;
            }
            while (true) {
                const auto dataOpt = safeReply->readAll();
                if (!dataOpt.has_value() || dataOpt->isEmpty()) {
                    break;
                }
                const qint64 w = out.write(*dataOpt);
                QVERIFY(w == dataOpt->size());
            }
        };

        connect(reply,
                &QCNetworkReply::backpressureStateChanged,
                this,
                [&](bool active, qint64 /*bufferedBytes*/, qint64 /*limitBytes*/) {
                    if (active && !sawOn) {
                        sawOn = true;
                        eventSeq.append(QStringLiteral("bp_on"));
                        QTimer::singleShot(0, this, drainToFile);
                        return;
                    }
                    if (!active && sawOn && !sawOff) {
                        sawOff = true;
                        eventSeq.append(QStringLiteral("bp_off"));
                        return;
                    }
                });

        connect(reply, &QCNetworkReply::readyRead, this, [&]() {
            if (!sawOn) {
                return;
            }
            drainToFile();
        });

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        timer.start(60000);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

        connect(reply, &QCNetworkReply::finished, this, [&]() {
            drainToFile();
            loop.quit();
        });

        loop.exec();
        QVERIFY2(timer.isActive(), "timeout waiting for backpressure contract download");
        QCOMPARE(reply->error(), NetworkError::NoError);
        QVERIFY(sawOn);
        QVERIFY(sawOff);

        out.close();

        QJsonObject root;
        root.insert(QStringLiteral("schema"), QStringLiteral("qcurl-lc/backpressure@v1"));
        root.insert(QStringLiteral("proto"), proto);
        root.insert(QStringLiteral("url"), url.toString());
        root.insert(QStringLiteral("limit_bytes"), bpLimitBytes);
        root.insert(QStringLiteral("resume_bytes"), bpResumeBytes);
        root.insert(QStringLiteral("curl_max_write_size"), static_cast<qint64>(CURL_MAX_WRITE_SIZE));
        root.insert(QStringLiteral("peak_buffered_bytes"),
                    static_cast<qint64>(reply->backpressureBufferedBytesPeak()));
        root.insert(QStringLiteral("event_seq"), eventSeq);
        QJsonObject result;
        result.insert(QStringLiteral("qcurl_error"), static_cast<int>(reply->error()));
        root.insert(QStringLiteral("result"), result);
        QVERIFY(writeJsonObjectToFile(QStringLiteral("backpressure_events.json"), root));

        reply->deleteLater();
        return;
    }

    if (caseId == QStringLiteral("p2_upload_readfunc_pause_resume")) {
        QVERIFY(observeHttpPort > 0);
        QVERIFY(uploadSize > 0);

        class ChunkedUploadDevice final : public QIODevice
        {
        public:
            explicit ChunkedUploadDevice(QObject *parent = nullptr)
                : QIODevice(parent)
            {}

            void appendChunk(const QByteArray &chunk)
            {
                if (chunk.isEmpty()) {
                    return;
                }
                m_buffer.append(chunk);
                emit readyRead();
            }

            void markFinished()
            {
                m_finished = true;
                emit readyRead();
            }

            [[nodiscard]] int zeroReadCount() const { return m_zeroReads; }

            bool isSequential() const override { return true; }

            qint64 bytesAvailable() const override
            {
                return static_cast<qint64>(m_buffer.size()) + QIODevice::bytesAvailable();
            }

        protected:
            qint64 readData(char *data, qint64 maxlen) override
            {
                if (maxlen <= 0) {
                    return 0;
                }
                if (m_buffer.isEmpty()) {
                    if (!m_finished) {
                        ++m_zeroReads;
                    }
                    return 0;
                }
                const qint64 n = std::min<qint64>(maxlen, m_buffer.size());
                std::memcpy(data, m_buffer.constData(), static_cast<size_t>(n));
                m_buffer.remove(0, static_cast<int>(n));
                return n;
            }

            qint64 writeData(const char *, qint64) override { return -1; }

            bool atEnd() const override { return m_finished && m_buffer.isEmpty(); }

        private:
            QByteArray m_buffer;
            bool m_finished = false;
            int m_zeroReads = 0;
        };

        const QByteArray payload(uploadSize, 'u');

        ChunkedUploadDevice device;
        QVERIFY(device.open(QIODevice::ReadOnly));

        const QByteArray chunk1 = payload.left(4096);
        const QByteArray chunk2 = payload.mid(4096, 4096);
        const QByteArray chunk3 = payload.mid(8192);

        device.appendChunk(chunk1);
        QTimer::singleShot(200, this, [&]() { device.appendChunk(chunk2); });
        QTimer::singleShot(400, this, [&]() {
            device.appendChunk(chunk3);
            device.markFinished();
        });

        const QUrl url
            = withRequestId(QUrl(QStringLiteral("http://localhost:%1/method").arg(observeHttpPort)),
                            requestId);

        QCNetworkRequest req(url);
        req.setHttpVersion(httpVersion);
        req.setUploadDevice(&device, static_cast<qint64>(uploadSize));

        auto *reply = manager.sendPut(req, QByteArray());
        QVERIFY(reply);

        QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
        QSignalSpy stateSpy(reply, &QCNetworkReply::stateChanged);
        QSignalSpy sendPausedSpy(reply, &QCNetworkReply::uploadSendPausedChanged);

        QVERIFY(waitForSpyCountAtLeast(finishedSpy, 1, 15000));
        QCOMPARE(reply->error(), NetworkError::NoError);

        for (const auto &args : stateSpy) {
            QVERIFY(!args.isEmpty());
            const ReplyState st = static_cast<ReplyState>(args.at(0).toInt());
            QVERIFY(st != ReplyState::Paused);
        }

        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QCOMPARE(dataOpt.value(), payload);
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               dataOpt.value(),
                               QIODevice::WriteOnly | QIODevice::Truncate));

        QJsonObject root;
        root.insert(QStringLiteral("schema"), QStringLiteral("qcurl-lc/upload-pause-resume@v1"));
        root.insert(QStringLiteral("proto"), proto);
        root.insert(QStringLiteral("url"), url.toString());
        root.insert(QStringLiteral("payload_size"), uploadSize);
        root.insert(QStringLiteral("zero_read_count"), device.zeroReadCount());
        QJsonArray seq;
        bool pauseSeen  = false;
        bool resumeSeen = false;
        for (const auto &args : sendPausedSpy) {
            QVERIFY(!args.isEmpty());
            const bool paused = args.at(0).toBool();
            if (paused) {
                pauseSeen = true;
            } else if (pauseSeen) {
                resumeSeen = true;
            }
        }
        if (pauseSeen) {
            seq.append(QStringLiteral("pause"));
        }
        if (resumeSeen) {
            seq.append(QStringLiteral("resume"));
        }
        root.insert(QStringLiteral("event_seq"), seq);
        QJsonObject result;
        result.insert(QStringLiteral("qcurl_error"), static_cast<int>(reply->error()));
        root.insert(QStringLiteral("result"), result);
        QVERIFY(writeJsonObjectToFile(QStringLiteral("upload_pause_resume.json"), root));

        QVERIFY(device.zeroReadCount() > 0);
        QVERIFY(pauseSeen);
        QVERIFY(resumeSeen);

        reply->deleteLater();
        return;
    }

    if (caseId == QStringLiteral("p1_login_cookie_flow")) {
        QVERIFY(observeHttpPort > 0);
        QVERIFY(!cookiePath.isEmpty());

        manager.setCookieFilePath(cookiePath, QCNetworkAccessManager::ReadWrite);
        QCNetworkRequest req(
            withRequestId(QUrl(QStringLiteral("http://localhost:%1/login").arg(observeHttpPort)),
                          requestId));
        req.setHttpVersion(httpVersion);
        req.setFollowLocation(true);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), NetworkError::NoError);
        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        deleteReplyLater(reply);
        return;
    }

    if (caseId == QStringLiteral("p1_cookie_path_match_redirect")) {
        QVERIFY(observeHttpPort > 0);
        QVERIFY(!cookiePath.isEmpty());

        manager.setCookieFilePath(cookiePath, QCNetworkAccessManager::ReadWrite);
        QCNetworkRequest req(
            withRequestId(QUrl(QStringLiteral("http://localhost:%1/login_path").arg(observeHttpPort)),
                          requestId));
        req.setHttpVersion(httpVersion);
        req.setFollowLocation(true);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), NetworkError::NoError);
        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        deleteReplyLater(reply);
        return;
    }

    if (caseId == QStringLiteral("p2_cookie_request_header")) {
        QVERIFY(observeHttpPort > 0);
        QVERIFY(!cookiePath.isEmpty());

        manager.setCookieFilePath(cookiePath, QCNetworkAccessManager::ReadOnly);
        const QUrl url
            = withRequestId(QUrl(QStringLiteral("http://localhost:%1/cookie").arg(observeHttpPort)),
                            requestId);

        QCNetworkRequest req(url);
        req.setHttpVersion(httpVersion);
        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QCOMPARE(reply->error(), NetworkError::NoError);
        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        deleteReplyLater(reply);
        return;
    }

    if (caseId == QStringLiteral("p2_share_handle_cookie_disabled")) {
        QVERIFY(observeHttpPort > 0);

        auto waitForFinished = [](QCNetworkReply *reply, int timeoutMs) -> bool {
            QEventLoop loop;
            QTimer timer;
            timer.setSingleShot(true);
            QObject::connect(reply, &QCNetworkReply::finished, &loop, &QEventLoop::quit);
            QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
            timer.start(timeoutMs);
            loop.exec();
            return reply->isFinished();
        };

        QCNetworkRequest loginReq(
            withRequestId(QUrl(QStringLiteral("http://localhost:%1/login").arg(observeHttpPort)),
                          requestId));
        loginReq.setFollowLocation(false);
        loginReq.setHttpVersion(httpVersion);
        QCNetworkReply *loginReply = manager.sendGet(loginReq);
        QVERIFY(loginReply);
        QVERIFY(waitForFinished(loginReply, 5000));
        QCOMPARE(loginReply->error(), NetworkError::NoError);
        loginReply->deleteLater();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

        QCNetworkRequest homeReq(
            withRequestId(QUrl(QStringLiteral("http://localhost:%1/home").arg(observeHttpPort)),
                          requestId));
        homeReq.setFollowLocation(false);
        homeReq.setHttpVersion(httpVersion);
        QCNetworkReply *homeReply = manager.sendGet(homeReq);
        QVERIFY(homeReply);
        QVERIFY(waitForFinished(homeReply, 5000));
        QVERIFY(homeReply->error() != NetworkError::NoError);
        const auto dataOpt    = homeReply->readAll();
        const QByteArray body = dataOpt.value_or(QByteArray());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               body,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        homeReply->deleteLater();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return;
    }

    if (caseId == QStringLiteral("p2_share_handle_cookie_enabled")) {
        QVERIFY(observeHttpPort > 0);
        QVERIFY2(!shareHandleEnv.isEmpty(),
                 "QCURL_LC_SHARE_HANDLE required for share cookie enabled case");

        auto waitForFinished = [](QCNetworkReply *reply, int timeoutMs) -> bool {
            QEventLoop loop;
            QTimer timer;
            timer.setSingleShot(true);
            QObject::connect(reply, &QCNetworkReply::finished, &loop, &QEventLoop::quit);
            QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
            timer.start(timeoutMs);
            loop.exec();
            return reply->isFinished();
        };

        QCNetworkRequest loginReq(
            withRequestId(QUrl(QStringLiteral("http://localhost:%1/login").arg(observeHttpPort)),
                          requestId));
        loginReq.setFollowLocation(false);
        loginReq.setHttpVersion(httpVersion);
        QCNetworkReply *loginReply = manager.sendGet(loginReq);
        QVERIFY(loginReply);
        QVERIFY(waitForFinished(loginReply, 5000));
        QCOMPARE(loginReply->error(), NetworkError::NoError);
        loginReply->deleteLater();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

        QCNetworkRequest homeReq(
            withRequestId(QUrl(QStringLiteral("http://localhost:%1/home").arg(observeHttpPort)),
                          requestId));
        homeReq.setFollowLocation(false);
        homeReq.setHttpVersion(httpVersion);
        QCNetworkReply *homeReply = manager.sendGet(homeReq);
        QVERIFY(homeReply);
        QVERIFY(waitForFinished(homeReply, 5000));
        QCOMPARE(homeReply->error(), NetworkError::NoError);
        const auto dataOpt = homeReply->readAll();
        QVERIFY(dataOpt.has_value());
        QCOMPARE(*dataOpt, QByteArray("home-ok\n"));
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        homeReply->deleteLater();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return;
    }

    if (caseId == QStringLiteral("p2_share_handle_cookie_concurrency")) {
        QVERIFY(observeHttpPort > 0);
        QVERIFY2(!shareHandleEnv.isEmpty(),
                 "QCURL_LC_SHARE_HANDLE required for share cookie concurrency case");

        auto waitForFinished = [](QCNetworkReply *reply, int timeoutMs) -> bool {
            QEventLoop loop;
            QTimer timer;
            timer.setSingleShot(true);
            QObject::connect(reply, &QCNetworkReply::finished, &loop, &QEventLoop::quit);
            QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
            timer.start(timeoutMs);
            loop.exec();
            return reply->isFinished();
        };

        QCNetworkRequest loginReq(
            withRequestId(QUrl(QStringLiteral("http://localhost:%1/login").arg(observeHttpPort)),
                          requestId));
        loginReq.setFollowLocation(false);
        loginReq.setHttpVersion(httpVersion);
        QCNetworkReply *loginReply = manager.sendGet(loginReq);
        QVERIFY(loginReply);
        QVERIFY(waitForFinished(loginReply, 5000));
        QCOMPARE(loginReply->error(), NetworkError::NoError);
        loginReply->deleteLater();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

        constexpr int kConcurrency = 64;
        QVector<QPointer<QCNetworkReply>> replies;
        replies.reserve(kConcurrency);

        int finishedCount = 0;
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(30000);

        for (int i = 0; i < kConcurrency; ++i) {
            QUrl url(QStringLiteral("http://localhost:%1/home").arg(observeHttpPort));
            QUrlQuery q(url);
            q.addQueryItem(QStringLiteral("seq"), QString::number(i));
            url.setQuery(q);

            QCNetworkRequest req(withRequestId(url, requestId));
            req.setFollowLocation(false);
            req.setHttpVersion(httpVersion);

            QCNetworkReply *reply = manager.sendGet(req);
            QVERIFY(reply);
            replies.append(reply);
            QObject::connect(reply, &QCNetworkReply::finished, this, [&finishedCount, &loop]() {
                finishedCount += 1;
                if (finishedCount >= kConcurrency) {
                    loop.quit();
                }
            });
        }

        loop.exec();
        QVERIFY2(timer.isActive(), "timeout waiting for share handle concurrency requests");
        QCOMPARE(finishedCount, kConcurrency);
        for (const auto &r : replies) {
            QVERIFY(r);
            QVERIFY(r->isFinished());
            QCOMPARE(r->error(), NetworkError::NoError);
            r->deleteLater();
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return;
    }

    if (caseId == QStringLiteral("p2_fixed_http_error")) {
        QVERIFY(observeHttpPort > 0);
        QVERIFY(observeStatusCode > 0);

        const QUrl url = withRequestId(QUrl(QStringLiteral("http://localhost:%1/status/%2")
                                                .arg(observeHttpPort)
                                                .arg(observeStatusCode)),
                                       requestId);

        QCNetworkRequest req(url);
        req.setHttpVersion(httpVersion);
        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QVERIFY(reply->error() != NetworkError::NoError);
        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        deleteReplyLater(reply);
        return;
    }

    if (caseId == QStringLiteral("p2_retry_501_sequence")) {
        QVERIFY(observeHttpPort > 0);

        const QUrl url = withRequestId(QUrl(QStringLiteral("http://localhost:%1/status/501")
                                                .arg(observeHttpPort)),
                                       requestId);

        QCNetworkRetryPolicy policy;
        policy.maxRetries   = 1;
        policy.initialDelay = std::chrono::milliseconds(1);
        policy.maxDelay     = std::chrono::milliseconds(1);

        QCNetworkRequest req(url);
        req.setHttpVersion(httpVersion);
        req.setRetryPolicy(policy);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        QVERIFY(reply->error() != NetworkError::NoError);
        const auto dataOpt = reply->readAll();
        QVERIFY(dataOpt.has_value());
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        deleteReplyLater(reply);
        return;
    }

    if (caseId == QStringLiteral("p2_error_proxy_407")) {
        QVERIFY(proxyPort > 0);
        QVERIFY(!proxyTargetUrl.isEmpty());

        QCNetworkRequest req{QUrl(proxyTargetUrl)};
        req.setHttpVersion(httpVersion);

        QCNetworkProxyConfig proxy;
        proxy.type     = QCNetworkProxyConfig::ProxyType::Http;
        proxy.hostName = QStringLiteral("localhost");
        proxy.port     = proxyPort;
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
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               *dataOpt,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        deleteReplyLater(reply);
        return;
    }

    if (caseId == QStringLiteral("proxy_http_basic_auth")
        || caseId == QStringLiteral("proxy_https_connect_basic_auth")
        || caseId == QStringLiteral("proxy_connect_headers_1941")) {
        QVERIFY(proxyPort > 0);
        QVERIFY(!proxyUser.isEmpty());
        QVERIFY(!proxyPass.isEmpty());
        QVERIFY(!proxyTargetUrl.isEmpty());

        QCNetworkRequest req{QUrl(proxyTargetUrl)};
        req.setSslConfig(QCNetworkSslConfig::insecureConfig());
        req.setHttpVersion(httpVersion);

        QCNetworkProxyConfig proxy;
        proxy.type     = QCNetworkProxyConfig::ProxyType::Http;
        proxy.hostName = QStringLiteral("localhost");
        proxy.port     = proxyPort;
        proxy.userName = proxyUser;
        proxy.password = proxyPass;
        req.setProxyConfig(proxy);

        const QString outFile = QStringLiteral("download_0.data");
        QFile out(outFile);
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));

        QCNetworkReply reply(req, HttpMethod::Get, ExecutionMode::Sync, QByteArray(), &manager);
        reply.setWriteCallback([&](char *buffer, size_t size) -> size_t {
            const qint64 want = static_cast<qint64>(size);
            const qint64 w    = out.write(buffer, want);
            if (w != want) {
                return 0;
            }
            return size;
        });
        reply.execute();
        out.close();

        QCOMPARE(reply.error(), NetworkError::NoError);

        // 诊断型采集：尽力落盘 CONNECT 阶段 header blocks（缺失/差异不作为门禁失败条件）
        const QByteArray headerData = reply.rawHeaderData();
        (void)writeAllToFile(QStringLiteral("response_headers_0.data"),
                             headerData,
                             QIODevice::WriteOnly | QIODevice::Truncate);

        if (caseId != QStringLiteral("proxy_http_basic_auth")) {
            QByteArray connectDump;
            const QList<QByteArray> lines = headerData.split('\n');
            QByteArray block;
            bool inBlock   = false;
            bool isConnect = false;
            for (QByteArray line : lines) {
                if (line.endsWith('\r')) {
                    line.chop(1);
                }
                if (line.startsWith("HTTP/")) {
                    block.clear();
                    inBlock              = true;
                    const QByteArray low = line.toLower();
                    isConnect            = low.contains("connection established");
                    block.append(line);
                    block.append('\n');
                    continue;
                }
                if (!inBlock) {
                    continue;
                }
                if (line.isEmpty()) {
                    if (isConnect && !block.isEmpty()) {
                        connectDump.append(block);
                        connectDump.append('\n');
                    }
                    inBlock   = false;
                    isConnect = false;
                    block.clear();
                    continue;
                }
                block.append(line);
                block.append('\n');
            }
            (void)writeAllToFile(QStringLiteral("connect_headers_0.data"),
                                 connectDump,
                                 QIODevice::WriteOnly | QIODevice::Truncate);
        }

        QFile f(outFile);
        QVERIFY(f.exists());
        QVERIFY(f.size() > 0);
        return;
    }

    if (caseId == QStringLiteral("download_serial_resume")
        || caseId == QStringLiteral("download_parallel_resume")) {
        QVERIFY(!docname.isEmpty());
        for (int i = 0; i < count; ++i) {
            const QUrl url         = withRequestId(QUrl(QStringLiteral("https://localhost:%1/%2")
                                                    .arg(httpsPort)
                                                    .arg(docname)),
                                           requestId);
            const QString outFile  = QStringLiteral("download_%1.data").arg(i);
            const NetworkError err = httpMethodToFile(manager,
                                                      HttpMethod::Get,
                                                      url,
                                                      httpVersion,
                                                      QByteArray(),
                                                      outFile);
            QCOMPARE(err, NetworkError::NoError);
        }
        return;
    }

    if (caseId == QStringLiteral("ext_tls_policy_and_cache")) {
        QVERIFY(httpsPort > 0);
        QVERIFY(!caCertPath.isEmpty());
        QVERIFY2(!hstsPath.isEmpty() || !altSvcPath.isEmpty(),
                 "QCURL_LC_HSTS_PATH or QCURL_LC_ALTSVC_PATH required");

        QCNetworkSslConfig ssl = QCNetworkSslConfig::defaultConfig();
        ssl.caCertPath         = caCertPath;

        QCNetworkRequest req(
            withRequestId(QUrl(QStringLiteral("https://localhost:%1/lc_cache_headers")
                                   .arg(httpsPort)),
                          requestId));
        req.setSslConfig(ssl);
        req.setHttpVersion(httpVersion);
        req.setFollowLocation(false);

        auto *reply = manager.sendGetSync(req);
        QVERIFY(reply);
        const QStringList warnings = reply->capabilityWarnings();
        QCOMPARE(reply->error(), NetworkError::NoError);
        const auto dataOpt = reply->readAll();
        if (dataOpt.has_value()) {
            QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                                   *dataOpt,
                                   QIODevice::WriteOnly | QIODevice::Truncate));
        }
        deleteReplyLater(reply);

        [[maybe_unused]] auto shouldSkipByOpt = [&warnings](const QString &opt) -> bool {
            for (const QString &w : warnings) {
                if (w.contains(opt)) {
                    return true;
                }
            }
            return false;
        };

        if (!hstsPath.isEmpty()) {
            QFile f(hstsPath);
            if (!f.exists() || f.size() <= 0) {
                if (shouldSkipByOpt(QStringLiteral("CURLOPT_HSTS"))) {
                    QSKIP("capability gated: CURLOPT_HSTS unsupported");
                }
            }
            QVERIFY2(f.exists(), "HSTS cache file missing");
            QVERIFY2(f.size() > 0, "HSTS cache file empty");
        }

        if (!altSvcPath.isEmpty()) {
            QFile f(altSvcPath);
            if (!f.exists() || f.size() <= 0) {
                if (shouldSkipByOpt(QStringLiteral("CURLOPT_ALTSVC"))) {
                    QSKIP("capability gated: CURLOPT_ALTSVC unsupported");
                }
            }
            QVERIFY2(f.exists(), "Alt-Svc cache file missing");
            QVERIFY2(f.size() > 0, "Alt-Svc cache file empty");
        }
        return;
    }

    if (caseId == QStringLiteral("multi_limits_smoke")) {
        QVERIFY(!docname.isEmpty());
        QVERIFY(httpsPort > 0);

        // 显式配置 multi limits（M3），用于覆盖 curl_multi_setopt + 线程切换路径。
        auto *poolManager                 = QCNetworkConnectionPoolManager::instance();
        QCNetworkConnectionPoolConfig cfg = poolManager->config();
        cfg.multiMaxTotalConnections      = 2;
        cfg.multiMaxHostConnections       = 1;
        cfg.multiMaxConcurrentStreams     = 1;
        cfg.multiMaxConnects              = 16;
        QVERIFY(cfg.isValid());
        poolManager->setConfig(cfg);

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
            const QUrl url       = withRequestId(QUrl(QStringLiteral("https://localhost:%1/%2%3")
                                                    .arg(httpsPort)
                                                    .arg(docname)
                                                    .arg(suffix)),
                                           requestId);

            QCNetworkRequest req(url);
            req.setSslConfig(QCNetworkSslConfig::insecureConfig());
            req.setHttpVersion(httpVersion);

            QCNetworkReply *reply = manager.sendGet(req);
            QVERIFY(reply);
            connect(reply, &QCNetworkReply::finished, this, [&, i, reply]() {
                const QString outFile                = QStringLiteral("download_%1.data").arg(i);
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
        QVERIFY2(timer.isActive(), "timeout waiting for multi-limits smoke downloads");
        for (int i = 0; i < errors.size(); ++i) {
            QCOMPARE(errors[i], NetworkError::NoError);
        }
        return;
    }

    if (caseId == QStringLiteral("ext_multi_get4_h2")
        || caseId == QStringLiteral("ext_multi_get4_h3")) {
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
            const QUrl url       = withRequestId(QUrl(QStringLiteral("https://localhost:%1/%2%3")
                                                    .arg(httpsPort)
                                                    .arg(docname)
                                                    .arg(suffix)),
                                           requestId);

            QCNetworkRequest req(url);
            req.setSslConfig(QCNetworkSslConfig::insecureConfig());
            req.setHttpVersion(httpVersion);

            QCNetworkReply *reply = manager.sendGet(req);
            QVERIFY(reply);
            connect(reply, &QCNetworkReply::finished, this, [&, i, reply]() {
                const QString outFile                = QStringLiteral("download_%1.data").arg(i);
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

    if (caseId == QStringLiteral("lc_soak_parallel_get")) {
        QVERIFY(httpsPort > 0);
        QVERIFY(!docname.isEmpty());

        const QString durationEnv  = qEnvironmentVariable("QCURL_LC_SOAK_DURATION_S");
        const QString parallelEnv  = qEnvironmentVariable("QCURL_LC_SOAK_PARALLEL");
        const QString maxErrorsEnv = qEnvironmentVariable("QCURL_LC_SOAK_MAX_ERRORS");

        const int durationS = durationEnv.isEmpty() ? 300 : durationEnv.toInt();
        const int parallel  = parallelEnv.isEmpty() ? 32 : parallelEnv.toInt();
        const int maxErrors = maxErrorsEnv.isEmpty() ? 0 : maxErrorsEnv.toInt();

        QVERIFY2(durationS > 0, "QCURL_LC_SOAK_DURATION_S invalid");
        QVERIFY2(parallel > 0, "QCURL_LC_SOAK_PARALLEL invalid");
        QVERIFY2(maxErrors >= 0, "QCURL_LC_SOAK_MAX_ERRORS invalid");

        const QUrl url = withRequestId(
            QUrl(QStringLiteral("https://localhost:%1/%2").arg(httpsPort).arg(docname)), requestId);

        QElapsedTimer elapsed;
        elapsed.start();

        QEventLoop loop;
        QTimer stopTimer;
        QTimer hardTimer;
        stopTimer.setSingleShot(true);
        hardTimer.setSingleShot(true);

        bool stopRequested = false;
        bool hardTimeout   = false;

        QObject::connect(&stopTimer, &QTimer::timeout, this, [&]() { stopRequested = true; });
        QObject::connect(&hardTimer, &QTimer::timeout, &loop, [&]() {
            stopRequested = true;
            hardTimeout   = true;
            loop.quit();
        });

        const int durationMs = durationS * 1000;
        stopTimer.start(durationMs);
        hardTimer.start(durationMs + 60000);

        int inFlight      = 0;
        int startedCount  = 0;
        int finishedCount = 0;
        int errorCount    = 0;

        std::function<void()> startOne;
        startOne = [&]() {
            if (stopRequested) {
                return;
            }
            QCNetworkRequest req(url);
            req.setSslConfig(QCNetworkSslConfig::insecureConfig());
            req.setHttpVersion(httpVersion);

            QCNetworkReply *reply = manager.sendGet(req);
            QVERIFY(reply);
            ++inFlight;
            ++startedCount;

            QObject::connect(reply, &QCNetworkReply::finished, this, [&, reply]() {
                const auto dataOpt = reply->readAll();
                QVERIFY(dataOpt.has_value());
                if (reply->error() != NetworkError::NoError) {
                    ++errorCount;
                }
                reply->deleteLater();
                --inFlight;
                ++finishedCount;

                if (!stopRequested && errorCount <= maxErrors) {
                    startOne();
                } else if (inFlight == 0) {
                    loop.quit();
                }
            });
        };

        for (int i = 0; i < parallel; ++i) {
            startOne();
        }

        loop.exec();

        if (hardTimeout) {
            QFAIL("timeout waiting for soak run to complete");
        }

        // stopTimer 可能已触发（期望），但 hardTimer 必须仍然 active。
        QVERIFY2(hardTimer.isActive(), "hard timeout triggered during soak run");
        QVERIFY2(errorCount <= maxErrors, "too many errors during soak run");

        QJsonObject summary;
        summary.insert(QStringLiteral("case_id"), QStringLiteral("lc_soak_parallel_get"));
        summary.insert(QStringLiteral("duration_s"), durationS);
        summary.insert(QStringLiteral("parallel"), parallel);
        summary.insert(QStringLiteral("started"), startedCount);
        summary.insert(QStringLiteral("finished"), finishedCount);
        summary.insert(QStringLiteral("errors"), errorCount);
        summary.insert(QStringLiteral("elapsed_ms"), static_cast<qint64>(elapsed.elapsed()));
        QVERIFY(writeJsonObjectToFile(QStringLiteral("soak_summary.json"), summary));

        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return;
    }

    if (caseId == QStringLiteral("ext_download_parallel_stress")) {
        QVERIFY(!docname.isEmpty());
        QVERIFY(httpsPort > 0);
        const bool perfNoDisk = qEnvironmentVariableIntValue("QCURL_LC_PERF_NO_DISK") != 0;

        const QUrl url = withRequestId(
            QUrl(QStringLiteral("https://localhost:%1/%2").arg(httpsPort).arg(docname)), requestId);

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
                const std::optional<QByteArray> data = reply->readAll();
                QVERIFY(data.has_value());
                QVERIFY(!data->isEmpty());
                if (!perfNoDisk) {
                    const QString outFile = QStringLiteral("download_%1.data").arg(i);
                    QVERIFY(
                        writeAllToFile(outFile, *data, QIODevice::WriteOnly | QIODevice::Truncate));
                }
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
        const QUrl url = withRequestId(QUrl(QStringLiteral("https://localhost:%1/curltest/put")
                                                .arg(httpsPort)),
                                       requestId);
        const QByteArray body = makeUploadBody(uploadSize);
        for (int i = 0; i < count; ++i) {
            const QString outFile = QStringLiteral("download_%1.data").arg(i);
            const NetworkError err
                = httpMethodToFile(manager, HttpMethod::Put, url, httpVersion, body, outFile);
            QCOMPARE(err, NetworkError::NoError);
        }
        return;
    }

    if (caseId == QStringLiteral("upload_post_reuse")) {
        const QUrl url = withRequestId(QUrl(QStringLiteral("https://localhost:%1/curltest/echo")
                                                .arg(httpsPort)),
                                       requestId);
        const QByteArray body = makeUploadBody(uploadSize);
        for (int i = 0; i < count; ++i) {
            const QString outFile = QStringLiteral("download_%1.data").arg(i);
            const NetworkError err
                = httpMethodToFile(manager, HttpMethod::Post, url, httpVersion, body, outFile);
            QCOMPARE(err, NetworkError::NoError);
        }
        return;
    }

    if (caseId == QStringLiteral("postfields_binary_1531")) {
        const QUrl url = withRequestId(QUrl(QStringLiteral("https://localhost:%1/curltest/echo")
                                                .arg(httpsPort)),
                                       requestId);
        QByteArray body;
        body.append(".abc", 4);
        body.append(char('\0'));
        body.append("xyz", 3);

        const QString outFile = QStringLiteral("download_0.data");
        const NetworkError err
            = httpMethodToFile(manager, HttpMethod::Post, url, httpVersion, body, outFile);
        QCOMPARE(err, NetworkError::NoError);

        QFile f(outFile);
        QVERIFY(f.exists());
        QCOMPARE(f.size(), qint64(body.size()));
        return;
    }

    if (caseId == QStringLiteral("cookiejar_1903")) {
        QVERIFY(httpPort > 0);
        QVERIFY(!cookiePath.isEmpty());
        const QUrl url
            = withRequestId(QUrl(QStringLiteral("http://localhost:%1/we/want/1903").arg(httpPort)),
                            requestId);

        manager.setCookieFilePath(cookiePath, QCNetworkAccessManager::ReadOnly);
        {
            QCNetworkRequest req(url);
            req.setHttpVersion(httpVersion);
            auto *reply = manager.sendGetSync(req);
            QVERIFY(reply);
            QCOMPARE(reply->error(), NetworkError::NoError);
            deleteReplyLater(reply);
        }

        manager.setCookieFilePath(cookiePath, QCNetworkAccessManager::ReadWrite);
        {
            QCNetworkRequest req(url);
            req.setHttpVersion(httpVersion);
            auto *reply = manager.sendGetSync(req);
            QVERIFY(reply);
            QCOMPARE(reply->error(), NetworkError::NoError);
            deleteReplyLater(reply);
        }

        QFile f(cookiePath);
        QVERIFY(f.exists());
        QVERIFY(f.size() > 0);
        return;
    }

    if (caseId == QStringLiteral("cookiejar_1920")) {
        QVERIFY(observeHttpPort > 0);
        QVERIFY(!cookiePath.isEmpty());

        const QUrl url = withRequestId(QUrl(QStringLiteral(
                                                "http://localhost:%1/cookie?scenario=1920")
                                                .arg(observeHttpPort)),
                                       requestId);

        manager.setCookieFilePath(cookiePath, QCNetworkAccessManager::ReadWrite);
        {
            QCNetworkRequest req(url);
            req.setHttpVersion(httpVersion);
            auto *reply = manager.sendGetSync(req);
            QVERIFY(reply);
            QCOMPARE(reply->error(), NetworkError::NoError);
            deleteReplyLater(reply);
        }

        QFile f(cookiePath);
        QVERIFY(f.exists());
        QVERIFY(f.size() > 0);
        return;
    }

    if (caseId == QStringLiteral("ws_pingpong_small")) {
        QVERIFY(wsPort > 0);
        const QByteArray payload = QByteArray(125, 'x');

        QCWebSocket ws(
            withRequestId(QUrl(QStringLiteral("ws://localhost:%1/").arg(wsPort)), requestId));
        QSignalSpy connectedSpy(&ws, &QCWebSocket::connected);
        QSignalSpy pongSpy(&ws, &QCWebSocket::pongReceived);
        const int connectedTarget = connectedSpy.count() + 1;
        ws.open();
        QVERIFY(waitForSpyCountAtLeast(connectedSpy, connectedTarget, 5000));
        const int pongTarget = pongSpy.count() + 1;
        ws.ping(payload);
        QVERIFY(waitForSpyCountAtLeast(pongSpy, pongTarget, 5000));
        const QByteArray pongPayload = pongSpy.takeFirst().at(0).toByteArray();
        QCOMPARE(pongPayload, payload);
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               pongPayload,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        ws.close();
        return;
    }

    if (caseId == QStringLiteral("ws_data_small")) {
        QVERIFY(wsPort > 0);
        QCWebSocket ws(
            withRequestId(QUrl(QStringLiteral("ws://localhost:%1/").arg(wsPort)), requestId));
        QSignalSpy connectedSpy(&ws, &QCWebSocket::connected);
        QSignalSpy binSpy(&ws, &QCWebSocket::binaryMessageReceived);
        const int connectedTarget = connectedSpy.count() + 1;
        ws.open();
        QVERIFY(waitForSpyCountAtLeast(connectedSpy, connectedTarget, 5000));

        QByteArray received;
        QByteArray expected;
        const int minLen         = 1;
        const int maxLen         = 10;
        const int repeats        = 2; // 对齐 cli_ws_data 默认 count=1 => 发送/接收 2 次
        const QByteArray pattern = QByteArray("0123456789");

        for (int len = minLen; len <= maxLen; ++len) {
            QByteArray msg;
            msg.reserve(len);
            for (int i = 0; i < len; ++i) {
                msg.append(pattern.at(i % pattern.size()));
            }
            for (int r = 0; r < repeats; ++r) {
                const int binTarget = binSpy.count() + 1;
                ws.sendBinaryMessage(msg);
                QVERIFY(waitForSpyCountAtLeast(binSpy, binTarget, 5000));
                const QByteArray echoed = binSpy.takeFirst().at(0).toByteArray();
                QCOMPARE(echoed, msg);
                received.append(echoed);
                expected.append(msg);
            }
        }

        QCOMPARE(received, expected);
        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               received,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        ws.close();
        return;
    }

    if (caseId == QStringLiteral("ext_ws_ping_2301")) {
        QVERIFY(wsPort > 0);
        const QUrl url
            = withRequestId(QUrl(QStringLiteral("ws://localhost:%1/?scenario=lc_ping").arg(wsPort)),
                            requestId);

        QCWebSocket ws(url);
        ws.setAutoPongEnabled(false);
        QSignalSpy connectedSpy(&ws, &QCWebSocket::connected);
        QSignalSpy pingSpy(&ws, &QCWebSocket::pingReceived);
        QSignalSpy closeSpy(&ws, &QCWebSocket::closeReceived);

        const int connectedTarget = connectedSpy.count() + 1;
        const int pingTarget      = pingSpy.count() + 1;
        const int closeTarget     = closeSpy.count() + 1;
        ws.open();
        QVERIFY(waitForSpyCountAtLeast(connectedSpy, connectedTarget, 5000));
        QVERIFY(waitForSpyCountAtLeast(pingSpy, pingTarget, 5000));
        const QByteArray pingPayload = pingSpy.takeFirst().at(0).toByteArray();
        QCOMPARE(pingPayload.size(), 0);
        ws.pong(pingPayload);

        QVERIFY(waitForSpyCountAtLeast(closeSpy, closeTarget, 5000));
        const QList<QVariant> closeArgs = closeSpy.takeFirst();
        const int closeCode             = closeArgs.at(0).toInt();
        const QString closeReason       = closeArgs.at(1).toString();
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
        const QUrl url
            = withRequestId(QUrl(QStringLiteral("ws://localhost:%1/?scenario=lc_ping").arg(wsPort)),
                            requestId);

        QCWebSocket ws(url);
        ws.setCompressionConfig(QCWebSocketCompressionConfig::defaultConfig());
        ws.setAutoPongEnabled(false);
        QSignalSpy connectedSpy(&ws, &QCWebSocket::connected);
        QSignalSpy pingSpy(&ws, &QCWebSocket::pingReceived);
        QSignalSpy closeSpy(&ws, &QCWebSocket::closeReceived);

        const int connectedTarget = connectedSpy.count() + 1;
        const int pingTarget      = pingSpy.count() + 1;
        const int closeTarget     = closeSpy.count() + 1;
        ws.open();
        QVERIFY(waitForSpyCountAtLeast(connectedSpy, connectedTarget, 5000));
        QVERIFY(waitForSpyCountAtLeast(pingSpy, pingTarget, 5000));
        const QByteArray pingPayload = pingSpy.takeFirst().at(0).toByteArray();
        QCOMPARE(pingPayload.size(), 0);
        ws.pong(pingPayload);

        QVERIFY(waitForSpyCountAtLeast(closeSpy, closeTarget, 5000));
        const QList<QVariant> closeArgs = closeSpy.takeFirst();
        const int closeCode             = closeArgs.at(0).toInt();
        const QString closeReason       = closeArgs.at(1).toString();
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
        const QUrl url = withRequestId(QUrl(QStringLiteral(
                                                "ws://localhost:%1/?scenario=lc_frame_types")
                                                .arg(wsPort)),
                                       requestId);

        QCWebSocket ws(url);
        ws.setAutoPongEnabled(false);
        QSignalSpy connectedSpy(&ws, &QCWebSocket::connected);
        QSignalSpy textSpy(&ws, &QCWebSocket::textMessageReceived);
        QSignalSpy binSpy(&ws, &QCWebSocket::binaryMessageReceived);
        QSignalSpy pingSpy(&ws, &QCWebSocket::pingReceived);
        QSignalSpy pongSpy(&ws, &QCWebSocket::pongReceived);
        QSignalSpy closeSpy(&ws, &QCWebSocket::closeReceived);

        const int connectedTarget = connectedSpy.count() + 1;
        const int textTarget      = textSpy.count() + 1;
        const int binTarget       = binSpy.count() + 1;
        const int pingTarget      = pingSpy.count() + 1;
        const int pongTarget      = pongSpy.count() + 1;
        const int closeTarget     = closeSpy.count() + 1;
        ws.open();
        QVERIFY(waitForSpyCountAtLeast(connectedSpy, connectedTarget, 5000));
        QVERIFY(waitForSpyCountAtLeast(textSpy, textTarget, 5000));
        const QString text = textSpy.takeFirst().at(0).toString();
        QCOMPARE(text, QStringLiteral("txt"));

        QVERIFY(waitForSpyCountAtLeast(binSpy, binTarget, 5000));
        const QByteArray bin = binSpy.takeFirst().at(0).toByteArray();
        QCOMPARE(bin, QByteArrayLiteral("bin"));

        QVERIFY(waitForSpyCountAtLeast(pingSpy, pingTarget, 5000));
        const QByteArray pingPayload = pingSpy.takeFirst().at(0).toByteArray();
        QCOMPARE(pingPayload, QByteArrayLiteral("ping"));
        ws.pong(pingPayload);

        QVERIFY(waitForSpyCountAtLeast(pongSpy, pongTarget, 5000));
        const QByteArray pongPayload = pongSpy.takeFirst().at(0).toByteArray();
        QCOMPARE(pongPayload, QByteArrayLiteral("pong"));

        QVERIFY(waitForSpyCountAtLeast(closeSpy, closeTarget, 5000));
        const QList<QVariant> closeArgs = closeSpy.takeFirst();
        const int closeCode             = closeArgs.at(0).toInt();
        const QString closeReason       = closeArgs.at(1).toString();
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

        const QUrl url = withRequestId(QUrl(QStringLiteral("http://localhost:%1/empty_200")
                                                .arg(observeHttpPort)),
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

        QVERIFY(writeAllToFile(QStringLiteral("download_0.data"),
                               last,
                               QIODevice::WriteOnly | QIODevice::Truncate));
        return;
    }

    if (caseId == QStringLiteral("download_range_resume")) {
        QVERIFY(!docname.isEmpty());
        QVERIFY(abortOffset > 0);
        QVERIFY(fileSize > abortOffset);
        const QUrl url = withRequestId(
            QUrl(QStringLiteral("https://localhost:%1/%2").arg(httpsPort).arg(docname)), requestId);
        const QString outFile = QStringLiteral("download_0.data");

        qint64 firstBytes = 0;
        const NetworkError firstErr
            = httpGetToFile(manager, url, httpVersion, outFile, abortOffset, &firstBytes);
        QVERIFY(firstErr != NetworkError::NoError);
        QVERIFY(firstBytes > 0);

        const NetworkError secondErr
            = httpRangeToFile(manager, url, httpVersion, outFile, abortOffset, fileSize - 1);
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
