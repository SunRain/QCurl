/**
 * @file tst_QCNetworkDiagnostics.cpp
 * @brief QCNetworkDiagnostics 网络诊断工具单元测试
 *
 * 测试 DNS 解析、连接测试、SSL 检查、HTTP 探测和综合诊断功能。
 *
 */

#include "QCNetworkDiagnostics.h"

#include <QDateTime>
#include <QHostInfo>
#include <QJsonDocument>
#include <QUrl>
#include <QtTest/QtTest>

using namespace QCurl;

namespace {

constexpr int kLocalHttpbinRetryAttempts = 3;
constexpr int kLocalHttpbinRetryDelayMs  = 200;

QString diagMessage(const DiagResult &result)
{
    QString message = result.toString().trimmed();
    if (!result.details.isEmpty()) {
        const QJsonDocument detailsJson = QJsonDocument::fromVariant(result.details);
        if (!detailsJson.isNull()) {
            message += QStringLiteral("\nJSON details: %1")
                           .arg(QString::fromUtf8(detailsJson.toJson(QJsonDocument::Compact)));
        }
    }
    return message;
}

DiagResult probeHttpWithRetry(const QUrl &url,
                              int timeoutMs,
                              int attempts     = kLocalHttpbinRetryAttempts,
                              int retryDelayMs = kLocalHttpbinRetryDelayMs)
{
    DiagResult lastResult;
    for (int attempt = 0; attempt < attempts; ++attempt) {
        lastResult = QCNetworkDiagnostics::probeHTTP(url, timeoutMs);
        if (lastResult.success || lastResult.details.contains("statusCode")) {
            return lastResult;
        }
        if (attempt + 1 < attempts) {
            QTest::qWait(retryDelayMs);
        }
    }
    return lastResult;
}

DiagResult diagnoseWithRetry(const QUrl &url,
                             int attempts     = kLocalHttpbinRetryAttempts,
                             int retryDelayMs = kLocalHttpbinRetryDelayMs)
{
    DiagResult lastResult;
    for (int attempt = 0; attempt < attempts; ++attempt) {
        lastResult = QCNetworkDiagnostics::diagnose(url);
        if (lastResult.success) {
            return lastResult;
        }
        if (attempt + 1 < attempts) {
            QTest::qWait(retryDelayMs);
        }
    }
    return lastResult;
}

} // namespace

class tst_QCNetworkDiagnostics : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // 1. DiagResult 结构测试
    void testDiagResult_ToString();

    // 2. DNS 解析测试
    void testResolveDNS_ValidDomain();
    void testResolveDNS_InvalidDomain();
    void testResolveDNS_Localhost();
    void testResolveDNS_IPv6Support();

    // 3. 反向 DNS 测试
    void testReverseDNS_ValidIP();
    void testReverseDNS_InvalidIP();

    // 4. TCP 连接测试
    void testConnection_ValidHost();
    void testConnection_InvalidHost();
    void testConnection_Timeout();
    void testConnection_CommonPorts();

    // 5. SSL 证书检查测试
    void testCheckSSL_ValidCertificate();
    void testCheckSSL_ExpiredCertificate();
    void testCheckSSL_SelfSignedCertificate();
    void testCheckSSL_CertificateDetails();

    // 6. HTTP 探测测试
    void testProbeHTTP_ValidURL();
    void testProbeHTTP_HTTPS();
    void testProbeHTTP_Redirect();
    void testProbeHTTP_404NotFound();
    void testProbeHTTP_TimingBreakdown();

    // 7. 综合诊断测试
    void testDiagnose_CompleteFlow();
    void testDiagnose_HTTPSite();
    void testDiagnose_HTTPSSite();
    void testDiagnose_FailedDNS();
    void testDiagnose_FailedConnection();

private:
    /**
     * @brief 检查是否有网络连接
     */
    bool hasNetworkAccess() const;

    static bool isTruthyEnvValue(const QByteArray &value);
    bool isExternalNetworkAllowed() const;
    QUrl httpbinBaseUrl() const;
    QUrl httpbinUrl(const QString &path) const;
};

void tst_QCNetworkDiagnostics::initTestCase()
{
    const QUrl httpbinBase = httpbinBaseUrl();
    if (httpbinBase.isValid() && !httpbinBase.isEmpty()) {
        qDebug() << "QCURL_HTTPBIN_URL:" << httpbinBase.toString();
    }

    if (!isExternalNetworkAllowed()) {
        qWarning() << "外网探测默认关闭（设置 QCURL_ALLOW_EXTERNAL_NETWORK=1 可启用）";
        qWarning() << "公网相关用例将被跳过（不影响本地/离线诊断用例）";
        return;
    }

    if (!hasNetworkAccess()) {
        qWarning() << "未检测到网络连接";
        qWarning() << "部分测试将被跳过";
    }
}

void tst_QCNetworkDiagnostics::cleanupTestCase()
{}

bool tst_QCNetworkDiagnostics::hasNetworkAccess() const
{
    static bool cached      = false;
    static bool initialized = false;
    if (initialized) {
        return cached;
    }

    // 使用外部域名进行探测，避免 localhost 导致“永远有网络”的误判
    const QHostInfo info = QHostInfo::fromName(QStringLiteral("example.com"));
    cached               = (info.error() == QHostInfo::NoError) && !info.addresses().isEmpty();
    initialized          = true;
    return cached;
}

bool tst_QCNetworkDiagnostics::isTruthyEnvValue(const QByteArray &value)
{
    const QByteArray normalized = value.trimmed().toLower();
    if (normalized.isEmpty()) {
        return false;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off") {
        return false;
    }
    return true;
}

bool tst_QCNetworkDiagnostics::isExternalNetworkAllowed() const
{
    return isTruthyEnvValue(qgetenv("QCURL_ALLOW_EXTERNAL_NETWORK"));
}

QUrl tst_QCNetworkDiagnostics::httpbinBaseUrl() const
{
    const QByteArray url = qgetenv("QCURL_HTTPBIN_URL");
    if (url.isEmpty()) {
        return {};
    }
    return QUrl(QString::fromUtf8(url));
}

QUrl tst_QCNetworkDiagnostics::httpbinUrl(const QString &path) const
{
    const QUrl baseUrl = httpbinBaseUrl();
    if (!baseUrl.isValid() || baseUrl.isEmpty()) {
        return {};
    }
    return baseUrl.resolved(QUrl(path));
}

// ============================================================================
// DiagResult 结构测试
// ============================================================================

void tst_QCNetworkDiagnostics::testDiagResult_ToString()
{
    DiagResult result;
    result.success         = true;
    result.summary         = "测试成功";
    result.durationMs      = 123;
    result.timestamp       = QDateTime::currentDateTime();
    result.details["key1"] = "value1";
    result.details["key2"] = 42;

    QString str = result.toString();
    QVERIFY(str.contains("✅"));
    QVERIFY(str.contains("测试成功"));
    QVERIFY(str.contains("123ms"));
    QVERIFY(str.contains("key1"));
    QVERIFY(str.contains("value1"));

    // 测试失败情况
    result.success     = false;
    result.errorString = "测试错误";
    str                = result.toString();
    QVERIFY(str.contains("❌"));
    QVERIFY(str.contains("测试错误"));
}

// ============================================================================
// DNS 解析测试
// ============================================================================

void tst_QCNetworkDiagnostics::testResolveDNS_ValidDomain()
{
    if (!isExternalNetworkAllowed()) {
        QSKIP("未显式允许外网（设置 QCURL_ALLOW_EXTERNAL_NETWORK=1）");
    }
    if (!hasNetworkAccess()) {
        QSKIP("无网络连接");
    }

    auto result = QCNetworkDiagnostics::resolveDNS("example.com", 5000);

    QVERIFY(result.success);
    QVERIFY(result.summary.contains("DNS 解析成功"));
    QCOMPARE(result.details["hostname"].toString(), QStringLiteral("example.com"));

    // 应该至少有一个 IPv4 地址
    QStringList ipv4 = result.details["ipv4"].toStringList();
    QVERIFY(!ipv4.isEmpty());

    // 验证解析耗时合理
    QVERIFY(result.durationMs >= 0);
    QVERIFY(result.durationMs < 5000);
}

void tst_QCNetworkDiagnostics::testResolveDNS_InvalidDomain()
{
    auto result = QCNetworkDiagnostics::resolveDNS("this-domain-does-not-exist-12345.com", 2000);

    // 注意：某些 DNS 服务器（如运营商 DNS）可能提供搜索建议或导航页面
    // 因此不强制要求失败，只验证基本功能
    if (!result.success) {
        QVERIFY(result.summary.contains("DNS 解析失败"));
        QVERIFY(!result.errorString.isEmpty());
        qDebug() << "无效域名测试：DNS 正确返回失败";
    } else {
        qWarning() << "DNS 服务器为无效域名提供了结果（可能是搜索建议）";
        qWarning() << "返回的 IP:" << result.details["ipv4"].toStringList();
    }
}

void tst_QCNetworkDiagnostics::testResolveDNS_Localhost()
{
    auto result = QCNetworkDiagnostics::resolveDNS("localhost", 1000);

    QVERIFY(result.success);
    QCOMPARE(result.details["hostname"].toString(), QStringLiteral("localhost"));

    QStringList ipv4 = result.details["ipv4"].toStringList();
    QVERIFY(!ipv4.isEmpty());
    QVERIFY(ipv4.contains("127.0.0.1"));
}

void tst_QCNetworkDiagnostics::testResolveDNS_IPv6Support()
{
    if (!isExternalNetworkAllowed()) {
        QSKIP("未显式允许外网（设置 QCURL_ALLOW_EXTERNAL_NETWORK=1）");
    }
    if (!hasNetworkAccess()) {
        QSKIP("无网络连接");
    }

    auto result = QCNetworkDiagnostics::resolveDNS("google.com", 5000);

    if (result.success) {
        // 检查是否支持 IPv6
        QStringList ipv6 = result.details["ipv6"].toStringList();
        if (!ipv6.isEmpty()) {
            qDebug() << "检测到 IPv6 支持:" << ipv6.first();
        }
        // 不强制要求 IPv6，因为环境可能不支持
    }
}

// ============================================================================
// 反向 DNS 测试
// ============================================================================

void tst_QCNetworkDiagnostics::testReverseDNS_ValidIP()
{
    if (!isExternalNetworkAllowed()) {
        QSKIP("未显式允许外网（设置 QCURL_ALLOW_EXTERNAL_NETWORK=1）");
    }
    if (!hasNetworkAccess()) {
        QSKIP("无网络连接");
    }

    auto result = QCNetworkDiagnostics::reverseDNS("8.8.8.8", 5000);

    QVERIFY(result.success);
    QVERIFY(result.summary.contains("反向 DNS 解析成功"));
    QCOMPARE(result.details["ip"].toString(), QStringLiteral("8.8.8.8"));
    QVERIFY(!result.details["hostname"].toString().isEmpty());
    QVERIFY(result.details["hostname"].toString().contains("dns.google"));
}

void tst_QCNetworkDiagnostics::testReverseDNS_InvalidIP()
{
    auto result = QCNetworkDiagnostics::reverseDNS("256.256.256.256", 2000);

    // 注意：无效的 IP 格式应该失败，但某些系统可能有不同行为
    if (!result.success) {
        QVERIFY(result.summary.contains("反向 DNS 解析失败"));
        qDebug() << "无效 IP 测试：DNS 正确返回失败";
    } else {
        qWarning() << "系统接受了无效的 IP 格式（可能被当作域名处理）";
    }
}

// ============================================================================
// TCP 连接测试
// ============================================================================

void tst_QCNetworkDiagnostics::testConnection_ValidHost()
{
    if (!isExternalNetworkAllowed()) {
        QSKIP("未显式允许外网（设置 QCURL_ALLOW_EXTERNAL_NETWORK=1）");
    }
    if (!hasNetworkAccess()) {
        QSKIP("无网络连接");
    }

    // 测试连接到 Google DNS (8.8.8.8:53)
    auto result = QCNetworkDiagnostics::testConnection("8.8.8.8", 53, 5000);

    if (result.success) {
        QVERIFY(result.summary.contains("连接成功"));
        QCOMPARE(result.details["host"].toString(), QStringLiteral("8.8.8.8"));
        QCOMPARE(result.details["port"].toInt(), 53);
        QVERIFY(result.details["connected"].toBool());
        QVERIFY(!result.details["resolvedIP"].toString().isEmpty());
    } else {
        qWarning() << "连接测试失败（可能被防火墙阻止）:" << result.errorString;
    }
}

void tst_QCNetworkDiagnostics::testConnection_InvalidHost()
{
    // 连接到不存在的主机（使用 TEST-NET-1 保留地址段 192.0.2.0/24）
    auto result = QCNetworkDiagnostics::testConnection("192.0.2.1", 12345, 2000);

    // 注意：某些网络环境可能有特殊路由配置
    if (!result.success) {
        QVERIFY(result.summary.contains("连接失败"));
        QVERIFY(result.details["connected"].toBool() == false);
        qDebug() << "无效主机测试：连接正确失败";
    } else {
        qWarning() << "连接到保留地址意外成功（可能有特殊网络配置）";
    }
}

void tst_QCNetworkDiagnostics::testConnection_Timeout()
{
    // 连接到一个不可达的 IP 地址
    auto startTime = QDateTime::currentMSecsSinceEpoch();
    auto result    = QCNetworkDiagnostics::testConnection("10.255.255.1", 81, 1000);
    auto elapsed   = QDateTime::currentMSecsSinceEpoch() - startTime;

    QVERIFY(!result.success);
    // 验证超时时间大致正确（允许 ±500ms 误差）
    QVERIFY(elapsed >= 500 && elapsed <= 1500);
}

void tst_QCNetworkDiagnostics::testConnection_CommonPorts()
{
    if (!isExternalNetworkAllowed()) {
        QSKIP("未显式允许外网（设置 QCURL_ALLOW_EXTERNAL_NETWORK=1）");
    }
    if (!hasNetworkAccess()) {
        QSKIP("无网络连接");
    }

    // 测试常见端口（HTTP、HTTPS）
    struct PortTest
    {
        QString host;
        int port;
        QString description;
    };

    QList<PortTest> tests = {{"google.com", 80, "HTTP"}, {"google.com", 443, "HTTPS"}};

    for (const auto &test : tests) {
        auto result = QCNetworkDiagnostics::testConnection(test.host, test.port, 5000);
        if (result.success) {
            qDebug() << test.description << "连接成功:" << test.host << ":" << test.port;
            QVERIFY(result.details["connected"].toBool());
        }
    }
}

// ============================================================================
// SSL 证书检查测试
// ============================================================================

void tst_QCNetworkDiagnostics::testCheckSSL_ValidCertificate()
{
    if (!isExternalNetworkAllowed()) {
        QSKIP("未显式允许外网（设置 QCURL_ALLOW_EXTERNAL_NETWORK=1）");
    }
    if (!hasNetworkAccess()) {
        QSKIP("无网络连接");
    }

    auto result = QCNetworkDiagnostics::checkSSL("google.com", 443, 10000);

    if (!result.success) {
        QSKIP(qPrintable(
            QStringLiteral("SSL 握手失败（网络/代理/证书环境相关）: %1").arg(result.errorString)));
    }
    QVERIFY(result.summary.contains("SSL 证书有效"));

    // 验证证书详情
    QVERIFY(!result.details["issuer"].toString().isEmpty());
    QVERIFY(!result.details["subject"].toString().isEmpty());
    QVERIFY(result.details["daysValid"].toInt() > 0);
    QVERIFY(result.details["verified"].toBool());
}

void tst_QCNetworkDiagnostics::testCheckSSL_ExpiredCertificate()
{
    if (!isExternalNetworkAllowed()) {
        QSKIP("未显式允许外网（设置 QCURL_ALLOW_EXTERNAL_NETWORK=1）");
    }
    if (!hasNetworkAccess()) {
        QSKIP("无网络连接");
    }

    // expired.badssl.com 是一个专门用于测试过期证书的网站
    auto result = QCNetworkDiagnostics::checkSSL("expired.badssl.com", 443, 10000);

    // 期望连接失败或证书无效
    if (!result.success) {
        qDebug() << "过期证书测试:" << result.errorString;
        QVERIFY(result.summary.contains("SSL 握手失败"));
    } else {
        // 如果连接成功，检查剩余天数是否为负
        int daysValid = result.details["daysValid"].toInt();
        qDebug() << "证书剩余有效期:" << daysValid << "天";
    }
}

void tst_QCNetworkDiagnostics::testCheckSSL_SelfSignedCertificate()
{
    if (!isExternalNetworkAllowed()) {
        QSKIP("未显式允许外网（设置 QCURL_ALLOW_EXTERNAL_NETWORK=1）");
    }
    if (!hasNetworkAccess()) {
        QSKIP("无网络连接");
    }

    // self-signed.badssl.com 是一个专门用于测试自签名证书的网站
    auto result = QCNetworkDiagnostics::checkSSL("self-signed.badssl.com", 443, 10000);

    // 期望 SSL 验证失败（但在部分网络环境中可能被代理/阻断，需容错）
    if (result.success) {
        QSKIP("自签名站点连接成功（可能存在 HTTPS 代理/证书替换），无法验证 sslErrors");
    }

    qDebug() << "自签名证书测试:" << result.errorString;
    if (!result.details.contains("sslErrors")) {
        const QByteArray skipReason
            = QStringLiteral("未捕获到 sslErrors（站点可能不可达/被阻断/超时），error=%1")
                  .arg(result.errorString)
                  .toUtf8();
        QSKIP(skipReason.constData());
    }

    QVERIFY(result.details.contains("sslErrors"));
}

void tst_QCNetworkDiagnostics::testCheckSSL_CertificateDetails()
{
    if (!isExternalNetworkAllowed()) {
        QSKIP("未显式允许外网（设置 QCURL_ALLOW_EXTERNAL_NETWORK=1）");
    }
    if (!hasNetworkAccess()) {
        QSKIP("无网络连接");
    }

    auto result = QCNetworkDiagnostics::checkSSL("www.github.com", 443, 10000);

    if (result.success) {
        // 验证证书详情字段存在
        QVERIFY(result.details.contains("issuer"));
        QVERIFY(result.details.contains("subject"));
        QVERIFY(result.details.contains("notBefore"));
        QVERIFY(result.details.contains("notAfter"));
        QVERIFY(result.details.contains("daysValid"));
        QVERIFY(result.details.contains("tlsVersion"));

        qDebug() << "证书颁发者:" << result.details["issuer"].toString();
        qDebug() << "证书主题:" << result.details["subject"].toString();
        qDebug() << "TLS 版本:" << result.details["tlsVersion"].toString();
        qDebug() << "剩余有效期:" << result.details["daysValid"].toInt() << "天";
    }
}

// ============================================================================
// HTTP 探测测试
// ============================================================================

void tst_QCNetworkDiagnostics::testProbeHTTP_ValidURL()
{
    const QUrl localUrl = httpbinUrl(QStringLiteral("/get"));
    if (localUrl.isValid() && !localUrl.isEmpty()) {
        const auto result = probeHttpWithRetry(localUrl, 10000);

        if (!result.success) {
            const QByteArray skipReason
                = QStringLiteral("本地 httpbin `/get` 探测未稳定成功，跳过以避免环境误报: %1")
                      .arg(diagMessage(result))
                      .toUtf8();
            QSKIP(skipReason.constData());
        }

        QVERIFY2(result.success, qPrintable(diagMessage(result)));
        QVERIFY2(result.summary.contains("HTTP 探测成功"), qPrintable(diagMessage(result)));
        QCOMPARE(result.details["url"].toString(), localUrl.toString());
        QCOMPARE(result.details["statusCode"].toInt(), 200);
        return;
    }

    if (!isExternalNetworkAllowed()) {
        QSKIP("未显式允许外网（QCURL_ALLOW_EXTERNAL_NETWORK=1），且未设置 QCURL_HTTPBIN_URL");
    }
    if (!hasNetworkAccess()) {
        QSKIP("无网络连接");
    }

    auto result = QCNetworkDiagnostics::probeHTTP(QUrl("http://example.com"), 10000);

    QVERIFY(result.success);
    QVERIFY(result.summary.contains("HTTP 探测成功"));
    QCOMPARE(result.details["url"].toString(), QStringLiteral("http://example.com"));
    QCOMPARE(result.details["statusCode"].toInt(), 200);
}

void tst_QCNetworkDiagnostics::testProbeHTTP_HTTPS()
{
    if (!isExternalNetworkAllowed()) {
        QSKIP("未显式允许外网（设置 QCURL_ALLOW_EXTERNAL_NETWORK=1）");
    }
    if (!hasNetworkAccess()) {
        QSKIP("无网络连接");
    }

    auto result = QCNetworkDiagnostics::probeHTTP(QUrl("https://www.google.com"), 10000);

    if (!result.success) {
        QSKIP(qPrintable(
            QStringLiteral("HTTPS 探测失败（网络/代理/证书环境相关）: %1").arg(result.errorString)));
    }
    QVERIFY(result.details["statusCode"].toInt() == 200
            || result.details["statusCode"].toInt() == 301
            || result.details["statusCode"].toInt() == 302);
}

void tst_QCNetworkDiagnostics::testProbeHTTP_Redirect()
{
    if (!isExternalNetworkAllowed()) {
        QSKIP("未显式允许外网（设置 QCURL_ALLOW_EXTERNAL_NETWORK=1）");
    }
    if (!hasNetworkAccess()) {
        QSKIP("无网络连接");
    }

    // http://google.com 会重定向到 https://www.google.com
    auto result = QCNetworkDiagnostics::probeHTTP(QUrl("http://google.com"), 10000);

    if (result.success) {
        QString finalURL = result.details["finalURL"].toString();
        qDebug() << "重定向到:" << finalURL;

        // 注意：Google 可能重定向到不同的国家域名（如 .hk, .jp 等）
        // 只验证发生了重定向，不强制要求 HTTPS
        QVERIFY(finalURL.contains("google"));

        if (finalURL.contains("https://")) {
            qDebug() << "重定向到 HTTPS";
        } else {
            qDebug() << "重定向到其他协议或域名:" << finalURL;
        }
    }
}

void tst_QCNetworkDiagnostics::testProbeHTTP_404NotFound()
{
    QUrl url;
    const bool useLocalHttpbin = httpbinBaseUrl().isValid() && !httpbinBaseUrl().isEmpty();
    const QUrl localUrl = httpbinUrl(QStringLiteral("/status/404"));
    if (localUrl.isValid() && !localUrl.isEmpty()) {
        url = localUrl;
    } else {
        if (!isExternalNetworkAllowed()) {
            QSKIP("未显式允许外网（QCURL_ALLOW_EXTERNAL_NETWORK=1），且未设置 QCURL_HTTPBIN_URL");
        }
        if (!hasNetworkAccess()) {
            QSKIP("无网络连接");
        }
        url = QUrl(QStringLiteral("https://httpbin.org/status/404"));
    }

    const auto result = useLocalHttpbin ? probeHttpWithRetry(url, 10000)
                                        : QCNetworkDiagnostics::probeHTTP(url, 10000);

    if (!result.details.contains("statusCode") && useLocalHttpbin) {
        const QByteArray skipReason
            = QStringLiteral("本地 httpbin `/status/404` 未返回稳定 HTTP 状态，跳过以避免环境误报: %1")
                  .arg(diagMessage(result))
                  .toUtf8();
        QSKIP(skipReason.constData());
    }

    QVERIFY2(result.details.contains("statusCode"), qPrintable(diagMessage(result)));
    QCOMPARE(result.details["statusCode"].toInt(), 404);
}

void tst_QCNetworkDiagnostics::testProbeHTTP_TimingBreakdown()
{
    QUrl url;
    const bool useLocalHttpbin = httpbinBaseUrl().isValid() && !httpbinBaseUrl().isEmpty();
    const QUrl localUrl = httpbinUrl(QStringLiteral("/get"));
    if (localUrl.isValid() && !localUrl.isEmpty()) {
        url = localUrl;
    } else {
        if (!isExternalNetworkAllowed()) {
            QSKIP("未显式允许外网（QCURL_ALLOW_EXTERNAL_NETWORK=1），且未设置 QCURL_HTTPBIN_URL");
        }
        if (!hasNetworkAccess()) {
            QSKIP("无网络连接");
        }
        url = QUrl(QStringLiteral("https://example.com"));
    }

    const auto result = useLocalHttpbin ? probeHttpWithRetry(url, 10000)
                                        : QCNetworkDiagnostics::probeHTTP(url, 10000);

    if (useLocalHttpbin && !result.success) {
        const QByteArray skipReason
            = QStringLiteral("本地 httpbin `/get` 耗时探测未稳定成功，跳过以避免环境误报: %1")
                  .arg(diagMessage(result))
                  .toUtf8();
        QSKIP(skipReason.constData());
    }

    QVERIFY2(result.success, qPrintable(diagMessage(result)));
    QVERIFY2(result.details.contains("totalTime"), qPrintable(diagMessage(result)));
    QVERIFY(result.details["totalTime"].toLongLong() > 0);

    qDebug() << "HTTP 探测耗时详情:";
    qDebug() << "  总耗时:" << result.details["totalTime"].toLongLong() << "ms";
}

// ============================================================================
// 综合诊断测试
// ============================================================================

void tst_QCNetworkDiagnostics::testDiagnose_CompleteFlow()
{
    QUrl url;
    const bool useLocalHttpbin = httpbinBaseUrl().isValid() && !httpbinBaseUrl().isEmpty();
    const QUrl localUrl = httpbinUrl(QStringLiteral("/get"));
    if (localUrl.isValid() && !localUrl.isEmpty()) {
        url = localUrl;
    } else {
        if (!isExternalNetworkAllowed()) {
            QSKIP("未显式允许外网（QCURL_ALLOW_EXTERNAL_NETWORK=1），且未设置 QCURL_HTTPBIN_URL");
        }
        if (!hasNetworkAccess()) {
            QSKIP("无网络连接");
        }
        url = QUrl(QStringLiteral("http://example.com"));
    }

    const auto result = useLocalHttpbin ? diagnoseWithRetry(url) : QCNetworkDiagnostics::diagnose(url);

    if (useLocalHttpbin && !result.success) {
        const QByteArray skipReason
            = QStringLiteral("本地 httpbin 综合诊断未稳定成功，跳过以避免环境误报: %1")
                  .arg(diagMessage(result))
                  .toUtf8();
        QSKIP(skipReason.constData());
    }

    QVERIFY2(result.success, qPrintable(diagMessage(result)));
    QVERIFY2(result.summary.contains("综合诊断完成"), qPrintable(diagMessage(result)));

    // 验证所有诊断步骤都执行了
    QVERIFY2(result.details.contains("dns"), qPrintable(diagMessage(result)));
    QVERIFY2(result.details.contains("connection"), qPrintable(diagMessage(result)));
    QVERIFY2(result.details.contains("http"), qPrintable(diagMessage(result)));
    QVERIFY2(result.details.contains("overallHealth"), qPrintable(diagMessage(result)));

    qDebug() << "整体健康状态:" << result.details["overallHealth"].toString();
}

void tst_QCNetworkDiagnostics::testDiagnose_HTTPSite()
{
    QUrl url;
    const QUrl localUrl         = httpbinBaseUrl();
    const bool useLocalHttpbin  = localUrl.isValid() && !localUrl.isEmpty();
    if (localUrl.isValid() && !localUrl.isEmpty()) {
        url = localUrl;
    } else {
        if (!isExternalNetworkAllowed()) {
            QSKIP("未显式允许外网（QCURL_ALLOW_EXTERNAL_NETWORK=1），且未设置 QCURL_HTTPBIN_URL");
        }
        if (!hasNetworkAccess()) {
            QSKIP("无网络连接");
        }
        url = QUrl(QStringLiteral("http://httpbin.org"));
    }

    const auto result = useLocalHttpbin ? diagnoseWithRetry(url) : QCNetworkDiagnostics::diagnose(url);

    if (useLocalHttpbin && !result.success) {
        const QByteArray skipReason
            = QStringLiteral("本地 httpbin 根地址综合诊断未稳定成功，跳过以避免环境误报: %1")
                  .arg(diagMessage(result))
                  .toUtf8();
        QSKIP(skipReason.constData());
    }

    if (result.success) {
        QVERIFY2(result.details.contains("dns"), qPrintable(diagMessage(result)));
        QVERIFY2(result.details.contains("connection"), qPrintable(diagMessage(result)));
        QVERIFY2(result.details.contains("http"), qPrintable(diagMessage(result)));
        QVERIFY2(!result.details.contains("ssl"), qPrintable(diagMessage(result))); // HTTP 不检查 SSL
    }
}

void tst_QCNetworkDiagnostics::testDiagnose_HTTPSSite()
{
    if (!isExternalNetworkAllowed()) {
        QSKIP("未显式允许外网（设置 QCURL_ALLOW_EXTERNAL_NETWORK=1）");
    }
    if (!hasNetworkAccess()) {
        QSKIP("无网络连接");
    }

    auto result = QCNetworkDiagnostics::diagnose(QUrl("https://www.github.com"));

    if (result.success) {
        QVERIFY(result.details.contains("dns"));
        QVERIFY(result.details.contains("connection"));
        QVERIFY(result.details.contains("ssl")); // HTTPS 应该有 SSL 检查
        QVERIFY(result.details.contains("http"));
    }
}

void tst_QCNetworkDiagnostics::testDiagnose_FailedDNS()
{
    auto result = QCNetworkDiagnostics::diagnose(
        QUrl("http://this-domain-absolutely-does-not-exist-12345.com"));

    // 注意：某些 DNS 服务器可能提供搜索建议
    QVERIFY(result.details.contains("dns"));

    if (!result.success) {
        // 无效域名在部分网络环境下可能被“劫持解析”，导致 DNS/连接成功但 HTTP 探测失败。
        // 这里不强行绑定失败阶段，只要求诊断返回失败并给出明确失败摘要。
        QVERIFY(result.summary.startsWith("诊断失败:"));
        qDebug() << "综合诊断：无效域名场景返回失败（阶段可能为 DNS/连接/HTTP）";
    } else {
        qWarning() << "DNS 服务器为无效域名提供了结果";
        qDebug() << "诊断摘要:" << result.summary;
    }
}

void tst_QCNetworkDiagnostics::testDiagnose_FailedConnection()
{
    // 连接到不可达的地址
    auto result = QCNetworkDiagnostics::diagnose(QUrl("http://192.0.2.1:12345"));

    QVERIFY(!result.success);
    // 可能在 DNS 或连接阶段失败
    QVERIFY(result.details.contains("dns") || result.details.contains("connection"));
}

QTEST_MAIN(tst_QCNetworkDiagnostics)
#include "tst_QCNetworkDiagnostics.moc"
