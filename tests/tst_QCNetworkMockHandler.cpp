// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkAccessManager.h"
#include "QCNetworkError.h"
#include "QCNetworkMockHandler.h"
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

private:
    QCNetworkAccessManager *m_manager = nullptr;
    QCNetworkMockHandler m_mockHandler;
};

void TestQCNetworkMockHandler::initTestCase()
{
    qDebug() << "=== TestQCNetworkMockHandler Test Suite ===";
}

void TestQCNetworkMockHandler::cleanupTestCase()
{
    qDebug() << "=== TestQCNetworkMockHandler Completed ===";
}

void TestQCNetworkMockHandler::init()
{
    m_manager = new QCNetworkAccessManager(this);
    m_mockHandler.clear();
}

void TestQCNetworkMockHandler::cleanup()
{
    if (m_manager) {
        m_manager->setMockHandler(nullptr);
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
    m_mockHandler.mockResponse(url, mockData, statusCode);

    // Assert
    QVERIFY(m_mockHandler.hasMock(url));

    int retrievedStatus      = 0;
    QByteArray retrievedData = m_mockHandler.getMockResponse(url, retrievedStatus);

    QCOMPARE(retrievedData, mockData);
    QCOMPARE(retrievedStatus, statusCode);

    // Test clearing
    m_mockHandler.clear();
    QVERIFY(!m_mockHandler.hasMock(url));
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
    m_mockHandler.mockError(url, error);

    // Assert
    QVERIFY(m_mockHandler.hasMock(url));

    NetworkError retrievedError = m_mockHandler.getMockError(url);
    QCOMPARE(retrievedError, error);

    // Test with different error
    m_mockHandler.clear();
    m_mockHandler.mockError(url, error);
    QVERIFY(m_mockHandler.hasMock(url));
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

    handler.mockResponse(mockUrl, mockResponse, 200);
    m_manager->setMockHandler(&handler);

    // Act
    QCNetworkRequest request(mockUrl);
    auto *reply = m_manager->sendGet(request);
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);

    // Assert
    QVERIFY(finishedSpy.wait(1000));
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->readAll().value_or(QByteArray()), mockResponse);

    reply->deleteLater();

    // method+url 优先于 url-only（兼容旧 API）
    handler.clear();
    handler.mockResponse(mockUrl, QByteArray("any"), 200);
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

    m_manager->setMockHandler(&handler);

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
    handler.mockResponse(url, QByteArray("ok"), 200);

    m_manager->setMockHandler(&handler);

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
    handler.mockResponse(url, QByteArray("ok"), 200);
    m_manager->setMockHandler(&handler);

    QCNetworkRequest request(url);
    request.setRawHeader("X-Test", "1234");
    auto *reply = m_manager->sendPost(request, QByteArray("abcdef"));
    QVERIFY(QSignalSpy(reply, &QCNetworkReply::finished).wait(1000));

    const auto captured = handler.takeCapturedRequests();
    QCOMPARE(captured.size(), 1);
    QCOMPARE(captured[0].url, url);
    QCOMPARE(captured[0].method, HttpMethod::Post);
    QCOMPARE(captured[0].bodySize, 6);
    QCOMPARE(captured[0].bodyPreview, QByteArray("abc"));

    bool hasXTest = false;
    for (const auto &h : captured[0].headers) {
        if (h.first == "X-Test" && h.second == "1234") {
            hasXTest = true;
            break;
        }
    }
    QVERIFY(hasXTest);

    reply->deleteLater();
}

QTEST_MAIN(TestQCNetworkMockHandler)
#include "tst_QCNetworkMockHandler.moc"
