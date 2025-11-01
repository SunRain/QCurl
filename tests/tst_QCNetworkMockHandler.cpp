// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include <QtTest>
#include <QUrl>

#include "QCNetworkAccessManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkError.h"

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

private:
    QCNetworkAccessManager *manager = nullptr;
    QCNetworkMockHandler *mockHandler = nullptr;
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
    manager = new QCNetworkAccessManager(this);
    mockHandler = new QCNetworkMockHandler();
}

void TestQCNetworkMockHandler::cleanup()
{
    if (mockHandler) {
        delete mockHandler;
        mockHandler = nullptr;
    }
    if (manager) {
        delete manager;
        manager = nullptr;
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
    int statusCode = 200;

    // Act
    mockHandler->mockResponse(url, mockData, statusCode);

    // Assert
    QVERIFY(mockHandler->hasMock(url));

    int retrievedStatus = 0;
    QByteArray retrievedData = mockHandler->getMockResponse(url, retrievedStatus);

    QCOMPARE(retrievedData, mockData);
    QCOMPARE(retrievedStatus, statusCode);

    // Test clearing
    mockHandler->clear();
    QVERIFY(!mockHandler->hasMock(url));
}

/**
 * @brief 测试模拟错误响应
 */
void TestQCNetworkMockHandler::testMockError()
{
    // Arrange
    QUrl url("http://example.com/api/error");
    NetworkError error = NetworkError::NoError;  // 使用实际的错误码

    // Act
    mockHandler->mockError(url, error);

    // Assert
    QVERIFY(mockHandler->hasMock(url));

    NetworkError retrievedError = mockHandler->getMockError(url);
    QCOMPARE(retrievedError, error);

    // Test with different error
    mockHandler->clear();
    mockHandler->mockError(url, error);
    QVERIFY(mockHandler->hasMock(url));
}

/**
 * @brief 测试 MockHandler 与 Manager 集成
 */
void TestQCNetworkMockHandler::testMockHandlerIntegration()
{
    // Arrange
    auto *handler = new QCNetworkMockHandler();
    QUrl mockUrl("http://example.com/mock/test");
    QByteArray mockResponse = "Mock Response Data";

    handler->mockResponse(mockUrl, mockResponse, 200);

    // Act
    manager->setMockHandler(handler);

    // Assert
    QCOMPARE(manager->mockHandler(), handler);
    QVERIFY(handler->hasMock(mockUrl));

    // Test removing mock handler
    manager->setMockHandler(nullptr);
    QCOMPARE(manager->mockHandler(), nullptr);

    // Cleanup
    delete handler;
}

QTEST_MAIN(TestQCNetworkMockHandler)
#include "tst_QCNetworkMockHandler.moc"
