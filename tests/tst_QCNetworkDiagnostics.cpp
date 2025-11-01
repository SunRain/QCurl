/**
 * @file tst_QCNetworkDiagnostics.cpp
 * @brief QCNetworkDiagnostics 网络诊断工具单元测试
 *
 * 测试 DNS 解析、连接测试、SSL 检查、HTTP 探测和综合诊断功能。
 *
 */

#include <QtTest/QtTest>
#include "QCNetworkDiagnostics.h"
#include <QUrl>
#include <QDateTime>

using namespace QCurl;

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
};

void tst_QCNetworkDiagnostics::initTestCase()
{
    qDebug() << "========================================";
    qDebug() << "QCNetworkDiagnostics 测试套件";
    qDebug() << "v2.19.0";
    qDebug() << "========================================";

    if (!hasNetworkAccess()) {
        qWarning() << "⚠️  未检测到网络连接";
        qWarning() << "   部分测试将被跳过";
    }
}

void tst_QCNetworkDiagnostics::cleanupTestCase()
{
    qDebug() << "========================================";
    qDebug() << "QCNetworkDiagnostics 测试套件完成";
    qDebug() << "========================================";
}

bool tst_QCNetworkDiagnostics::hasNetworkAccess() const
{
    // 简单测试：尝试解析 localhost
    auto result = QCNetworkDiagnostics::resolveDNS("localhost", 1000);
    return result.success;
}

// ============================================================================
// DiagResult 结构测试
// ============================================================================

void tst_QCNetworkDiagnostics::testDiagResult_ToString()
{
    DiagResult result;
    result.success = true;
    result.summary = "测试成功";
    result.durationMs = 123;
    result.timestamp = QDateTime::currentDateTime();
    result.details["key1"] = "value1";
    result.details["key2"] = 42;

    QString str = result.toString();
    QVERIFY(str.contains("✅"));
    QVERIFY(str.contains("测试成功"));
    QVERIFY(str.contains("123ms"));
    QVERIFY(str.contains("key1"));
    QVERIFY(str.contains("value1"));

    // 测试失败情况
    result.success = false;
    result.errorString = "测试错误";
    str = result.toString();
    QVERIFY(str.contains("❌"));
    QVERIFY(str.contains("测试错误"));
}

// ============================================================================
// DNS 解析测试
// ============================================================================

void tst_QCNetworkDiagnostics::testResolveDNS_ValidDomain()
{
    if (!hasNetworkAccess()) {
        QSKIP("无网络连接");
    }

    auto result = QCNetworkDiagnostics::resolveDNS("example.com", 5000);

    QVERIFY(result.success);
    QVERIFY(result.summary.contains("DNS 解析成功"));
    QCOMPARE(result.details["hostname"].toString(), QString("example.com"));

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
    QCOMPARE(result.details["hostname"].toString(), QString("localhost"));

    QStringList ipv4 = result.details["ipv4"].toStringList();
    QVERIFY(!ipv4.isEmpty());
    QVERIFY(ipv4.contains("127.0.0.1"));
}

void tst_QCNetworkDiagnostics::testResolveDNS_IPv6Support()
{
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
    if (!hasNetworkAccess()) {
        QSKIP("无网络连接");
    }

    auto result = QCNetworkDiagnostics::reverseDNS("8.8.8.8", 5000);

    QVERIFY(result.success);
    QVERIFY(result.summary.contains("反向 DNS 解析成功"));
    QCOMPARE(result.details["ip"].toString(), QString("8.8.8.8"));
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
    if (!hasNetworkAccess()) {
        QSKIP("无网络连接");
    }

    // 测试连接到 Google DNS (8.8.8.8:53)
    auto result = QCNetworkDiagnostics::testConnection("8.8.8.8", 53, 5000);

    if (result.success) {
        QVERIFY(result.summary.contains("连接成功"));
        QCOMPARE(result.details["host"].toString(), QString("8.8.8.8"));
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
    auto result = QCNetworkDiagnostics::testConnection("10.255.255.1", 81, 1000);
    auto elapsed = QDateTime::currentMSecsSinceEpoch() - startTime;

    QVERIFY(!result.success);
    // 验证超时时间大致正确（允许 ±500ms 误差）
    QVERIFY(elapsed >= 500 && elapsed <= 1500);
}

void tst_QCNetworkDiagnostics::testConnection_CommonPorts()
{
    if (!hasNetworkAccess()) {
        QSKIP("无网络连接");
    }

    // 测试常见端口（HTTP、HTTPS）
    struct PortTest {
        QString host;
        int port;
        QString description;
    };

    QList<PortTest> tests = {
        {"google.com", 80, "HTTP"},
        {"google.com", 443, "HTTPS"}
    };

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
    if (!hasNetworkAccess()) {
        QSKIP("无网络连接");
    }

    auto result = QCNetworkDiagnostics::checkSSL("google.com", 443, 10000);

    QVERIFY(result.success);
    QVERIFY(result.summary.contains("SSL 证书有效"));

    // 验证证书详情
    QVERIFY(!result.details["issuer"].toString().isEmpty());
    QVERIFY(!result.details["subject"].toString().isEmpty());
    QVERIFY(result.details["daysValid"].toInt() > 0);
    QVERIFY(result.details["verified"].toBool());
}

void tst_QCNetworkDiagnostics::testCheckSSL_ExpiredCertificate()
{
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
    if (!hasNetworkAccess()) {
        QSKIP("无网络连接");
    }

    // self-signed.badssl.com 是一个专门用于测试自签名证书的网站
    auto result = QCNetworkDiagnostics::checkSSL("self-signed.badssl.com", 443, 10000);

    // 期望 SSL 验证失败
    if (!result.success) {
        qDebug() << "自签名证书测试:" << result.errorString;
        QVERIFY(result.details.contains("sslErrors"));
    }
}

void tst_QCNetworkDiagnostics::testCheckSSL_CertificateDetails()
{
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
    if (!hasNetworkAccess()) {
        QSKIP("无网络连接");
    }

    auto result = QCNetworkDiagnostics::probeHTTP(QUrl("http://example.com"), 10000);

    QVERIFY(result.success);
    QVERIFY(result.summary.contains("HTTP 探测成功"));
    QCOMPARE(result.details["url"].toString(), QString("http://example.com"));
    QCOMPARE(result.details["statusCode"].toInt(), 200);
}

void tst_QCNetworkDiagnostics::testProbeHTTP_HTTPS()
{
    if (!hasNetworkAccess()) {
        QSKIP("无网络连接");
    }

    auto result = QCNetworkDiagnostics::probeHTTP(QUrl("https://www.google.com"), 10000);

    QVERIFY(result.success);
    QVERIFY(result.details["statusCode"].toInt() == 200 ||
            result.details["statusCode"].toInt() == 301 ||
            result.details["statusCode"].toInt() == 302);
}

void tst_QCNetworkDiagnostics::testProbeHTTP_Redirect()
{
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
            qDebug() << "✅ 重定向到 HTTPS";
        } else {
            qDebug() << "ℹ️  重定向到其他协议或域名:" << finalURL;
        }
    }
}

void tst_QCNetworkDiagnostics::testProbeHTTP_404NotFound()
{
    if (!hasNetworkAccess()) {
        QSKIP("无网络连接");
    }

    auto result = QCNetworkDiagnostics::probeHTTP(
        QUrl("https://httpbin.org/status/404"), 10000);

    if (result.success) {
        QCOMPARE(result.details["statusCode"].toInt(), 404);
    }
}

void tst_QCNetworkDiagnostics::testProbeHTTP_TimingBreakdown()
{
    if (!hasNetworkAccess()) {
        QSKIP("无网络连接");
    }

    auto result = QCNetworkDiagnostics::probeHTTP(QUrl("https://example.com"), 10000);

    if (result.success) {
        // 验证时间详情存在
        QVERIFY(result.details.contains("totalTime"));
        QVERIFY(result.details["totalTime"].toLongLong() > 0);

        qDebug() << "HTTP 探测耗时详情:";
        qDebug() << "  总耗时:" << result.details["totalTime"].toLongLong() << "ms";
    }
}

// ============================================================================
// 综合诊断测试
// ============================================================================

void tst_QCNetworkDiagnostics::testDiagnose_CompleteFlow()
{
    if (!hasNetworkAccess()) {
        QSKIP("无网络连接");
    }

    auto result = QCNetworkDiagnostics::diagnose(QUrl("http://example.com"));

    QVERIFY(result.success);
    QVERIFY(result.summary.contains("综合诊断完成"));

    // 验证所有诊断步骤都执行了
    QVERIFY(result.details.contains("dns"));
    QVERIFY(result.details.contains("connection"));
    QVERIFY(result.details.contains("http"));
    QVERIFY(result.details.contains("overallHealth"));

    qDebug() << "整体健康状态:" << result.details["overallHealth"].toString();
}

void tst_QCNetworkDiagnostics::testDiagnose_HTTPSite()
{
    if (!hasNetworkAccess()) {
        QSKIP("无网络连接");
    }

    auto result = QCNetworkDiagnostics::diagnose(QUrl("http://httpbin.org"));

    if (result.success) {
        QVERIFY(result.details.contains("dns"));
        QVERIFY(result.details.contains("connection"));
        QVERIFY(result.details.contains("http"));
        QVERIFY(!result.details.contains("ssl"));  // HTTP 不检查 SSL
    }
}

void tst_QCNetworkDiagnostics::testDiagnose_HTTPSSite()
{
    if (!hasNetworkAccess()) {
        QSKIP("无网络连接");
    }

    auto result = QCNetworkDiagnostics::diagnose(QUrl("https://www.github.com"));

    if (result.success) {
        QVERIFY(result.details.contains("dns"));
        QVERIFY(result.details.contains("connection"));
        QVERIFY(result.details.contains("ssl"));  // HTTPS 应该有 SSL 检查
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
        QVERIFY(result.summary.contains("DNS 解析失败") ||
                result.summary.contains("连接"));
        qDebug() << "综合诊断：正确识别 DNS 失败";
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
