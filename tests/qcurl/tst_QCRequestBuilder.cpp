/**
 * @file tst_QCRequestBuilder.cpp
 * @brief QCRequestBuilder 传统构建器 API 单元测试
 * @version v2.9.0
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkRequest.h"
#include "QCNetworkReply.h"
#include "QCNetworkTimeoutConfig.h"
#include "QCRequestBuilder.h"

#include <QCoreApplication>
#include <QEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

#include <optional>

using namespace QCurl;

static std::optional<QByteArray> findHeaderValue(const QList<QPair<QByteArray, QByteArray>> &headers,
                                                 const QByteArray &nameLower)
{
    for (const auto &kv : headers) {
        if (kv.first.trimmed().toLower() == nameLower) {
            return kv.second;
        }
    }
    return std::nullopt;
}

class TestQCRequestBuilder : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // 基本配置方法测试
    void testSetUrl();
    void testSetMethod();
    void testAddHeader();
    void testAddQueryParam();
    void testSetTimeout();
    void testSetTimeoutMs();

    // 请求体配置测试
    void testSetBody();
    void testSetJsonBody();
    void testSetContentType();

    // 高级配置测试
    void testSetFollowRedirects();

    // 构建方法测试
    void testBuild();
    void testBuildEmptyUrl();
    void testReset();

    // 方法链测试
    void testMethodChaining();
    void testComplexBuilder();

private:
    QCNetworkAccessManager *m_manager = nullptr;
    QCNetworkMockHandler m_mock;
};

void TestQCRequestBuilder::initTestCase()
{
    qDebug() << "初始化 QCRequestBuilder 测试套件 (v2.9.0)";
}

void TestQCRequestBuilder::cleanupTestCase()
{
    qDebug() << "清理 QCRequestBuilder 测试套件";
}

void TestQCRequestBuilder::init()
{
    m_manager = new QCNetworkAccessManager(this);

    m_mock.clear();
    m_mock.clearCapturedRequests();
    m_mock.setCaptureEnabled(true);
    m_mock.setGlobalDelay(0);
    m_manager->setMockHandler(&m_mock);
}

void TestQCRequestBuilder::cleanup()
{
    if (m_manager) {
        m_manager->deleteLater();
        m_manager = nullptr;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

// ========== 基本配置方法测试 ==========

void TestQCRequestBuilder::testSetUrl()
{
    QCRequestBuilder builder;

    // 测试 QString 重载
    builder.setUrl("https://example.com/api");
    auto request1 = builder.build();
    QCOMPARE(request1.url().toString(), QStringLiteral("https://example.com/api"));

    // 测试 QUrl 重载
    builder.setUrl(QUrl("https://example.org/test"));
    auto request2 = builder.build();
    QCOMPARE(request2.url().toString(), QStringLiteral("https://example.org/test"));
}

void TestQCRequestBuilder::testSetMethod()
{
    const QUrl url("http://example.com/api");
    const QByteArray body("DATA");

    struct MethodCase
    {
        QCRequestBuilder::Method builderMethod;
        HttpMethod expectedMethod;
        bool expectBody;
    };
    const QList<MethodCase> cases = {
        { QCRequestBuilder::GET, HttpMethod::Get, false },
        { QCRequestBuilder::POST, HttpMethod::Post, true },
        { QCRequestBuilder::PUT, HttpMethod::Put, true },
        { QCRequestBuilder::DELETE, HttpMethod::Delete, true },
        { QCRequestBuilder::PATCH, HttpMethod::Patch, true },
        { QCRequestBuilder::HEAD, HttpMethod::Head, false },
    };

    for (const auto &c : cases) {
        m_mock.mockResponse(c.expectedMethod, url, QByteArray("OK"));
        m_mock.clearCapturedRequests();

        QCRequestBuilder builder;
        builder.setUrl(url).setMethod(c.builderMethod);
        if (c.expectBody) {
            builder.setBody(body);
        }

        auto *reply = builder.send(m_manager);
        QVERIFY(reply != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
        QCOMPARE(reply->error(), NetworkError::NoError);

        const auto captured = m_mock.takeCapturedRequests();
        QCOMPARE(captured.size(), 1);
        QCOMPARE(captured.first().url, url);
        QCOMPARE(captured.first().method, c.expectedMethod);
        if (c.expectBody) {
            QCOMPARE(captured.first().bodySize, body.size());
            QCOMPARE(captured.first().bodyPreview, body);
        } else {
            QCOMPARE(captured.first().bodySize, 0);
        }

        reply->deleteLater();
    }
}

void TestQCRequestBuilder::testAddHeader()
{
    QCRequestBuilder builder;
    builder.setUrl("https://example.com")
        .addHeader("Authorization", "Bearer token123")
        .addHeader("User-Agent", "QCurl-Test/2.9.0");

    auto request = builder.build();

    // 验证 headers 被设置（通过 rawHeader 方法）
    QCOMPARE(request.rawHeader("Authorization"), QByteArray("Bearer token123"));
    QCOMPARE(request.rawHeader("User-Agent"), QByteArray("QCurl-Test/2.9.0"));
}

void TestQCRequestBuilder::testAddQueryParam()
{
    QCRequestBuilder builder;
    builder.setUrl("https://example.com/api").addQueryParam("page", "1").addQueryParam("limit", "10");

    auto request      = builder.build();
    QString urlString = request.url().toString();

    // 验证查询参数被添加
    QVERIFY(urlString.contains("page=1"));
    QVERIFY(urlString.contains("limit=10"));
}

void TestQCRequestBuilder::testSetTimeout()
{
    QCRequestBuilder builder;
    builder.setUrl("https://example.com").setTimeout(30);

    auto request = builder.build();
    const auto timeout = request.timeoutConfig();
    QVERIFY(timeout.totalTimeout.has_value());
    QCOMPARE(timeout.totalTimeout->count(), std::chrono::milliseconds(30000).count());
}

void TestQCRequestBuilder::testSetTimeoutMs()
{
    QCRequestBuilder builder;
    builder.setUrl("https://example.com").setTimeoutMs(5000);

    auto request = builder.build();
    const auto timeout = request.timeoutConfig();
    QVERIFY(timeout.totalTimeout.has_value());
    QCOMPARE(timeout.totalTimeout->count(), std::chrono::milliseconds(5000).count());
}

// ========== 请求体配置测试 ==========

void TestQCRequestBuilder::testSetBody()
{
    const QUrl url("http://example.com/body");
    const QByteArray body("raw test data");
    m_mock.mockResponse(HttpMethod::Post, url, QByteArray("OK"));
    m_mock.clearCapturedRequests();

    QCRequestBuilder builder;
    builder.setUrl(url).setMethod(QCRequestBuilder::POST).setBody(body);

    auto *reply = builder.send(m_manager);
    QVERIFY(reply != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    QCOMPARE(reply->error(), NetworkError::NoError);

    const auto captured = m_mock.takeCapturedRequests();
    QCOMPARE(captured.size(), 1);
    QCOMPARE(captured.first().method, HttpMethod::Post);
    QCOMPARE(captured.first().bodySize, body.size());
    QCOMPARE(captured.first().bodyPreview, body);

    reply->deleteLater();
}

void TestQCRequestBuilder::testSetJsonBody()
{
    QCRequestBuilder builder;
    QJsonObject json;
    json["name"] = "Alice";
    json["age"]  = 30;

    const QUrl url("http://example.com/users");
    builder.setUrl(url).setMethod(QCRequestBuilder::POST).setJsonBody(json);

    const auto request = builder.build();
    QCOMPARE(request.rawHeader("Content-Type"), QByteArray("application/json"));

    const QByteArray expectedBody = QJsonDocument(json).toJson(QJsonDocument::Compact);
    m_mock.mockResponse(HttpMethod::Post, url, QByteArray("OK"));
    m_mock.clearCapturedRequests();

    auto *reply = builder.send(m_manager);
    QVERIFY(reply != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    QCOMPARE(reply->error(), NetworkError::NoError);

    const auto captured = m_mock.takeCapturedRequests();
    QCOMPARE(captured.size(), 1);
    QCOMPARE(captured.first().method, HttpMethod::Post);
    QCOMPARE(captured.first().bodySize, expectedBody.size());
    QCOMPARE(captured.first().bodyPreview, expectedBody);

    const auto contentType = findHeaderValue(captured.first().headers, QByteArrayLiteral("content-type"));
    QVERIFY(contentType.has_value());
    QCOMPARE(*contentType, QByteArray("application/json"));

    reply->deleteLater();
}

void TestQCRequestBuilder::testSetContentType()
{
    QCRequestBuilder builder;
    builder.setUrl("https://example.com").setContentType("text/xml");

    auto request = builder.build();
    QCOMPARE(request.rawHeader("Content-Type"), QByteArray("text/xml"));
}

// ========== 高级配置测试 ==========

void TestQCRequestBuilder::testSetFollowRedirects()
{
    QCRequestBuilder builder;

    builder.setUrl("https://example.com").setFollowRedirects(true);
    auto request1 = builder.build();
    QVERIFY(request1.followLocation());

    builder.setFollowRedirects(false);
    auto request2 = builder.build();
    QVERIFY(!request2.followLocation());
}

// ========== 构建方法测试 ==========

void TestQCRequestBuilder::testBuild()
{
    QCRequestBuilder builder;
    builder.setUrl("https://example.com/test");

    auto request = builder.build();

    // 验证构建的 request 有效
    QVERIFY(!request.url().isEmpty());
    QCOMPARE(request.url().toString(), QStringLiteral("https://example.com/test"));
}

void TestQCRequestBuilder::testBuildEmptyUrl()
{
    QCRequestBuilder builder;
    // 不设置 URL，直接构建

    auto request = builder.build();

    // 应该返回一个空 URL 的 request（有警告但不崩溃）
    QVERIFY(request.url().isEmpty());
}

void TestQCRequestBuilder::testReset()
{
    QCRequestBuilder builder;
    builder.setUrl("https://example.com")
        .setMethod(QCRequestBuilder::POST)
        .addHeader("Authorization", "Bearer token")
        .setTimeout(30);

    // 重置构建器
    builder.reset();

    // 再次构建，应该是默认状态
    auto request = builder.build();
    QVERIFY(request.url().isEmpty());
    QVERIFY(request.rawHeader("Authorization").isEmpty());
}

// ========== 方法链测试 ==========

void TestQCRequestBuilder::testMethodChaining()
{
    QCRequestBuilder builder;

    // 验证所有方法都返回引用，可以链式调用
    auto request = builder.setUrl("https://api.example.com")
                       .setMethod(QCRequestBuilder::GET)
                       .addHeader("Accept", "application/json")
                       .addQueryParam("version", "v1")
                       .setTimeout(20)
                       .build();

    QVERIFY(!request.url().isEmpty());
    QCOMPARE(request.rawHeader("Accept"), QByteArray("application/json"));
}

void TestQCRequestBuilder::testComplexBuilder()
{
    QCRequestBuilder builder;

    QJsonObject json;
    json["username"] = "testuser";
    json["email"]    = "test@example.com";

    auto request = builder.setUrl("https://api.example.com/register")
                       .setMethod(QCRequestBuilder::POST)
                       .addHeader("User-Agent", "QCurl/2.9.0")
                       .addHeader("Accept", "application/json")
                       .setJsonBody(json)
                       .setTimeout(30)
                       .setFollowRedirects(true)
                       .build();

    // 验证多个配置都生效
    QCOMPARE(request.url().toString(), QStringLiteral("https://api.example.com/register"));
    QCOMPARE(request.rawHeader("User-Agent"), QByteArray("QCurl/2.9.0"));
    QCOMPARE(request.rawHeader("Accept"), QByteArray("application/json"));
    QCOMPARE(request.rawHeader("Content-Type"), QByteArray("application/json"));
    QVERIFY(request.followLocation());
}

QTEST_MAIN(TestQCRequestBuilder)
#include "tst_QCRequestBuilder.moc"
