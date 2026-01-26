// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkAccessManager.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRequestBuilder.h"

#include <QCoreApplication>
#include <QEvent>
#include <QUrl>
#include <QtTest>

#include <optional>
#include <utility>

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
    void testSendDeleteWithBody();

private:
    QCNetworkAccessManager *m_manager = nullptr;
    QCNetworkMockHandler m_mock;
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
    m_manager = new QCNetworkAccessManager(this);

    m_mock.clear();
    m_mock.clearCapturedRequests();
    m_mock.setCaptureEnabled(true);
    m_mock.setGlobalDelay(0);
    m_manager->setMockHandler(&m_mock);
}

void TestQCNetworkRequestBuilder::cleanup()
{
    if (m_manager) {
        m_manager->deleteLater();
        m_manager = nullptr;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

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

/**
 * @brief 测试创建 Builder
 */
void TestQCNetworkRequestBuilder::testCreateBuilder()
{
    const QUrl url("http://example.com/create-builder");
    m_mock.mockResponse(HttpMethod::Get, url, QByteArray("OK"));
    m_mock.clearCapturedRequests();

    auto builder = m_manager->newRequest(url);
    auto moved = std::move(builder);

    auto *reply = moved.sendGet();
    QVERIFY(reply != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    QCOMPARE(reply->error(), NetworkError::NoError);

    const auto captured = m_mock.takeCapturedRequests();
    QCOMPARE(captured.size(), 1);
    QCOMPARE(captured.first().url, url);
    QCOMPARE(captured.first().method, HttpMethod::Get);

    reply->deleteLater();
}

/**
 * @brief 测试链式调用
 */
void TestQCNetworkRequestBuilder::testChainedCalls()
{
    // Arrange
    QUrl url("http://example.com/api");
    auto builder = m_manager->newRequest(url);

    // Act - 链式调用（使用 . 而不是 ->）
    auto &result = builder.withHeader("User-Agent", "QCurl Test")
                       .withHeader("Accept", "application/json")
                       .withTimeout(5000)
                       .withFollowLocation(true);

    // Assert - 返回的是引用，应该是同一个对象
    QCOMPARE(&result, &builder);
}

/**
 * @brief 测试 withHeader
 */
void TestQCNetworkRequestBuilder::testWithHeader()
{
    // Arrange
    QUrl url("http://example.com/with-header");
    m_mock.mockResponse(HttpMethod::Get, url, QByteArray("OK"));
    m_mock.clearCapturedRequests();

    auto builder = m_manager->newRequest(url);

    // Act
    builder.withHeader("User-Agent", "TestAgent").withHeader("Accept", "text/html");

    auto *reply = builder.sendGet();
    QVERIFY(reply != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);

    // Assert - 通过 MockHandler 请求捕获做离线可观测断言
    const auto captured = m_mock.takeCapturedRequests();
    QCOMPARE(captured.size(), 1);
    QCOMPARE(captured.first().url, url);
    QCOMPARE(captured.first().method, HttpMethod::Get);
    QCOMPARE(findHeaderValue(captured.first().headers, QByteArrayLiteral("user-agent")).value(),
             QByteArray("TestAgent"));
    QCOMPARE(findHeaderValue(captured.first().headers, QByteArrayLiteral("accept")).value(),
             QByteArray("text/html"));

    reply->deleteLater();
}

/**
 * @brief 测试 withTimeout
 */
void TestQCNetworkRequestBuilder::testWithTimeout()
{
    // Arrange
    QUrl url("http://example.com/with-timeout");
    m_mock.mockResponse(HttpMethod::Get, url, QByteArray("OK"));
    m_mock.clearCapturedRequests();

    auto builder = m_manager->newRequest(url);

    // Act
    builder.withTimeout(3000); // 3 秒超时

    auto *reply = builder.sendGet();
    QVERIFY(reply != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);

    const auto captured = m_mock.takeCapturedRequests();
    QCOMPARE(captured.size(), 1);
    QCOMPARE(captured.first().url, url);
    QCOMPARE(captured.first().method, HttpMethod::Get);

    reply->deleteLater();
}

/**
 * @brief 测试 withQueryParams
 */
void TestQCNetworkRequestBuilder::testWithQueryParams()
{
    // Arrange
    QUrl baseUrl("http://example.com/api");
    auto builder = m_manager->newRequest(baseUrl);

    // Act
    builder.withQueryParam("page", "1").withQueryParam("limit", "20").withQueryParam("sort", "desc");

    const QUrl expectedUrl("http://example.com/api?page=1&limit=20&sort=desc");
    m_mock.mockResponse(HttpMethod::Get, expectedUrl, QByteArray("OK"));
    m_mock.clearCapturedRequests();

    auto *reply = builder.sendGet();
    QVERIFY(reply != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);

    const auto captured = m_mock.takeCapturedRequests();
    QCOMPARE(captured.size(), 1);
    QCOMPARE(captured.first().url, expectedUrl);
    QCOMPARE(captured.first().method, HttpMethod::Get);

    reply->deleteLater();
}

/**
 * @brief 测试 withFollowLocation
 */
void TestQCNetworkRequestBuilder::testWithFollowLocation()
{
    // Arrange
    const QUrl url1("http://example.com/follow-default");
    const QUrl url2("http://example.com/follow-on");
    const QUrl url3("http://example.com/follow-off");
    m_mock.mockResponse(HttpMethod::Get, url1, QByteArray("OK"));
    m_mock.mockResponse(HttpMethod::Get, url2, QByteArray("OK"));
    m_mock.mockResponse(HttpMethod::Get, url3, QByteArray("OK"));

    // Case 1: default（未调用 withFollowLocation）
    {
        m_mock.clearCapturedRequests();
        auto builder = m_manager->newRequest(url1);
        auto *reply = builder.sendGet();
        QVERIFY(reply != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);

        const auto captured = m_mock.takeCapturedRequests();
        QCOMPARE(captured.size(), 1);
        QCOMPARE(captured.first().url, url1);
        QVERIFY(captured.first().followLocation);

        reply->deleteLater();
    }

    // Case 2: followLocation=true
    {
        m_mock.clearCapturedRequests();
        auto builder = m_manager->newRequest(url2);
        builder.withFollowLocation(true);

        auto *reply = builder.sendGet();
        QVERIFY(reply != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);

        const auto captured = m_mock.takeCapturedRequests();
        QCOMPARE(captured.size(), 1);
        QCOMPARE(captured.first().url, url2);
        QVERIFY(captured.first().followLocation);

        reply->deleteLater();
    }

    // Case 3: followLocation=false（显式覆盖）
    {
        m_mock.clearCapturedRequests();
        auto builder = m_manager->newRequest(url3);
        builder.withFollowLocation(false);

        auto *reply = builder.sendGet();
        QVERIFY(reply != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);

        const auto captured = m_mock.takeCapturedRequests();
        QCOMPARE(captured.size(), 1);
        QCOMPARE(captured.first().url, url3);
        QVERIFY(!captured.first().followLocation);

        reply->deleteLater();
    }
}

/**
 * @brief 测试 sendGet
 */
void TestQCNetworkRequestBuilder::testSendGet()
{
    // Arrange
    QUrl url("http://example.com/test");
    m_mock.mockResponse(HttpMethod::Get, url, QByteArray("OK"));
    m_mock.clearCapturedRequests();

    auto builder = m_manager->newRequest(url);
    builder.withHeader("User-Agent", "QCurl Test").withTimeout(5000);

    // Act
    auto *reply = builder.sendGet();

    // Assert
    QVERIFY(reply != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);

    const auto captured = m_mock.takeCapturedRequests();
    QCOMPARE(captured.size(), 1);
    QCOMPARE(captured.first().url, url);
    QCOMPARE(captured.first().method, HttpMethod::Get);
    QCOMPARE(findHeaderValue(captured.first().headers, QByteArrayLiteral("user-agent")).value(),
             QByteArray("QCurl Test"));

    // Cleanup
    reply->deleteLater();
}

/**
 * @brief 测试 sendPost
 */
void TestQCNetworkRequestBuilder::testSendPost()
{
    // Arrange
    QUrl url("http://example.com/submit");
    m_mock.mockResponse(HttpMethod::Post, url, QByteArray("OK"));
    m_mock.clearCapturedRequests();

    auto builder        = m_manager->newRequest(url);
    QByteArray postData = "test=data";

    builder.withHeader("Content-Type", "application/x-www-form-urlencoded").withTimeout(5000);

    // Act
    auto *reply = builder.sendPost(postData);

    // Assert
    QVERIFY(reply != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);

    const auto captured = m_mock.takeCapturedRequests();
    QCOMPARE(captured.size(), 1);
    QCOMPARE(captured.first().url, url);
    QCOMPARE(captured.first().method, HttpMethod::Post);
    QCOMPARE(captured.first().bodySize, postData.size());
    QCOMPARE(captured.first().bodyPreview, postData);
    QCOMPARE(findHeaderValue(captured.first().headers, QByteArrayLiteral("content-type")).value(),
             QByteArray("application/x-www-form-urlencoded"));

    // Cleanup
    reply->deleteLater();
}

/**
 * @brief 测试 sendDelete（携带 body）
 */
void TestQCNetworkRequestBuilder::testSendDeleteWithBody()
{
    // Arrange
    QUrl url1("http://example.com/delete");
    QUrl url2("http://example.com/delete2");
    m_mock.mockResponse(HttpMethod::Delete, url1, QByteArray("OK"));
    m_mock.mockResponse(HttpMethod::Delete, url2, QByteArray("OK"));
    m_mock.clearCapturedRequests();

    auto builder    = m_manager->newRequest(url1);
    QByteArray body = "test=data";
    builder.withBody(body);

    // Act（使用 withBody 中的 body）
    auto *reply = builder.sendDelete();

    // Assert
    QVERIFY(reply != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    reply->deleteLater();

    // Act（显式传入 body）
    auto builder2 = m_manager->newRequest(url2);
    auto *reply2  = builder2.sendDelete(body);
    QVERIFY(reply2 != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(reply2->isFinished(), 2000);
    reply2->deleteLater();

    const auto captured = m_mock.takeCapturedRequests();
    QCOMPARE(captured.size(), 2);
    QCOMPARE(captured.at(0).method, HttpMethod::Delete);
    QCOMPARE(captured.at(0).url, url1);
    QCOMPARE(captured.at(0).bodySize, body.size());
    QCOMPARE(captured.at(0).bodyPreview, body);
    QCOMPARE(captured.at(1).method, HttpMethod::Delete);
    QCOMPARE(captured.at(1).url, url2);
    QCOMPARE(captured.at(1).bodySize, body.size());
    QCOMPARE(captured.at(1).bodyPreview, body);
}

QTEST_MAIN(TestQCNetworkRequestBuilder)
#include "tst_QCNetworkRequestBuilder.moc"
