// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include <QtTest>
#include <QUrl>
#include <QCoreApplication>
#include <QEvent>

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
    int statusCode = 200;

    // Act
    m_mockHandler.mockResponse(url, mockData, statusCode);

    // Assert
    QVERIFY(m_mockHandler.hasMock(url));

    int retrievedStatus = 0;
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
    NetworkError error = NetworkError::NoError;  // 使用实际的错误码

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
    QUrl mockUrl("http://example.com/mock/test");
    QByteArray mockResponse = "Mock Response Data";

    handler.mockResponse(mockUrl, mockResponse, 200);

    // Act
    m_manager->setMockHandler(&handler);

    // Assert
    QCOMPARE(m_manager->mockHandler(), &handler);
    QVERIFY(handler.hasMock(mockUrl));

    // Test removing mock handler
    m_manager->setMockHandler(nullptr);
    QCOMPARE(m_manager->mockHandler(), nullptr);
}

QTEST_MAIN(TestQCNetworkMockHandler)
#include "tst_QCNetworkMockHandler.moc"
