/**
 * @file tst_QCNetworkHttp2.cpp
 * @brief QCurl HTTP/2 功能测试 - 验证 HTTP/2 协议支持
 *
 * 测试覆盖：
 * - HTTP/2 协议协商（ALPN）
 * - 多路复用（单连接多请求）
 * - 头部压缩（HPACK）
 * - 协议降级（HTTP/2 → HTTP/1.1）
 * - HTTP/2 over TLS (h2)
 * - 并发流限制
 * - 流控制
 *
 * ============================================================================
 * 测试前准备
 * ============================================================================
 *
 * 1. 确保 libcurl 编译时启用 nghttp2 支持：
 *    curl --version | grep HTTP2
 *
 * 2. 默认使用仓库内置的本地可控 HTTP/2 server（node）：
 *    - 脚本：tests/qcurl/http2-test-server.js
 *    - 证书：tests/qcurl/testdata/http2/localhost.{crt,key}（仅用于测试）
 *
 *    如需对接其他 HTTP/2 server（例如 curl testenv），可通过环境变量覆盖 base URL：
 *    - QCURL_HTTP2_TEST_BASE_URL: 覆盖 HTTP/2 base URL（例如 https://127.0.0.1:PORT）
 *    - QCURL_HTTP2_TEST_HTTP1_BASE_URL: 覆盖 HTTP/1.1-only base URL（用于降级用例）
 *
 *    可选：
 *    - QCURL_HTTP2_DISABLE_DOWNGRADE_TEST=1：禁用“HTTP/2 → HTTP/1.1 降级”用例（仅记录 QWARN）
 *
 * 3. 运行测试：
 *    ./tst_QCNetworkHttp2
 *
 * ============================================================================
 */

#include "QCCurlHandleManager.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkError.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkSslConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QtTest/QtTest>

#include <curl/curl.h>

using namespace QCurl;

// ============================================================================
// 测试服务器配置
// ============================================================================

static const char *kHttp2TestBaseUrlEnvName = "QCURL_HTTP2_TEST_BASE_URL";
static const char *kHttp1TestBaseUrlEnvName = "QCURL_HTTP2_TEST_HTTP1_BASE_URL";
static const char *kDisableDowngradeEnvName = "QCURL_HTTP2_DISABLE_DOWNGRADE_TEST";

namespace {

bool outputSuggestsLocalListenRestriction(const QString &output)
{
    const QString normalized = output.toLower();
    return normalized.contains(QStringLiteral("listen eperm"))
           || normalized.contains(QStringLiteral("operation not permitted"))
           || normalized.contains(QStringLiteral("permission denied"));
}

} // namespace

class TestQCNetworkHttp2 : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // ========== HTTP/2 功能测试 ==========
    void testHttp2Support();           // 检测 HTTP/2 支持
    void testHttp2Negotiation();       // HTTP/2 协议协商（ALPN）
    void testHttp2Multiplexing();      // 多路复用（单连接多请求）
    void testHttp2HeaderCompression(); // 头部压缩（HPACK）
    void testHttp2Downgrade();         // 协议降级到 HTTP/1.1
    void testHttp2WithSsl();           // HTTP/2 over TLS (h2)
    void testHttp2ConcurrentStreams(); // 并发流限制
    void testHttp2ConnectionReuse();   // 连接复用验证

    // ========== HTTP/2 vs HTTP/1.1 对比测试 ==========
    void testHttp2VsHttp1Performance(); // 性能对比（简化版）

private:
    static constexpr const char *kJsonErrorKey = "_qcurl_test_error";

    QCNetworkAccessManager *m_manager = nullptr;

    QProcess m_localServer;
    QString m_h2BaseUrl;
    QString m_http1BaseUrl;
    QString m_localH2BaseUrl;
    QString m_localHttp1BaseUrl;
    QString m_serverError;
    bool m_localServerRestricted = false;
    bool m_disableDowngradeTest = false;

    // 辅助方法
    bool waitForSignal(QObject *obj, const QMetaMethod &signal, int timeout = 10000);
    bool checkHttp2Support(); // 检查 libcurl 是否支持 HTTP/2
    static bool waitForPortReady(quint16 port, int timeoutMs);
    bool startLocalTestServer();
    void stopLocalTestServer();
    QJsonObject requestJson(const QUrl &url,
                            QCNetworkHttpVersion httpVersion,
                            const QList<QPair<QByteArray, QByteArray>> &headers = {},
                            int timeoutMs                                       = 15000);
};

// ============================================================================
// 辅助方法实现
// ============================================================================

bool TestQCNetworkHttp2::waitForSignal(QObject *obj, const QMetaMethod &signal, int timeout)
{
    // 避免 “信号已先触发 -> 新建 QSignalSpy 永远等不到” 的假超时。
    auto *reply = qobject_cast<QCNetworkReply *>(obj);
    if (signal == QMetaMethod::fromSignal(&QCNetworkReply::finished)) {
        if (reply && reply->state() == ReplyState::Finished) {
            return true;
        }
    }

    QSignalSpy spy(obj, signal);
    if (spy.count() > 0) {
        return true;
    }
    if (spy.wait(timeout)) {
        return true;
    }

    if (signal == QMetaMethod::fromSignal(&QCNetworkReply::finished)) {
        return reply && reply->state() == ReplyState::Finished;
    }

    return false;
}

bool TestQCNetworkHttp2::checkHttp2Support()
{
    curl_version_info_data *ver = curl_version_info(CURLVERSION_NOW);
    return (ver->features & CURL_VERSION_HTTP2) != 0;
}

bool TestQCNetworkHttp2::waitForPortReady(quint16 port, int timeoutMs)
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

bool TestQCNetworkHttp2::startLocalTestServer()
{
    stopLocalTestServer();
    m_serverError.clear();
    m_localH2BaseUrl.clear();
    m_localHttp1BaseUrl.clear();
    m_localServerRestricted = false;

    const QString appDir     = QCoreApplication::applicationDirPath();
    const QString scriptPath = QDir(appDir).absoluteFilePath(
        QStringLiteral("../../tests/qcurl/http2-test-server.js"));
    if (!QFileInfo::exists(scriptPath)) {
        m_serverError = QStringLiteral(
            "未找到本地 HTTP/2 测试服务器脚本（tests/qcurl/http2-test-server.js）。");
        return false;
    }

    m_localServer.setProgram(QStringLiteral("node"));
    m_localServer.setArguments({scriptPath,
                                QStringLiteral("--h2-port"),
                                QStringLiteral("0"),
                                QStringLiteral("--http1-port"),
                                QStringLiteral("0")});
    m_localServer.setProcessChannelMode(QProcess::MergedChannels);
    m_localServer.start();

    if (!m_localServer.waitForStarted(2000)) {
        m_serverError = QStringLiteral(
            "无法启动本地 HTTP/2 测试服务器（node）。请确认 node 可用。");
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    QByteArray output;

    QRegularExpression re(QStringLiteral(R"(QCURL_HTTP2_TEST_SERVER_READY\s+(\{.*\})\s*)"));
    while (timer.elapsed() < 5000) {
        if (m_localServer.waitForReadyRead(200)) {
            output += m_localServer.readAll();
            const QString outText               = QString::fromUtf8(output);
            const QRegularExpressionMatch match = re.match(outText);
            if (match.hasMatch()) {
                const QByteArray jsonBytes = match.captured(1).toUtf8();
                QJsonParseError parseError{};
                const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &parseError);
                if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                    m_serverError = QStringLiteral("本地 HTTP/2 测试服务器输出格式错误：%1")
                                        .arg(parseError.errorString());
                    break;
                }

                const QJsonObject obj = doc.object();
                const int h2Port      = obj.value(QStringLiteral("h2Port")).toInt(0);
                const int http1Port   = obj.value(QStringLiteral("http1Port")).toInt(0);
                if (h2Port <= 0 || http1Port <= 0) {
                    m_serverError = QStringLiteral("本地 HTTP/2 测试服务器返回端口无效。");
                    break;
                }

                if (!waitForPortReady(static_cast<quint16>(h2Port), 3000)
                    || !waitForPortReady(static_cast<quint16>(http1Port), 3000)) {
                    m_serverError = QStringLiteral(
                        "本地 HTTP/2 测试服务器未在预期时间内就绪（端口不可连接）。");
                    break;
                }

                m_localH2BaseUrl = QStringLiteral("https://127.0.0.1:%1")
                                       .arg(QString::number(h2Port));
                m_localHttp1BaseUrl = QStringLiteral("https://127.0.0.1:%1")
                                          .arg(QString::number(http1Port));
                return true;
            }
        }
    }

    const QString outText = QString::fromUtf8(output + m_localServer.readAll());
    if (!outText.isEmpty()) {
        qWarning().noquote() << "Local HTTP/2 server output:\n" << outText;
        if (outputSuggestsLocalListenRestriction(outText)) {
            m_localServerRestricted = true;
            m_serverError = QStringLiteral(
                "当前执行环境禁止本地 HTTP/2 测试服务器监听 127.0.0.1");
        }
    }

    stopLocalTestServer();
    if (m_serverError.isEmpty()) {
        m_serverError = QStringLiteral("本地 HTTP/2 测试服务器未在预期时间内输出 READY 信号。");
    }
    return false;
}

void TestQCNetworkHttp2::stopLocalTestServer()
{
    if (m_localServer.state() == QProcess::NotRunning) {
        m_localH2BaseUrl.clear();
        m_localHttp1BaseUrl.clear();
        return;
    }

    m_localServer.terminate();
    if (!m_localServer.waitForFinished(1500)) {
        m_localServer.kill();
        m_localServer.waitForFinished(1500);
    }

    m_localH2BaseUrl.clear();
    m_localHttp1BaseUrl.clear();
}

QJsonObject TestQCNetworkHttp2::requestJson(const QUrl &url,
                                            QCNetworkHttpVersion httpVersion,
                                            const QList<QPair<QByteArray, QByteArray>> &headers,
                                            int timeoutMs)
{
    QCNetworkRequest request(url);
    request.setHttpVersion(httpVersion);
    request.setSslConfig(QCNetworkSslConfig::insecureConfig());

    for (const auto &kv : headers) {
        request.setRawHeader(kv.first, kv.second);
    }

    auto *reply = m_manager->sendGet(request);
    if (!waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), timeoutMs)) {
        const QString err = reply->errorString();
        reply->cancel();
        reply->deleteLater();
        return QJsonObject{
            {QString::fromUtf8(kJsonErrorKey),
             QStringLiteral("请求超时（%1ms）：%2").arg(timeoutMs).arg(err)},
        };
    }

    if (reply->error() != NetworkError::NoError) {
        const QString err = reply->errorString();
        reply->deleteLater();
        return QJsonObject{
            {QString::fromUtf8(kJsonErrorKey), QStringLiteral("请求失败：%1").arg(err)},
        };
    }

    auto data = reply->readAll();
    reply->deleteLater();
    if (!data.has_value()) {
        return QJsonObject{
            {QString::fromUtf8(kJsonErrorKey), QStringLiteral("响应体为空（std::nullopt）")},
        };
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(*data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return QJsonObject{
            {QString::fromUtf8(kJsonErrorKey),
             QStringLiteral("JSON 解析失败：%1").arg(parseError.errorString())},
        };
    }
    if (!doc.isObject()) {
        return QJsonObject{
            {QString::fromUtf8(kJsonErrorKey), QStringLiteral("JSON 顶层必须是 object")},
        };
    }
    return doc.object();
}

// ============================================================================
// 测试初始化
// ============================================================================

void TestQCNetworkHttp2::initTestCase()
{
    // 检查 HTTP/2 支持
    if (!checkHttp2Support()) {
        QSKIP("libcurl 未编译 HTTP/2 支持（需要 nghttp2），跳过所有 HTTP/2 测试");
    }

    m_disableDowngradeTest = !qgetenv(kDisableDowngradeEnvName).trimmed().isEmpty()
                             && qgetenv(kDisableDowngradeEnvName).trimmed() != "0";

    const QString overriddenH2    = QString::fromUtf8(qgetenv(kHttp2TestBaseUrlEnvName)).trimmed();
    const QString overriddenHttp1 = QString::fromUtf8(qgetenv(kHttp1TestBaseUrlEnvName)).trimmed();

    if (!overriddenH2.isEmpty()) {
        m_h2BaseUrl = overriddenH2;
    }
    if (!overriddenHttp1.isEmpty()) {
        m_http1BaseUrl = overriddenHttp1;
    }

    if (m_h2BaseUrl.isEmpty() || m_http1BaseUrl.isEmpty()) {
        if (!startLocalTestServer()) {
            const QString reason
                = QStringLiteral("本地 HTTP/2 测试服务器不可用：%1").arg(m_serverError);
            if (m_localServerRestricted) {
                QSKIP(qPrintable(reason));
            }
            QFAIL(qPrintable(reason));
        }

        if (m_h2BaseUrl.isEmpty()) {
            m_h2BaseUrl = m_localH2BaseUrl;
        }
        if (m_http1BaseUrl.isEmpty()) {
            m_http1BaseUrl = m_localHttp1BaseUrl;
        }
    }

    qDebug() << "HTTP/2 base URL:" << m_h2BaseUrl;
    qDebug() << "HTTP/1.1 base URL:" << m_http1BaseUrl;

    m_manager = new QCNetworkAccessManager(this);
}

void TestQCNetworkHttp2::cleanupTestCase()
{
    stopLocalTestServer();
    m_manager = nullptr;
}

void TestQCNetworkHttp2::init()
{
    // 每个测试前执行
}

void TestQCNetworkHttp2::cleanup()
{
    // 每个测试后执行
}

// ============================================================================
// HTTP/2 功能测试
// ============================================================================

void TestQCNetworkHttp2::testHttp2Support()
{
    // 验证 QCNetworkHttpVersion 枚举包含 HTTP/2
    QVERIFY(static_cast<int>(QCNetworkHttpVersion::Http2) > 0);
    QVERIFY(static_cast<int>(QCNetworkHttpVersion::Http2TLS) > 0);

    // 验证 curl 运行时支持
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
    // 同时发起 5 个请求到同一服务器（HTTP/2 应该复用单个连接）
    QList<QCNetworkReply *> replies;

    for (int i = 0; i < 5; ++i) {
        const QUrl url(m_h2BaseUrl
                       + QStringLiteral("/reqinfo?id=%1&delay_ms=150").arg(QString::number(i)));
        QCNetworkRequest request(url);
        request.setHttpVersion(QCNetworkHttpVersion::Http2TLS);
        request.setSslConfig(QCNetworkSslConfig::insecureConfig());
        auto *reply = m_manager->sendGet(request);
        replies.append(reply);
    }

    // 等待所有请求完成
    for (int i = 0; i < replies.size(); ++i) {
        auto *reply = replies[i];
        if (!waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 30000)) {
            qWarning() << "HTTP/2 Multiplexing timeout on request" << i;
            for (auto *pendingReply : replies) {
                if (pendingReply) {
                    pendingReply->cancel();
                    pendingReply->deleteLater();
                }
            }
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QFAIL("HTTP/2 多路复用测试超时（单请求 30s 未完成）");
        }
    }

    QSet<int> streamIds;
    int sessionId = -1;
    for (int i = 0; i < replies.size(); ++i) {
        auto *reply = replies[i];
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
        QVERIFY2(!streamIds.contains(streamId),
                 qPrintable(QStringLiteral("streamId 重复：%1").arg(streamId)));
        streamIds.insert(streamId);
    }

    for (auto *reply : replies) {
        if (reply) {
            reply->deleteLater();
        }
    }
}

void TestQCNetworkHttp2::testHttp2HeaderCompression()
{
    // 发送带大量自定义 Header 的请求
    QCNetworkRequest request(QUrl(m_h2BaseUrl + QStringLiteral("/reqinfo?case=headers")));
    request.setHttpVersion(QCNetworkHttpVersion::Http2TLS);
    request.setSslConfig(QCNetworkSslConfig::insecureConfig());

    // 添加多个自定义 Header
    for (int i = 0; i < 10; ++i) {
        request.setRawHeader(QStringLiteral("X-Custom-Header-%1").arg(i).toUtf8(),
                             QStringLiteral("Value-%1-With-Long-Content-For-Compression-Test")
                                 .arg(i)
                                 .toUtf8());
    }

    auto *reply = m_manager->sendGet(request);
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
    if (m_disableDowngradeTest) {
        // 证据口径：禁用关键用例不应“静默通过”，避免误读为已验证。
        // 注意：本仓库 ctest 取证式门禁为 skip=fail（见 tests/qcurl/CMakeLists.txt），因此该 QSKIP 代表“无证据”。
        QSKIP(
            "已通过 QCURL_HTTP2_DISABLE_DOWNGRADE_TEST 禁用降级用例（证据门禁口径下视为无证据）。");
    }

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
    // HTTP/2 over TLS (h2) - 标准 HTTPS + HTTP/2
    const QUrl url(m_h2BaseUrl + QStringLiteral("/reqinfo?case=h2tls"));
    const QJsonObject obj = requestJson(url, QCNetworkHttpVersion::Http2TLS);
    QVERIFY2(!obj.contains(QString::fromUtf8(kJsonErrorKey)),
             qPrintable(obj.value(QString::fromUtf8(kJsonErrorKey)).toString()));
    QCOMPARE(obj.value(QStringLiteral("httpVersion")).toString(), QStringLiteral("2.0"));
}

void TestQCNetworkHttp2::testHttp2ConcurrentStreams()
{
    // 测试并发流（HTTP/2 的核心特性）
    // 同时发起 10 个请求，验证它们能并发执行

    QElapsedTimer timer;
    timer.start();

    QList<QCNetworkReply *> replies;
    for (int i = 0; i < 10; ++i) {
        const QUrl url(m_h2BaseUrl
                       + QStringLiteral("/reqinfo?stream=%1&delay_ms=200").arg(QString::number(i)));
        QCNetworkRequest request(url);
        request.setHttpVersion(QCNetworkHttpVersion::Http2TLS);
        request.setSslConfig(QCNetworkSslConfig::insecureConfig());

        auto *reply = m_manager->sendGet(request);
        replies.append(reply);
    }

    // 在慢环境下保留更宽松的等待窗口，避免将并发流验证误判为失败。
    // 等待所有请求完成
    for (int i = 0; i < replies.size(); ++i) {
        auto *reply = replies[i];
        if (!waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 30000)) {
            qWarning() << "HTTP/2 ConcurrentStreams timeout on request" << i;
            for (auto *pendingReply : replies) {
                if (pendingReply) {
                    pendingReply->cancel();
                    pendingReply->deleteLater();
                }
            }
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QFAIL("HTTP/2 并发流测试超时（单请求 30s 未完成）");
        }
    }

    qint64 elapsed = timer.elapsed();

    QSet<int> streamIds;
    int sessionId = -1;
    for (auto *reply : replies) {
        QVERIFY(reply->error() == NetworkError::NoError);
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
    }

    qDebug() << "总耗时：" << elapsed << "ms";

    for (auto *reply : replies) {
        if (reply) {
            reply->deleteLater();
        }
    }
}

void TestQCNetworkHttp2::testHttp2ConnectionReuse()
{
    // 验证连接复用：顺序发送 5 个请求，HTTP/2 应该复用同一连接

    int sessionId = -1;
    QSet<int> streamIds;

    for (int i = 0; i < 5; ++i) {
        QCNetworkRequest request(
            QUrl(m_h2BaseUrl + QStringLiteral("/reqinfo?seq=%1").arg(QString::number(i))));
        request.setHttpVersion(QCNetworkHttpVersion::Http2TLS);
        request.setSslConfig(QCNetworkSslConfig::insecureConfig());

        auto *reply = m_manager->sendGet(request);
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
    const int requestCount = 10;

    // ========== HTTP/1.1 基准测试 ==========
    QElapsedTimer http1Timer;
    http1Timer.start();

    for (int i = 0; i < requestCount; ++i) {
        QCNetworkRequest request(
            QUrl(m_http1BaseUrl + QStringLiteral("/reqinfo?http1=%1").arg(QString::number(i))));
        request.setHttpVersion(QCNetworkHttpVersion::Http1_1);
        request.setSslConfig(QCNetworkSslConfig::insecureConfig());

        auto *reply = m_manager->sendGet(request);
        if (!waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 15000)) {
            const QString err = reply->errorString();
            reply->cancel();
            reply->deleteLater();
            QFAIL(qPrintable(QStringLiteral("HTTP/1.1 基准请求 15s 内未完成：%1").arg(err)));
        }
        if (reply->error() != NetworkError::NoError) {
            const QString errorString = reply->errorString();
            reply->deleteLater();
            QFAIL(qPrintable(QStringLiteral("HTTP/1.1 基准请求失败：%1").arg(errorString)));
        }
        reply->deleteLater();
    }

    qint64 http1Elapsed = http1Timer.elapsed();

    // ========== HTTP/2 基准测试 ==========
    QElapsedTimer http2Timer;
    http2Timer.start();

    for (int i = 0; i < requestCount; ++i) {
        QCNetworkRequest request(
            QUrl(m_h2BaseUrl + QStringLiteral("/reqinfo?http2=%1").arg(QString::number(i))));
        request.setHttpVersion(QCNetworkHttpVersion::Http2TLS);
        request.setSslConfig(QCNetworkSslConfig::insecureConfig());

        auto *reply = m_manager->sendGet(request);
        if (!waitForSignal(reply, QMetaMethod::fromSignal(&QCNetworkReply::finished), 15000)) {
            const QString err = reply->errorString();
            reply->cancel();
            reply->deleteLater();
            QFAIL(qPrintable(QStringLiteral("HTTP/2 基准请求 15s 内未完成：%1").arg(err)));
        }
        if (reply->error() != NetworkError::NoError) {
            const QString errorString = reply->errorString();
            reply->deleteLater();
            QFAIL(qPrintable(QStringLiteral("HTTP/2 基准请求失败：%1").arg(errorString)));
        }
        reply->deleteLater();
    }

    qint64 http2Elapsed = http2Timer.elapsed();
    qDebug() << "顺序请求耗时对比(ms): http1=" << http1Elapsed << "http2=" << http2Elapsed;

    // 不强制要求 HTTP/2 更快（因为测试环境差异），只验证流程可用
}

QTEST_MAIN(TestQCNetworkHttp2)
#include "tst_QCNetworkHttp2.moc"
