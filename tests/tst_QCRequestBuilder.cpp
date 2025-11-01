/**
 * @file tst_QCRequestBuilder.cpp
 * @brief QCRequestBuilder 传统构建器 API 单元测试
 * @version v2.9.0
 */

#include <QtTest/QtTest>
#include <QJsonObject>
#include "QCRequestBuilder.h"
#include "QCNetworkRequest.h"

using namespace QCurl;

class TestQCRequestBuilder : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

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
};

void TestQCRequestBuilder::initTestCase()
{
    qDebug() << "初始化 QCRequestBuilder 测试套件 (v2.9.0)";
}

void TestQCRequestBuilder::cleanupTestCase()
{
    qDebug() << "清理 QCRequestBuilder 测试套件";
}

// ========== 基本配置方法测试 ==========

void TestQCRequestBuilder::testSetUrl()
{
    QCRequestBuilder builder;

    // 测试 QString 重载
    builder.setUrl("https://example.com/api");
    auto request1 = builder.build();
    QCOMPARE(request1.url().toString(), QString("https://example.com/api"));

    // 测试 QUrl 重载
    builder.setUrl(QUrl("https://example.org/test"));
    auto request2 = builder.build();
    QCOMPARE(request2.url().toString(), QString("https://example.org/test"));
}

void TestQCRequestBuilder::testSetMethod()
{
    QCRequestBuilder builder;

    // 方法枚举设置（不会直接体现在 QCNetworkRequest 中，但不应崩溃）
    builder.setUrl("https://example.com")
           .setMethod(QCRequestBuilder::GET);
    QVERIFY(true);

    builder.setMethod(QCRequestBuilder::POST);
    QVERIFY(true);

    builder.setMethod(QCRequestBuilder::PUT);
    QVERIFY(true);

    builder.setMethod(QCRequestBuilder::DELETE);
    QVERIFY(true);

    builder.setMethod(QCRequestBuilder::PATCH);
    QVERIFY(true);

    builder.setMethod(QCRequestBuilder::HEAD);
    QVERIFY(true);
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
    builder.setUrl("https://example.com/api")
           .addQueryParam("page", "1")
           .addQueryParam("limit", "10");

    auto request = builder.build();
    QString urlString = request.url().toString();

    // 验证查询参数被添加
    QVERIFY(urlString.contains("page=1"));
    QVERIFY(urlString.contains("limit=10"));
}

void TestQCRequestBuilder::testSetTimeout()
{
    QCRequestBuilder builder;
    builder.setUrl("https://example.com")
           .setTimeout(30);

    auto request = builder.build();
    // 超时值无法直接从 QCNetworkRequest 读取，但可以验证不崩溃
    QVERIFY(true);
}

void TestQCRequestBuilder::testSetTimeoutMs()
{
    QCRequestBuilder builder;
    builder.setUrl("https://example.com")
           .setTimeoutMs(5000);

    auto request = builder.build();
    QVERIFY(true);
}

// ========== 请求体配置测试 ==========

void TestQCRequestBuilder::testSetBody()
{
    QCRequestBuilder builder;
    QByteArray data = "raw test data";

    builder.setUrl("https://example.com")
           .setMethod(QCRequestBuilder::POST)
           .setBody(data);

    auto request = builder.build();
    // body 数据无法直接从 QCNetworkRequest 读取，但可以验证不崩溃
    QVERIFY(true);
}

void TestQCRequestBuilder::testSetJsonBody()
{
    QCRequestBuilder builder;
    QJsonObject json;
    json["name"] = "Alice";
    json["age"] = 30;

    builder.setUrl("https://example.com/users")
           .setMethod(QCRequestBuilder::POST)
           .setJsonBody(json);

    auto request = builder.build();

    // 验证 Content-Type 被自动设置
    QCOMPARE(request.rawHeader("Content-Type"), QByteArray("application/json"));
}

void TestQCRequestBuilder::testSetContentType()
{
    QCRequestBuilder builder;
    builder.setUrl("https://example.com")
           .setContentType("text/xml");

    auto request = builder.build();
    QCOMPARE(request.rawHeader("Content-Type"), QByteArray("text/xml"));
}

// ========== 高级配置测试 ==========

void TestQCRequestBuilder::testSetFollowRedirects()
{
    QCRequestBuilder builder;

    builder.setUrl("https://example.com")
           .setFollowRedirects(true);
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
    QCOMPARE(request.url().toString(), QString("https://example.com/test"));
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
    auto request = builder
        .setUrl("https://api.example.com")
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
    json["email"] = "test@example.com";

    auto request = builder
        .setUrl("https://api.example.com/register")
        .setMethod(QCRequestBuilder::POST)
        .addHeader("User-Agent", "QCurl/2.9.0")
        .addHeader("Accept", "application/json")
        .setJsonBody(json)
        .setTimeout(30)
        .setFollowRedirects(true)
        .build();

    // 验证多个配置都生效
    QCOMPARE(request.url().toString(), QString("https://api.example.com/register"));
    QCOMPARE(request.rawHeader("User-Agent"), QByteArray("QCurl/2.9.0"));
    QCOMPARE(request.rawHeader("Accept"), QByteArray("application/json"));
    QCOMPARE(request.rawHeader("Content-Type"), QByteArray("application/json"));
    QVERIFY(request.followLocation());
}

QTEST_MAIN(TestQCRequestBuilder)
#include "tst_QCRequestBuilder.moc"
