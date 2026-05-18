// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkAccessManager.h"
#include "QCNetworkError.h"
#include "QCNetworkMockHandler.h"
#include "qcnetwork_mock_test_support.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QSignalSpy>
#include <QUrl>
#include <QtTest>

using namespace QCurl;

/**
 * @brief MockHandler 单元测试
 *
 * 测试 Mock 处理器的响应模拟、错误模拟和延迟设置。
 *
 */
class TestQCNetworkMockHandler : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Mock 功能测试
    void testMockResponse();
    void testMockError();
    void testMockHandlerIntegration();
    void testSequenceConsumption();
    void testGlobalDelayApplied();
    void testRequestCapture();
    void testCapturedRequestAccessorsAndTimeoutClearing();
    void testCapturedRequestSharedDataDetachesOnWrite();

private:
    QCNetworkAccessManager *m_manager = nullptr;
    QCNetworkMockHandler m_mockHandler;
};

void TestQCNetworkMockHandler::initTestCase()
{}

void TestQCNetworkMockHandler::cleanupTestCase()
{}

void TestQCNetworkMockHandler::init()
{
    m_manager = new QCNetworkAccessManager(this);
    m_mockHandler.clear();
}

void TestQCNetworkMockHandler::cleanup()
{
    if (m_manager) {
        QCurl::TestSupport::setMockHandler(*m_manager, nullptr);
        m_manager->deleteLater();
        m_manager = nullptr;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

/**
 * @brief 测试模拟成功响应
 */
void TestQCNetworkMockHandler::testMockResponse()
{
    // Arrange
    QUrl url("http://example.com/api/test");
    QByteArray mockData = "{\"status\":\"success\"}";
    int statusCode      = 200;

    // Act
    m_mockHandler.mockResponse(HttpMethod::Get, url, mockData, statusCode);

    // Assert
    QVERIFY(m_mockHandler.hasMock(HttpMethod::Get, url));

    int retrievedStatus      = 0;
    QByteArray retrievedData = m_mockHandler.getMockResponse(HttpMethod::Get, url, retrievedStatus);

    QCOMPARE(retrievedData, mockData);
    QCOMPARE(retrievedStatus, statusCode);

    // Test clearing
    m_mockHandler.clear();
    QVERIFY(!m_mockHandler.hasMock(HttpMethod::Get, url));
}

/**
 * @brief 测试模拟错误响应
 */
void TestQCNetworkMockHandler::testMockError()
{
    // Arrange
    QUrl url("http://example.com/api/error");
    NetworkError error = NetworkError::ConnectionRefused;

    // Act
    m_mockHandler.mockError(HttpMethod::Get, url, error);

    // Assert
    QVERIFY(m_mockHandler.hasMock(HttpMethod::Get, url));

    NetworkError retrievedError = m_mockHandler.getMockError(HttpMethod::Get, url);
    QCOMPARE(retrievedError, error);

    // Test with different error
    m_mockHandler.clear();
    m_mockHandler.mockError(HttpMethod::Get, url, error);
    QVERIFY(m_mockHandler.hasMock(HttpMethod::Get, url));
}

/**
 * @brief 测试 MockHandler 与 Manager 集成
 */
void TestQCNetworkMockHandler::testMockHandlerIntegration()
{
    // Arrange
    QCNetworkMockHandler handler;
    const QUrl mockUrl("http://example.com/mock/test");
    const QByteArray mockResponse = "Mock Response Data";

    handler.mockResponse(HttpMethod::Get, mockUrl, mockResponse, 200);
    QCurl::TestSupport::setMockHandler(*m_manager, &handler);

    // Act
    QCNetworkRequest request(mockUrl);
    auto *reply = m_manager->sendGet(request);
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);

    // Assert
    QVERIFY(finishedSpy.wait(1000));
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->readAll().value_or(QByteArray()), mockResponse);

    reply->deleteLater();

    // method+url 是唯一匹配合同，不同方法互不回退。
    handler.clear();
    handler.mockResponse(HttpMethod::Get, mockUrl, QByteArray("any"), 200);
    handler.mockResponse(HttpMethod::Post, mockUrl, QByteArray("post"), 200);

    QCNetworkRequest postReq(mockUrl);
    auto *postReply = m_manager->sendPost(postReq, QByteArray("x"));
    QSignalSpy postFinishedSpy(postReply, &QCNetworkReply::finished);
    QVERIFY(postFinishedSpy.wait(1000));
    QCOMPARE(postReply->error(), NetworkError::NoError);
    QCOMPARE(postReply->readAll().value_or(QByteArray()), QByteArray("post"));
    postReply->deleteLater();
}

void TestQCNetworkMockHandler::testSequenceConsumption()
{
    QCNetworkMockHandler handler;
    const QUrl url("http://example.com/mock/seq");
    handler.enqueueResponse(HttpMethod::Get, url, QByteArray("v1"), 200);
    handler.enqueueResponse(HttpMethod::Get, url, QByteArray("v2"), 200);

    QCurl::TestSupport::setMockHandler(*m_manager, &handler);

    QCNetworkRequest request(url);

    auto *r1 = m_manager->sendGet(request);
    QVERIFY(QSignalSpy(r1, &QCNetworkReply::finished).wait(1000));
    QCOMPARE(r1->readAll().value_or(QByteArray()), QByteArray("v1"));
    r1->deleteLater();

    auto *r2 = m_manager->sendGet(request);
    QVERIFY(QSignalSpy(r2, &QCNetworkReply::finished).wait(1000));
    QCOMPARE(r2->readAll().value_or(QByteArray()), QByteArray("v2"));
    r2->deleteLater();

    // 队列耗尽后复用最后一条
    auto *r3 = m_manager->sendGet(request);
    QVERIFY(QSignalSpy(r3, &QCNetworkReply::finished).wait(1000));
    QCOMPARE(r3->readAll().value_or(QByteArray()), QByteArray("v2"));
    r3->deleteLater();
}

void TestQCNetworkMockHandler::testGlobalDelayApplied()
{
    QCNetworkMockHandler handler;
    const QUrl url("http://example.com/mock/delay");
    handler.setGlobalDelay(50);
    handler.mockResponse(HttpMethod::Get, url, QByteArray("ok"), 200);

    QCurl::TestSupport::setMockHandler(*m_manager, &handler);

    QCNetworkRequest request(url);
    QElapsedTimer timer;
    timer.start();

    auto *reply = m_manager->sendGet(request);
    QVERIFY(QSignalSpy(reply, &QCNetworkReply::finished).wait(2000));

    QVERIFY(timer.elapsed() >= 30);
    reply->deleteLater();
}

void TestQCNetworkMockHandler::testRequestCapture()
{
    QCNetworkMockHandler handler;
    handler.setCaptureEnabled(true);
    handler.setCaptureBodyPreviewLimit(3);

    const QUrl url("http://example.com/mock/capture");
    handler.mockResponse(HttpMethod::Post, url, QByteArray("ok"), 200);
    QCurl::TestSupport::setMockHandler(*m_manager, &handler);

    QCNetworkRequest request(url);
    request.setRawHeader("X-Test", "1234");
    auto *reply = m_manager->sendPost(request, QByteArray("abcdef"));
    QVERIFY(QSignalSpy(reply, &QCNetworkReply::finished).wait(1000));

    const auto captured = handler.takeCapturedRequests();
    QCOMPARE(captured.size(), 1);
    QCOMPARE(captured[0].url(), url);
    QCOMPARE(captured[0].method(), HttpMethod::Post);
    QCOMPARE(captured[0].bodySize(), 6);
    QCOMPARE(captured[0].bodyPreview(), QByteArray("abc"));

    bool hasXTest = false;
    for (const auto &h : captured[0].headers()) {
        if (h.first == "X-Test" && h.second == "1234") {
            hasXTest = true;
            break;
        }
    }
    QVERIFY(hasXTest);

    reply->deleteLater();
}

void TestQCNetworkMockHandler::testCapturedRequestAccessorsAndTimeoutClearing()
{
    QCNetworkCapturedRequest request;
    const QUrl url(QStringLiteral("http://example.com/mock/value"));
    QVERIFY(request.followLocation());
    QVERIFY(!request.connectTimeoutMs().has_value());
    QVERIFY(!request.totalTimeoutMs().has_value());

    request.setUrl(url);
    request.setMethod(HttpMethod::Put);
    request.addHeader(QByteArrayLiteral("X-One"), QByteArrayLiteral("1"));
    request.addHeader(QByteArrayLiteral("X-Two"), QByteArrayLiteral("2"));
    request.setBodyPreview(QByteArrayLiteral("abcdef"));
    request.setBodySize(12);
    request.setFollowLocation(false);
    request.setConnectTimeoutMs(250);
    request.setTotalTimeoutMs(1000);

    QCOMPARE(request.url(), url);
    QCOMPARE(request.method(), HttpMethod::Put);
    QCOMPARE(request.headers().size(), 2);
    QCOMPARE(request.bodyPreview(), QByteArrayLiteral("abcdef"));
    QCOMPARE(request.bodySize(), qsizetype(12));
    QVERIFY(!request.followLocation());
    QVERIFY(request.connectTimeoutMs().has_value());
    QCOMPARE(request.connectTimeoutMs().value(), qint64(250));
    QVERIFY(request.totalTimeoutMs().has_value());
    QCOMPARE(request.totalTimeoutMs().value(), qint64(1000));

    request.clearConnectTimeoutMs();
    request.clearTotalTimeoutMs();
    QVERIFY(!request.connectTimeoutMs().has_value());
    QVERIFY(!request.totalTimeoutMs().has_value());
}

void TestQCNetworkMockHandler::testCapturedRequestSharedDataDetachesOnWrite()
{
    QCNetworkCapturedRequest original;
    const QUrl url(QStringLiteral("http://example.com/mock/value"));
    original.setUrl(url);
    original.setMethod(HttpMethod::Put);
    original.addHeader(QByteArrayLiteral("X-One"), QByteArrayLiteral("1"));
    original.addHeader(QByteArrayLiteral("X-Two"), QByteArrayLiteral("2"));
    original.setBodyPreview(QByteArrayLiteral("abcdef"));
    original.setBodySize(12);
    original.setFollowLocation(false);
    original.setConnectTimeoutMs(250);
    original.setTotalTimeoutMs(1000);

    QCNetworkCapturedRequest copy = original;
    copy.setUrl(QUrl(QStringLiteral("http://example.com/mock/copy")));
    copy.addHeader(QByteArrayLiteral("X-Copy"), QByteArrayLiteral("yes"));
    copy.setBodyPreview(QByteArrayLiteral("z"));
    copy.setBodySize(1);
    copy.setFollowLocation(true);
    copy.clearConnectTimeoutMs();
    copy.setTotalTimeoutMs(2000);

    QCOMPARE(original.url(), url);
    QCOMPARE(original.headers().size(), 2);
    QCOMPARE(original.bodyPreview(), QByteArrayLiteral("abcdef"));
    QCOMPARE(original.bodySize(), qsizetype(12));
    QVERIFY(!original.followLocation());
    QCOMPARE(original.connectTimeoutMs().value(), qint64(250));
    QCOMPARE(original.totalTimeoutMs().value(), qint64(1000));

    QCOMPARE(copy.headers().size(), 3);
    QCOMPARE(copy.bodyPreview(), QByteArrayLiteral("z"));
    QCOMPARE(copy.bodySize(), qsizetype(1));
    QVERIFY(copy.followLocation());
    QVERIFY(!copy.connectTimeoutMs().has_value());
    QCOMPARE(copy.totalTimeoutMs().value(), qint64(2000));

    copy.clearTotalTimeoutMs();
    QVERIFY(!copy.totalTimeoutMs().has_value());
}

QTEST_MAIN(TestQCNetworkMockHandler)
#include "tst_QCNetworkMockHandler.moc"
