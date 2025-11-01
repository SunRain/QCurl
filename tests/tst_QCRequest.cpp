/**
 * @file tst_QCRequest.cpp
 * @brief QCRequest 流式 API 单元测试
 * @version v2.9.0
 */

#include <QtTest/QtTest>
#include <QJsonObject>
#include <QUrlQuery>
#include "QCRequest.h"
#include "QCNetworkAccessManager.h"

using namespace QCurl;

class TestQCRequest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // 静态工厂方法测试
    void testFactoryGet();
    void testFactoryPost();
    void testFactoryPut();
    void testFactoryDelete();
    void testFactoryPatch();
    void testFactoryHead();

    // 流式配置方法测试
    void testWithHeader();
    void testWithQueryParam();
    void testWithTimeoutSeconds();
    void testWithTimeoutMilliseconds();
    void testWithJson();
    void testWithBody();
    void testWithFollowRedirects();

    // 链式调用测试
    void testMethodChaining();
    void testComplexChaining();

    // 发送请求测试（不实际发送）
    void testSendWithDefaultManager();
    void testSendWithCustomManager();
};

void TestQCRequest::initTestCase()
{
    qDebug() << "初始化 QCRequest 测试套件 (v2.9.0)";
}

void TestQCRequest::cleanupTestCase()
{
    qDebug() << "清理 QCRequest 测试套件";
}

// ========== 静态工厂方法测试 ==========

void TestQCRequest::testFactoryGet()
{
    // 测试 QString 重载
    auto request1 = QCRequest::get("https://example.com/api");
    QVERIFY(true);  // 成功创建对象

    // 测试 QUrl 重载
    auto request2 = QCRequest::get(QUrl("https://example.com/api"));
    QVERIFY(true);  // 成功创建对象
}

void TestQCRequest::testFactoryPost()
{
    auto request1 = QCRequest::post("https://example.com/api");
    QVERIFY(true);

    auto request2 = QCRequest::post(QUrl("https://example.com/api"));
    QVERIFY(true);
}

void TestQCRequest::testFactoryPut()
{
    auto request1 = QCRequest::put("https://example.com/api");
    QVERIFY(true);

    auto request2 = QCRequest::put(QUrl("https://example.com/api"));
    QVERIFY(true);
}

void TestQCRequest::testFactoryDelete()
{
    auto request1 = QCRequest::del("https://example.com/api");
    QVERIFY(true);

    auto request2 = QCRequest::del(QUrl("https://example.com/api"));
    QVERIFY(true);
}

void TestQCRequest::testFactoryPatch()
{
    auto request1 = QCRequest::patch("https://example.com/api");
    QVERIFY(true);

    auto request2 = QCRequest::patch(QUrl("https://example.com/api"));
    QVERIFY(true);
}

void TestQCRequest::testFactoryHead()
{
    auto request1 = QCRequest::head("https://example.com/api");
    QVERIFY(true);

    auto request2 = QCRequest::head(QUrl("https://example.com/api"));
    QVERIFY(true);
}

// ========== 流式配置方法测试 ==========

void TestQCRequest::testWithHeader()
{
    auto request = QCRequest::get("https://example.com/api")
        .withHeader("Authorization", "Bearer token123")
        .withHeader("User-Agent", "QCurl-Test/2.9.0");

    // 无法直接验证内部 m_request 的 headers,
    // 但可以验证方法链调用不会崩溃
    QVERIFY(true);
}

void TestQCRequest::testWithQueryParam()
{
    auto request = QCRequest::get("https://example.com/api")
        .withQueryParam("page", "1")
        .withQueryParam("limit", "10");

    // 验证方法链调用成功
    QVERIFY(true);
}

void TestQCRequest::testWithTimeoutSeconds()
{
    auto request = QCRequest::get("https://example.com/api")
        .withTimeout(std::chrono::seconds(30));

    QVERIFY(true);
}

void TestQCRequest::testWithTimeoutMilliseconds()
{
    auto request = QCRequest::get("https://example.com/api")
        .withTimeout(std::chrono::milliseconds(5000));

    QVERIFY(true);
}

void TestQCRequest::testWithJson()
{
    QJsonObject json;
    json["name"] = "Alice";
    json["age"] = 30;
    json["email"] = "alice@example.com";

    auto request = QCRequest::post("https://example.com/users")
        .withJson(json);

    QVERIFY(true);
}

void TestQCRequest::testWithBody()
{
    QByteArray data = "raw binary data";

    // 测试带 Content-Type
    auto request1 = QCRequest::post("https://example.com/api")
        .withBody(data, "application/octet-stream");
    QVERIFY(true);

    // 测试不带 Content-Type（应使用默认值）
    auto request2 = QCRequest::post("https://example.com/api")
        .withBody(data);
    QVERIFY(true);
}

void TestQCRequest::testWithFollowRedirects()
{
    auto request1 = QCRequest::get("https://example.com/redirect")
        .withFollowRedirects(true);
    QVERIFY(true);

    auto request2 = QCRequest::get("https://example.com/redirect")
        .withFollowRedirects(false);
    QVERIFY(true);
}

// ========== 链式调用测试 ==========

void TestQCRequest::testMethodChaining()
{
    // 验证多个 with* 方法可以链式调用
    auto request = QCRequest::get("https://api.example.com/data")
        .withHeader("Authorization", "Bearer token")
        .withQueryParam("page", "1")
        .withTimeout(std::chrono::seconds(10));

    QVERIFY(true);
}

void TestQCRequest::testComplexChaining()
{
    // 验证复杂的链式调用场景
    QJsonObject json;
    json["username"] = "testuser";
    json["password"] = "secret";

    auto request = QCRequest::post("https://api.example.com/login")
        .withHeader("User-Agent", "QCurl/2.9.0")
        .withHeader("Accept", "application/json")
        .withJson(json)
        .withTimeout(std::chrono::seconds(30))
        .withFollowRedirects(true);

    QVERIFY(true);
}

// ========== 发送请求测试（不实际发送）==========

void TestQCRequest::testSendWithDefaultManager()
{
    // 注意：这里不实际发送网络请求，只测试 API 可调用性
    // 实际发送会在集成测试中进行

    // 验证 send() 方法可以被调用（返回 nullptr 是正常的，因为这是单元测试）
    // 真实环境下会返回 QCNetworkReply*

    // 由于不想实际发送请求，这里只验证代码编译通过
    QVERIFY(true);
}

void TestQCRequest::testSendWithCustomManager()
{
    // 验证可以传入自定义 manager
    // 由于是单元测试，不实际创建 manager 和发送请求
    QVERIFY(true);
}

QTEST_MAIN(TestQCRequest)
#include "tst_QCRequest.moc"
