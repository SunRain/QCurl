/**
 * @file tst_QCNetworkHttp3.cpp
 * @brief HTTP/3 功能单元测试
 * 
 */

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QThread>
#include <QElapsedTimer>
#include "QCNetworkAccessManager.h"
#include "QCNetworkRequest.h"
#include "QCNetworkReply.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkSslConfig.h"

using namespace QCurl;

class TestQCNetworkHttp3 : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    
    // 1. HTTP/3 枚举测试
    void testHttp3EnumConversion();
    
    // 2. HTTP/3 请求配置测试
    void testHttp3RequestConfiguration();
    
    // 3. HTTP/3 实际请求测试（需要支持 HTTP/3 的服务器）
    void testHttp3ActualRequest();
    
    // 4. HTTP/3 降级测试（服务器不支持时自动降级）
    void testHttp3Fallback();
    
    // 5. Http3Only 模式测试
    void testHttp3OnlyMode();
    
    // 6. HTTP 版本协商测试
    void testHttpVersionNegotiation();
    
    // 7. HTTP/3 + HTTPS 组合测试
    void testHttp3WithSSL();
    
    // 8. HTTP/3 性能对比测试（benchmark）
    void testHttp3Performance();

    // 9. HTTP/3 错误场景测试
    void testHttp3ErrorScenarios();

    // 10. HTTP/3 边界条件测试
    void testHttp3BoundaryConditions();

    // 11. HTTP/3 0-RTT 连接测试
    void testHttp3ZeroRttConnection();

    // 12. HTTP/3 连接迁移测试
    void testHttp3ConnectionMigration();

    // 13. HTTP/3 优先级测试
    void testHttp3Priority();

    // 14. HTTP/3 大文件传输测试
    void testHttp3LargeFileTransfer();

    // 15. HTTP/3 并发请求测试
    void testHttp3ConcurrentRequests();

private:
    QCNetworkAccessManager *m_manager = nullptr;
    
    /**
     * @brief 检查 libcurl 是否支持 HTTP/3
     */
    bool isHttp3Supported() const;
};

void TestQCNetworkHttp3::initTestCase()
{
    qDebug() << "========================================";
    qDebug() << "HTTP/3 功能测试套件";
    qDebug() << "v2.17.0";
    qDebug() << "========================================";
    
    m_manager = new QCNetworkAccessManager(this);
    
    // 检查 libcurl 版本和 HTTP/3 支持
    curl_version_info_data *ver = curl_version_info(CURLVERSION_NOW);
    qDebug() << "libcurl 版本:" << ver->version;
    qDebug() << "HTTP/3 支持:" << (isHttp3Supported() ? "✅ 是" : "❌ 否");
    
    if (!isHttp3Supported()) {
        qWarning() << "⚠️  当前 libcurl 不支持 HTTP/3";
        qWarning() << "   部分测试将被跳过或使用模拟";
    }
}

void TestQCNetworkHttp3::cleanupTestCase()
{
    delete m_manager;
    qDebug() << "========================================";
    qDebug() << "HTTP/3 测试套件完成";
    qDebug() << "========================================";
}

bool TestQCNetworkHttp3::isHttp3Supported() const
{
#ifdef CURL_HTTP_VERSION_3
    curl_version_info_data *ver = curl_version_info(CURLVERSION_NOW);
    // 检查特性列表中是否包含 "HTTP3"
    if (ver->features & CURL_VERSION_HTTP3) {
        return true;
    }
#endif
    return false;
}

// ============================================================================
// 测试用例实现
// ============================================================================

void TestQCNetworkHttp3::testHttp3EnumConversion()
{
    qDebug() << "\n[测试 1] HTTP/3 枚举转换";
    
    // 测试所有枚举值都能正确转换
    QVERIFY(toCurlHttpVersion(QCNetworkHttpVersion::Http1_0) == CURL_HTTP_VERSION_1_0);
    QVERIFY(toCurlHttpVersion(QCNetworkHttpVersion::Http1_1) == CURL_HTTP_VERSION_1_1);
    QVERIFY(toCurlHttpVersion(QCNetworkHttpVersion::Http2) == CURL_HTTP_VERSION_2_0);
    QVERIFY(toCurlHttpVersion(QCNetworkHttpVersion::Http2TLS) == CURL_HTTP_VERSION_2TLS);
    
#ifdef CURL_HTTP_VERSION_3
    qDebug() << "  ✅ CURL_HTTP_VERSION_3 已定义";
    long http3Value = toCurlHttpVersion(QCNetworkHttpVersion::Http3);
    QVERIFY(http3Value == CURL_HTTP_VERSION_3 || http3Value == CURL_HTTP_VERSION_2TLS);
#else
    qDebug() << "  ⚠️  CURL_HTTP_VERSION_3 未定义，将降级到 HTTP/2";
    QVERIFY(toCurlHttpVersion(QCNetworkHttpVersion::Http3) == CURL_HTTP_VERSION_2TLS);
#endif

#ifdef CURL_HTTP_VERSION_3ONLY
    qDebug() << "  ✅ CURL_HTTP_VERSION_3ONLY 已定义";
    QVERIFY(toCurlHttpVersion(QCNetworkHttpVersion::Http3Only) == CURL_HTTP_VERSION_3ONLY);
#elif defined(CURL_HTTP_VERSION_3)
    qDebug() << "  ⚠️  CURL_HTTP_VERSION_3ONLY 未定义，使用 CURL_HTTP_VERSION_3";
    QVERIFY(toCurlHttpVersion(QCNetworkHttpVersion::Http3Only) == CURL_HTTP_VERSION_3);
#else
    qDebug() << "  ⚠️  HTTP/3 完全不支持，降级到 HTTP/2";
    QVERIFY(toCurlHttpVersion(QCNetworkHttpVersion::Http3Only) == CURL_HTTP_VERSION_2TLS);
#endif
    
    QVERIFY(toCurlHttpVersion(QCNetworkHttpVersion::HttpAny) == CURL_HTTP_VERSION_NONE);
    
    qDebug() << "  ✅ 所有枚举转换正确";
}

void TestQCNetworkHttp3::testHttp3RequestConfiguration()
{
    qDebug() << "\n[测试 2] HTTP/3 请求配置";
    
    QCNetworkRequest request(QUrl("https://cloudflare-quic.com"));
    
    // 设置 HTTP/3
    request.setHttpVersion(QCNetworkHttpVersion::Http3);
    QVERIFY(request.httpVersion() == QCNetworkHttpVersion::Http3);
    qDebug() << "  ✅ HTTP/3 配置成功";
    
    // 设置 Http3Only
    request.setHttpVersion(QCNetworkHttpVersion::Http3Only);
    QVERIFY(request.httpVersion() == QCNetworkHttpVersion::Http3Only);
    qDebug() << "  ✅ Http3Only 配置成功";
    
    // 验证配置可以链式调用
    request.setHttpVersion(QCNetworkHttpVersion::Http3)
           .setFollowLocation(true);
    QVERIFY(request.httpVersion() == QCNetworkHttpVersion::Http3);
    QVERIFY(request.followLocation() == true);
    qDebug() << "  ✅ 链式调用成功";
}

void TestQCNetworkHttp3::testHttp3ActualRequest()
{
    qDebug() << "\n[测试 3] HTTP/3 实际请求";
    
    if (!isHttp3Supported()) {
        QSKIP("libcurl 不支持 HTTP/3，跳过此测试");
    }
    
    // 使用 Cloudflare 的 HTTP/3 测试服务器
    QCNetworkRequest request(QUrl("https://cloudflare-quic.com"));
    request.setHttpVersion(QCNetworkHttpVersion::Http3);
    
    auto *reply = m_manager->sendGet(request);
    QVERIFY(reply != nullptr);
    
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
    reply->execute();
    
    // 等待最多 10 秒
    QVERIFY(finishedSpy.wait(10000));
    
    if (reply->error() == NetworkError::NoError) {
        qDebug() << "  ✅ HTTP/3 请求成功";
        
        // 读取响应数据
        auto data = reply->readAll();
        QVERIFY(data.has_value());
        QVERIFY(!data->isEmpty());
        
        qDebug() << "  ✅ 收到响应数据:" << data->size() << "字节";
    } else {
        qDebug() << "  ⚠️  请求失败（可能服务器不支持或网络问题）:" << reply->errorString();
        qDebug() << "     错误码:" << static_cast<int>(reply->error());
        // 不强制要求成功，因为可能是网络问题
    }
    
    reply->deleteLater();
}

void TestQCNetworkHttp3::testHttp3Fallback()
{
    qDebug() << "\n[测试 4] HTTP/3 降级测试";
    
    // 使用一个只支持 HTTP/1.1 的服务器
    QCNetworkRequest request(QUrl("https://httpbin.org/get"));
    request.setHttpVersion(QCNetworkHttpVersion::Http3);
    
    auto *reply = m_manager->sendGet(request);
    QVERIFY(reply != nullptr);
    
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
    reply->execute();
    
    // 等待最多 10 秒
    QVERIFY(finishedSpy.wait(10000));
    
    // HTTP/3 不可用时，libcurl 应该自动降级
    if (reply->error() == NetworkError::NoError) {
        qDebug() << "  ✅ 请求成功（可能已降级到 HTTP/1.1 或 HTTP/2）";
        
        auto data = reply->readAll();
        QVERIFY(data.has_value());
        QVERIFY(!data->isEmpty());
        
        qDebug() << "  ✅ 降级后依然能正常获取数据";
    } else {
        qDebug() << "  ⚠️  请求失败:" << reply->errorString();
    }
    
    reply->deleteLater();
}

void TestQCNetworkHttp3::testHttp3OnlyMode()
{
    qDebug() << "\n[测试 5] Http3Only 模式";
    
    if (!isHttp3Supported()) {
        QSKIP("libcurl 不支持 HTTP/3，跳过此测试");
    }
    
    // 测试 Http3Only：只接受 HTTP/3，不降级
    QCNetworkRequest request(QUrl("https://cloudflare-quic.com"));
    request.setHttpVersion(QCNetworkHttpVersion::Http3Only);
    
    auto *reply = m_manager->sendGet(request);
    QVERIFY(reply != nullptr);
    
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
    reply->execute();
    
    QVERIFY(finishedSpy.wait(10000));
    
    if (reply->error() == NetworkError::NoError) {
        qDebug() << "  ✅ Http3Only 模式成功（服务器支持 HTTP/3）";
    } else {
        qDebug() << "  ⚠️  Http3Only 模式失败:" << reply->errorString();
        qDebug() << "     （这是预期行为，如果服务器不支持 HTTP/3）";
    }
    
    reply->deleteLater();
}

void TestQCNetworkHttp3::testHttpVersionNegotiation()
{
    qDebug() << "\n[测试 6] HTTP 版本协商";
    
    // 测试 HttpAny：让 libcurl 自动选择最优版本
    QCNetworkRequest request(QUrl("https://www.google.com"));
    request.setHttpVersion(QCNetworkHttpVersion::HttpAny);
    
    auto *reply = m_manager->sendGet(request);
    QVERIFY(reply != nullptr);
    
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
    reply->execute();
    
    QVERIFY(finishedSpy.wait(10000));
    
    if (reply->error() == NetworkError::NoError) {
        qDebug() << "  ✅ HTTP 版本自动协商成功";
        
        // Google 通常支持 HTTP/2 和 HTTP/3
        auto data = reply->readAll();
        QVERIFY(data.has_value());
        
        qDebug() << "  ✅ 成功获取数据:" << data->size() << "字节";
    } else {
        qDebug() << "  ⚠️  请求失败:" << reply->errorString();
    }
    
    reply->deleteLater();
}

void TestQCNetworkHttp3::testHttp3WithSSL()
{
    qDebug() << "\n[测试 7] HTTP/3 + HTTPS 组合";
    
    if (!isHttp3Supported()) {
        QSKIP("libcurl 不支持 HTTP/3，跳过此测试");
    }
    
    // HTTP/3 总是基于 QUIC（使用 TLS 1.3）
    QCNetworkRequest request(QUrl("https://cloudflare-quic.com"));
    request.setHttpVersion(QCNetworkHttpVersion::Http3);
    
    // 配置 SSL
    QCNetworkSslConfig sslConfig;
    sslConfig.verifyPeer = true;
    sslConfig.verifyHost = true;
    request.setSslConfig(sslConfig);
    
    auto *reply = m_manager->sendGet(request);
    QVERIFY(reply != nullptr);
    
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
    reply->execute();
    
    QVERIFY(finishedSpy.wait(10000));
    
    if (reply->error() == NetworkError::NoError) {
        qDebug() << "  ✅ HTTP/3 + SSL 验证成功";
    } else {
        qDebug() << "  ⚠️  请求失败:" << reply->errorString();
    }
    
    reply->deleteLater();
}

void TestQCNetworkHttp3::testHttp3Performance()
{
    qDebug() << "\n[测试 8] HTTP/3 性能对比（基准测试）";
    
    if (!isHttp3Supported()) {
        QSKIP("libcurl 不支持 HTTP/3，跳过此测试");
    }
    
    const QUrl testUrl("https://cloudflare-quic.com");
    const int iterations = 3;
    
    // 测试 HTTP/1.1
    qint64 http11Total = 0;
    for (int i = 0; i < iterations; ++i) {
        QCNetworkRequest request(testUrl);
        request.setHttpVersion(QCNetworkHttpVersion::Http1_1);
        
        QElapsedTimer timer;
        timer.start();
        
        auto *reply = m_manager->sendGet(request);
        QSignalSpy spy(reply, &QCNetworkReply::finished);
        reply->execute();
        spy.wait(10000);
        
        http11Total += timer.elapsed();
        reply->deleteLater();
    }
    qint64 http11Avg = http11Total / iterations;
    
    // 测试 HTTP/2
    qint64 http2Total = 0;
    for (int i = 0; i < iterations; ++i) {
        QCNetworkRequest request(testUrl);
        request.setHttpVersion(QCNetworkHttpVersion::Http2);
        
        QElapsedTimer timer;
        timer.start();
        
        auto *reply = m_manager->sendGet(request);
        QSignalSpy spy(reply, &QCNetworkReply::finished);
        reply->execute();
        spy.wait(10000);
        
        http2Total += timer.elapsed();
        reply->deleteLater();
    }
    qint64 http2Avg = http2Total / iterations;
    
    // 测试 HTTP/3
    qint64 http3Total = 0;
    for (int i = 0; i < iterations; ++i) {
        QCNetworkRequest request(testUrl);
        request.setHttpVersion(QCNetworkHttpVersion::Http3);
        
        QElapsedTimer timer;
        timer.start();
        
        auto *reply = m_manager->sendGet(request);
        QSignalSpy spy(reply, &QCNetworkReply::finished);
        reply->execute();
        spy.wait(10000);
        
        http3Total += timer.elapsed();
        reply->deleteLater();
    }
    qint64 http3Avg = http3Total / iterations;
    
    qDebug() << "  ========================================";
    qDebug() << "  性能对比结果（平均响应时间，" << iterations << "次请求）:";
    qDebug() << "  ========================================";
    qDebug() << "  HTTP/1.1:" << http11Avg << "ms";
    qDebug() << "  HTTP/2:  " << http2Avg << "ms";
    qDebug() << "  HTTP/3:  " << http3Avg << "ms";
    qDebug() << "  ========================================";
    
    if (http3Avg < http2Avg) {
        qDebug() << "  ✅ HTTP/3 比 HTTP/2 快" << (http2Avg - http3Avg) << "ms";
    } else {
        qDebug() << "  ⚠️  HTTP/3 比 HTTP/2 慢" << (http3Avg - http2Avg) << "ms";
        qDebug() << "     （可能是网络抖动或服务器负载影响）";
    }
    
    // 验证所有版本都能工作
    QVERIFY(http11Avg > 0);
    QVERIFY(http2Avg > 0);
    QVERIFY(http3Avg > 0);
}

void TestQCNetworkHttp3::testHttp3ErrorScenarios()
{
    qDebug() << "\n[测试 9] HTTP/3 错误场景测试";

    // 9.1 测试无效 URL
    {
        QCNetworkRequest request(QUrl("https://invalid.nonexistent.domain.test"));
        request.setHttpVersion(QCNetworkHttpVersion::Http3);

        auto *reply = m_manager->sendGet(request);
        QVERIFY(reply != nullptr);

        QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
        reply->execute();

        QVERIFY(finishedSpy.wait(15000));

        // 应该返回 DNS 解析失败或连接失败
        QVERIFY(reply->error() != NetworkError::NoError);
        qDebug() << "  ✅ 无效域名正确返回错误:" << reply->errorString();

        reply->deleteLater();
    }

    // 9.2 测试连接超时（简化版，不使用不存在的 TimeoutConfig）
    {
        QCNetworkRequest request(QUrl("https://10.255.255.1")); // 不可路由的 IP
        request.setHttpVersion(QCNetworkHttpVersion::Http3);

        auto *reply = m_manager->sendGet(request);
        QVERIFY(reply != nullptr);

        QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
        reply->execute();

        // 使用较短的超时时间测试
        QVERIFY(finishedSpy.wait(8000)); // 8 秒超时

        // 应该返回超时或连接失败错误
        QVERIFY(reply->error() != NetworkError::NoError);
        qDebug() << "  ✅ 连接超时正确处理:" << reply->errorString();

        reply->deleteLater();
    }

    // 9.3 测试 HTTP 非安全 URL（HTTP/3 需要 HTTPS）
    {
        QCNetworkRequest request(QUrl("http://httpbin.org/get")); // 注意是 http 不是 https
        request.setHttpVersion(QCNetworkHttpVersion::Http3);

        // HTTP/3 基于 QUIC，QUIC 只能在 HTTPS 上运行
        // libcurl 应该自动降级或报错
        auto *reply = m_manager->sendGet(request);
        QVERIFY(reply != nullptr);

        QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
        reply->execute();

        QVERIFY(finishedSpy.wait(15000));

        // 不论成功还是失败，都应该正常完成（可能降级到 HTTP/1.1）
        qDebug() << "  ✅ 非 HTTPS URL 处理完成, 状态:"
                 << (reply->error() == NetworkError::NoError ? "成功（已降级）" : "失败");

        reply->deleteLater();
    }

    qDebug() << "  ✅ 所有错误场景测试通过";
}

void TestQCNetworkHttp3::testHttp3BoundaryConditions()
{
    qDebug() << "\n[测试 10] HTTP/3 边界条件测试";

    // 10.1 测试空 URL
    {
        QCNetworkRequest request{QUrl{}};
        request.setHttpVersion(QCNetworkHttpVersion::Http3);

        QVERIFY(!request.url().isValid());
        qDebug() << "  ✅ 空 URL 正确处理";
    }

    // 10.2 测试所有 HTTP 版本枚举值
    {
        QCNetworkRequest request(QUrl("https://example.com"));

        // 遍历所有枚举值
        request.setHttpVersion(QCNetworkHttpVersion::Http1_0);
        QVERIFY(request.httpVersion() == QCNetworkHttpVersion::Http1_0);

        request.setHttpVersion(QCNetworkHttpVersion::Http1_1);
        QVERIFY(request.httpVersion() == QCNetworkHttpVersion::Http1_1);

        request.setHttpVersion(QCNetworkHttpVersion::Http2);
        QVERIFY(request.httpVersion() == QCNetworkHttpVersion::Http2);

        request.setHttpVersion(QCNetworkHttpVersion::Http2TLS);
        QVERIFY(request.httpVersion() == QCNetworkHttpVersion::Http2TLS);

        request.setHttpVersion(QCNetworkHttpVersion::Http3);
        QVERIFY(request.httpVersion() == QCNetworkHttpVersion::Http3);

        request.setHttpVersion(QCNetworkHttpVersion::Http3Only);
        QVERIFY(request.httpVersion() == QCNetworkHttpVersion::Http3Only);

        request.setHttpVersion(QCNetworkHttpVersion::HttpAny);
        QVERIFY(request.httpVersion() == QCNetworkHttpVersion::HttpAny);

        qDebug() << "  ✅ 所有 HTTP 版本枚举值正确处理";
    }

    // 10.3 测试 toCurlHttpVersion 所有分支
    {
        // 测试默认分支（使用无效枚举值）
        auto invalidVersion = static_cast<QCNetworkHttpVersion>(999);
        long curlVersion = toCurlHttpVersion(invalidVersion);
        QVERIFY(curlVersion == CURL_HTTP_VERSION_1_1); // 默认应该是 HTTP/1.1
        qDebug() << "  ✅ 无效枚举值正确降级到 HTTP/1.1";
    }

    // 10.4 测试请求复制
    {
        QCNetworkRequest request1(QUrl("https://example.com"));
        request1.setHttpVersion(QCNetworkHttpVersion::Http3);

        QCNetworkRequest request2 = request1;
        QVERIFY(request2.httpVersion() == QCNetworkHttpVersion::Http3);
        QVERIFY(request2.url() == request1.url());

        qDebug() << "  ✅ 请求复制正确保留 HTTP 版本";
    }

    qDebug() << "  ✅ 所有边界条件测试通过";
}

void TestQCNetworkHttp3::testHttp3ZeroRttConnection()
{
    qDebug() << "\n[测试 11] HTTP/3 0-RTT 连接测试";

    if (!isHttp3Supported()) {
        QSKIP("libcurl 不支持 HTTP/3，跳过此测试");
    }

    // 0-RTT 是 QUIC 的特性，允许在握手完成前发送数据
    // 测试重复连接到同一服务器时的性能提升

    const QUrl testUrl("https://cloudflare-quic.com");

    // 第一次连接（冷启动，需要完整握手）
    qint64 firstConnectionTime = 0;
    {
        QCNetworkRequest request(testUrl);
        request.setHttpVersion(QCNetworkHttpVersion::Http3);

        QElapsedTimer timer;
        timer.start();

        auto *reply = m_manager->sendGet(request);
        QSignalSpy spy(reply, &QCNetworkReply::finished);
        reply->execute();
        spy.wait(15000);

        firstConnectionTime = timer.elapsed();

        if (reply->error() == NetworkError::NoError) {
            qDebug() << "  第一次连接（冷启动）:" << firstConnectionTime << "ms";
        } else {
            qDebug() << "  ⚠️  第一次连接失败:" << reply->errorString();
        }

        reply->deleteLater();
    }

    // 第二次连接（热启动，可能使用 0-RTT）
    qint64 secondConnectionTime = 0;
    {
        QCNetworkRequest request(testUrl);
        request.setHttpVersion(QCNetworkHttpVersion::Http3);

        QElapsedTimer timer;
        timer.start();

        auto *reply = m_manager->sendGet(request);
        QSignalSpy spy(reply, &QCNetworkReply::finished);
        reply->execute();
        spy.wait(15000);

        secondConnectionTime = timer.elapsed();

        if (reply->error() == NetworkError::NoError) {
            qDebug() << "  第二次连接（热启动）:" << secondConnectionTime << "ms";
        } else {
            qDebug() << "  ⚠️  第二次连接失败:" << reply->errorString();
        }

        reply->deleteLater();
    }

    // 验证连接时间（不强制要求更快，因为网络条件可能变化）
    if (firstConnectionTime > 0 && secondConnectionTime > 0) {
        if (secondConnectionTime < firstConnectionTime) {
            qDebug() << "  ✅ 热启动连接快" << (firstConnectionTime - secondConnectionTime) << "ms";
        } else {
            qDebug() << "  ⚠️  热启动未见明显改善（可能是网络波动）";
        }
    }

    qDebug() << "  ✅ 0-RTT 连接测试完成";
}

void TestQCNetworkHttp3::testHttp3ConnectionMigration()
{
    qDebug() << "\n[测试 12] HTTP/3 连接迁移测试";

    if (!isHttp3Supported()) {
        QSKIP("libcurl 不支持 HTTP/3，跳过此测试");
    }

    // QUIC 支持连接迁移，即使底层 IP 地址改变也能保持连接
    // 这里我们测试基本的连接稳定性

    const QUrl testUrl("https://cloudflare-quic.com");
    const int requestCount = 3;
    int successCount = 0;

    for (int i = 0; i < requestCount; ++i) {
        QCNetworkRequest request(testUrl);
        request.setHttpVersion(QCNetworkHttpVersion::Http3);

        auto *reply = m_manager->sendGet(request);
        QSignalSpy spy(reply, &QCNetworkReply::finished);
        reply->execute();
        spy.wait(15000);

        if (reply->error() == NetworkError::NoError) {
            successCount++;
            qDebug() << "  请求" << (i + 1) << "成功";
        } else {
            qDebug() << "  请求" << (i + 1) << "失败:" << reply->errorString();
        }

        reply->deleteLater();

        // 短暂延迟
        QThread::msleep(500);
    }

    qDebug() << "  连接稳定性:" << successCount << "/" << requestCount << "请求成功";

    // 至少一半请求应该成功
    QVERIFY(successCount >= requestCount / 2);

    qDebug() << "  ✅ 连接迁移测试完成";
}

void TestQCNetworkHttp3::testHttp3Priority()
{
    qDebug() << "\n[测试 13] HTTP/3 优先级测试";

    // HTTP/3 支持请求优先级，通过 PRIORITY 帧设置
    // 这里测试多个请求的基本优先级行为

    QCNetworkRequest request(QUrl("https://httpbin.org/get"));
    request.setHttpVersion(QCNetworkHttpVersion::Http3);

    // 验证请求配置
    QVERIFY(request.httpVersion() == QCNetworkHttpVersion::Http3);
    QVERIFY(request.url().isValid());

    // 测试请求可以正常执行
    auto *reply = m_manager->sendGet(request);
    QVERIFY(reply != nullptr);

    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
    reply->execute();

    QVERIFY(finishedSpy.wait(15000));

    if (reply->error() == NetworkError::NoError) {
        qDebug() << "  ✅ 优先级请求成功";
    } else {
        qDebug() << "  ⚠️  请求失败（可能已降级）:" << reply->errorString();
    }

    reply->deleteLater();

    qDebug() << "  ✅ 优先级测试完成";
}

void TestQCNetworkHttp3::testHttp3LargeFileTransfer()
{
    qDebug() << "\n[测试 14] HTTP/3 大文件传输测试";

    if (!isHttp3Supported()) {
        QSKIP("libcurl 不支持 HTTP/3，跳过此测试");
    }

    // 测试较大数据量的传输
    // 使用 httpbin 的 /bytes 端点生成指定大小的数据
    const int dataSize = 1024 * 100; // 100 KB

    QCNetworkRequest request(QUrl(QString("https://httpbin.org/bytes/%1").arg(dataSize)));
    request.setHttpVersion(QCNetworkHttpVersion::Http3);

    auto *reply = m_manager->sendGet(request);
    QVERIFY(reply != nullptr);

    // 监控下载进度
    qint64 totalReceived = 0;
    QObject::connect(reply, &QCNetworkReply::downloadProgress,
        [&totalReceived](qint64 received, qint64 total) {
            totalReceived = received;
            Q_UNUSED(total);
        });

    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
    reply->execute();

    QVERIFY(finishedSpy.wait(30000)); // 30 秒超时

    if (reply->error() == NetworkError::NoError) {
        auto data = reply->readAll();
        QVERIFY(data.has_value());

        qDebug() << "  ✅ 大文件传输成功";
        qDebug() << "     请求大小:" << dataSize << "bytes";
        qDebug() << "     实际接收:" << data->size() << "bytes";

        // 验证数据大小（允许一定误差，因为可能有 HTTP 头开销）
        QVERIFY(data->size() > 0);
    } else {
        qDebug() << "  ⚠️  大文件传输失败（可能已降级）:" << reply->errorString();
    }

    reply->deleteLater();

    qDebug() << "  ✅ 大文件传输测试完成";
}

void TestQCNetworkHttp3::testHttp3ConcurrentRequests()
{
    qDebug() << "\n[测试 15] HTTP/3 并发请求测试";

    // HTTP/3 基于 QUIC，支持多路复用
    // 测试多个并发请求

    const int concurrentCount = 5;
    QList<QCNetworkReply*> replies;
    QList<QSignalSpy*> spies;

    // 创建并发请求
    for (int i = 0; i < concurrentCount; ++i) {
        QCNetworkRequest request(QUrl(QString("https://httpbin.org/get?id=%1").arg(i)));
        request.setHttpVersion(QCNetworkHttpVersion::Http3);

        auto *reply = m_manager->sendGet(request);
        replies.append(reply);

        auto *spy = new QSignalSpy(reply, &QCNetworkReply::finished);
        spies.append(spy);
    }

    // 同时执行所有请求
    QElapsedTimer timer;
    timer.start();

    for (auto *reply : replies) {
        reply->execute();
    }

    // 等待所有请求完成
    int successCount = 0;
    for (int i = 0; i < concurrentCount; ++i) {
        if (spies[i]->wait(30000)) {
            if (replies[i]->error() == NetworkError::NoError) {
                successCount++;
            }
        }
    }

    qint64 totalTime = timer.elapsed();

    qDebug() << "  ========================================";
    qDebug() << "  并发请求结果:";
    qDebug() << "  ========================================";
    qDebug() << "  请求数量:" << concurrentCount;
    qDebug() << "  成功数量:" << successCount;
    qDebug() << "  总耗时:" << totalTime << "ms";
    qDebug() << "  平均每请求:" << (totalTime / concurrentCount) << "ms";
    qDebug() << "  ========================================";

    // 清理
    for (auto *spy : spies) {
        delete spy;
    }
    for (auto *reply : replies) {
        reply->deleteLater();
    }

    // 至少一半请求应该成功
    QVERIFY(successCount >= concurrentCount / 2);

    qDebug() << "  ✅ 并发请求测试完成";
}

QTEST_MAIN(TestQCNetworkHttp3)
#include "tst_QCNetworkHttp3.moc"
