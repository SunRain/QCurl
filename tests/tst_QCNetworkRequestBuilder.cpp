// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include <QtTest>
#include <QSignalSpy>
#include <QUrl>
#include <QUrlQuery>

#include "QCNetworkAccessManager.h"
#include "QCNetworkRequest.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequestBuilder.h"

using namespace QCurl;

/**
 * @brief RequestBuilder 单元测试
 *
 * 测试流式构建器的链式调用、参数设置和请求发送。
 *
 */
class TestQCNetworkRequestBuilder : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // 基础功能测试
    void testCreateBuilder();
    void testChainedCalls();
    void testWithHeader();
    void testWithTimeout();

    // 参数设置测试
    void testWithQueryParams();
    void testWithFollowLocation();

    // 请求发送测试
    void testSendGet();
    void testSendPost();

private:
    QCNetworkAccessManager *manager = nullptr;
};

void TestQCNetworkRequestBuilder::initTestCase()
{
    qDebug() << "=== TestQCNetworkRequestBuilder Test Suite ===";
}

void TestQCNetworkRequestBuilder::cleanupTestCase()
{
    qDebug() << "=== TestQCNetworkRequestBuilder Completed ===";
}

void TestQCNetworkRequestBuilder::init()
{
    manager = new QCNetworkAccessManager(this);
}

void TestQCNetworkRequestBuilder::cleanup()
{
    if (manager) {
        delete manager;
        manager = nullptr;
    }
}

/**
 * @brief 测试创建 Builder
 */
void TestQCNetworkRequestBuilder::testCreateBuilder()
{
    // Arrange
    QUrl url("http://example.com/api");

    // Act
    auto *builder = manager->newRequest(url);

    // Assert
    QVERIFY(builder != nullptr);

    // Cleanup
    delete builder;
}

/**
 * @brief 测试链式调用
 */
void TestQCNetworkRequestBuilder::testChainedCalls()
{
    // Arrange
    QUrl url("http://example.com/api");
    auto *builder = manager->newRequest(url);

    // Act - 链式调用（使用 . 而不是 ->）
    auto &result = builder->withHeader("User-Agent", "QCurl Test")
                           .withHeader("Accept", "application/json")
                           .withTimeout(5000)
                           .withFollowLocation(true);

    // Assert - 返回的是引用，应该是同一个对象
    QCOMPARE(&result, builder);

    // Cleanup
    delete builder;
}

/**
 * @brief 测试 withHeader
 */
void TestQCNetworkRequestBuilder::testWithHeader()
{
    // Arrange
    auto *builder = manager->newRequest(QUrl("http://example.com"));

    // Act
    builder->withHeader("User-Agent", "TestAgent")
            .withHeader("Accept", "text/html");

    // Assert - 验证链式调用成功
    // Note: Builder 内部存储了 headers，但没有公共 getter
    // 这里只验证方法调用不崩溃

    // Cleanup
    delete builder;
}

/**
 * @brief 测试 withTimeout
 */
void TestQCNetworkRequestBuilder::testWithTimeout()
{
    // Arrange
    auto *builder = manager->newRequest(QUrl("http://example.com"));

    // Act
    builder->withTimeout(3000);  // 3 秒超时

    // Assert - 验证方法调用成功
    // Note: 实际超时值存储在私有成员中

    // Cleanup
    delete builder;
}

/**
 * @brief 测试 withQueryParams
 */
void TestQCNetworkRequestBuilder::testWithQueryParams()
{
    // Arrange
    auto *builder = manager->newRequest(QUrl("http://example.com/api"));

    // Act
    builder->withQueryParam("page", "1")
            .withQueryParam("limit", "20")
            .withQueryParam("sort", "desc");

    // Assert - 验证链式调用成功

    // Cleanup
    delete builder;
}

/**
 * @brief 测试 withFollowLocation
 */
void TestQCNetworkRequestBuilder::testWithFollowLocation()
{
    // Arrange
    auto *builder = manager->newRequest(QUrl("http://example.com"));

    // Act
    builder->withFollowLocation(true);
    builder->withFollowLocation(false);

    // Assert - 验证方法调用成功

    // Cleanup
    delete builder;
}

/**
 * @brief 测试 sendGet
 */
void TestQCNetworkRequestBuilder::testSendGet()
{
    // Arrange
    auto *builder = manager->newRequest(QUrl("http://example.com/test"));
    builder->withHeader("User-Agent", "QCurl Test")
            .withTimeout(5000);

    // Act
    auto *reply = builder->sendGet();

    // Assert
    QVERIFY(reply != nullptr);
    QCOMPARE(reply->url().toString(), QString("http://example.com/test"));

    // Cleanup
    reply->deleteLater();
    delete builder;
}

/**
 * @brief 测试 sendPost
 */
void TestQCNetworkRequestBuilder::testSendPost()
{
    // Arrange
    auto *builder = manager->newRequest(QUrl("http://example.com/submit"));
    QByteArray postData = "test=data";

    builder->withHeader("Content-Type", "application/x-www-form-urlencoded")
            .withTimeout(5000);

    // Act
    auto *reply = builder->sendPost(postData);

    // Assert
    QVERIFY(reply != nullptr);
    QCOMPARE(reply->url().toString(), QString("http://example.com/submit"));

    // Cleanup
    reply->deleteLater();
    delete builder;
}

QTEST_MAIN(TestQCNetworkRequestBuilder)
#include "tst_QCNetworkRequestBuilder.moc"
