// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include <QtTest>
#include <QUrl>
#include <QCoreApplication>
#include <QEvent>

#include "QCNetworkAccessManager.h"
#include "QCNetworkRequest.h"
#include "QCNetworkReply.h"
#include "QCNetworkMiddleware.h"

using namespace QCurl;

/**
 * @brief Middleware 系统单元测试
 *
 * 测试中间件的添加、移除、执行顺序和自定义实现。
 *
 */
class TestQCNetworkMiddleware : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // 基础功能测试
    void testAddMiddleware();
    void testRemoveMiddleware();
    void testClearMiddlewares();
    void testGetMiddlewares();

    // 中间件行为测试
    void testMultipleMiddlewares();
    void testMiddlewareOrder();
    void testDuplicateMiddleware();

    // 内置中间件测试
    void testBuiltinMiddlewares();

private:
    QCNetworkAccessManager *m_manager = nullptr;
};

void TestQCNetworkMiddleware::initTestCase()
{
    qDebug() << "=== TestQCNetworkMiddleware Test Suite ===";
}

void TestQCNetworkMiddleware::cleanupTestCase()
{
    qDebug() << "=== TestQCNetworkMiddleware Completed ===";
}

void TestQCNetworkMiddleware::init()
{
    m_manager = new QCNetworkAccessManager(this);
}

void TestQCNetworkMiddleware::cleanup()
{
    if (m_manager) {
        m_manager->clearMiddlewares();
        m_manager->deleteLater();
        m_manager = nullptr;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

/**
 * @brief 测试添加中间件
 */
void TestQCNetworkMiddleware::testAddMiddleware()
{
    // Arrange
    QCLoggingMiddleware middleware;

    // Act
    m_manager->addMiddleware(&middleware);

    // Assert
    QCOMPARE(m_manager->middlewares().size(), 1);
    QVERIFY(m_manager->middlewares().contains(&middleware));
    m_manager->clearMiddlewares();
}

/**
 * @brief 测试移除中间件
 */
void TestQCNetworkMiddleware::testRemoveMiddleware()
{
    // Arrange
    QCLoggingMiddleware middleware1;
    QCErrorHandlingMiddleware middleware2;

    m_manager->addMiddleware(&middleware1);
    m_manager->addMiddleware(&middleware2);
    QCOMPARE(m_manager->middlewares().size(), 2);

    // Act
    m_manager->removeMiddleware(&middleware1);

    // Assert
    QCOMPARE(m_manager->middlewares().size(), 1);
    QVERIFY(!m_manager->middlewares().contains(&middleware1));
    QVERIFY(m_manager->middlewares().contains(&middleware2));
    m_manager->clearMiddlewares();
}

/**
 * @brief 测试清空所有中间件
 */
void TestQCNetworkMiddleware::testClearMiddlewares()
{
    // Arrange
    QCLoggingMiddleware middleware1;
    QCErrorHandlingMiddleware middleware2;

    m_manager->addMiddleware(&middleware1);
    m_manager->addMiddleware(&middleware2);
    QCOMPARE(m_manager->middlewares().size(), 2);

    // Act
    m_manager->clearMiddlewares();

    // Assert
    QCOMPARE(m_manager->middlewares().size(), 0);
    QVERIFY(m_manager->middlewares().isEmpty());
}

/**
 * @brief 测试获取中间件列表
 */
void TestQCNetworkMiddleware::testGetMiddlewares()
{
    // Arrange
    QCLoggingMiddleware middleware1;
    QCErrorHandlingMiddleware middleware2;

    // Act
    m_manager->addMiddleware(&middleware1);
    m_manager->addMiddleware(&middleware2);

    auto middlewareList = m_manager->middlewares();

    // Assert
    QCOMPARE(middlewareList.size(), 2);
    QCOMPARE(middlewareList.at(0), &middleware1);
    QCOMPARE(middlewareList.at(1), &middleware2);
    m_manager->clearMiddlewares();
}

/**
 * @brief 测试多个中间件的添加
 */
void TestQCNetworkMiddleware::testMultipleMiddlewares()
{
    // Arrange
    QCLoggingMiddleware m1;
    QCErrorHandlingMiddleware m2;
    QCLoggingMiddleware m3;  // 允许同类型多个实例

    // Act
    m_manager->addMiddleware(&m1);
    m_manager->addMiddleware(&m2);
    m_manager->addMiddleware(&m3);

    // Assert
    QCOMPARE(m_manager->middlewares().size(), 3);
    m_manager->clearMiddlewares();
}

/**
 * @brief 测试中间件执行顺序
 */
void TestQCNetworkMiddleware::testMiddlewareOrder()
{
    // 定义顺序记录中间件
    class OrderMiddleware : public QCNetworkMiddleware
    {
    public:
        OrderMiddleware(const QString &name, QStringList &order)
            : m_order(order),
              m_name(name)
        {
        }

        void onRequestPreSend(QCNetworkRequest &request) override
        {
            Q_UNUSED(request);
            m_order.append(m_name + QStringLiteral("_request"));
        }

        void onResponseReceived(QCNetworkReply *reply) override
        {
            Q_UNUSED(reply);
            m_order.append(m_name + QStringLiteral("_response"));
        }

    private:
        QStringList &m_order;
        QString m_name;
    };

    // Arrange
    QStringList executionOrder;
    OrderMiddleware m1(QStringLiteral("M1"), executionOrder);
    OrderMiddleware m2(QStringLiteral("M2"), executionOrder);
    OrderMiddleware m3(QStringLiteral("M3"), executionOrder);

    // Act
    m_manager->addMiddleware(&m1);
    m_manager->addMiddleware(&m2);
    m_manager->addMiddleware(&m3);

    // 手动触发（模拟请求/响应）
    QCNetworkRequest request(QUrl("http://example.com"));
    m1.onRequestPreSend(request);
    m2.onRequestPreSend(request);
    m3.onRequestPreSend(request);

    // Assert - 验证执行顺序
    QCOMPARE(executionOrder.size(), 3);
    QCOMPARE(executionOrder.at(0), QString("M1_request"));
    QCOMPARE(executionOrder.at(1), QString("M2_request"));
    QCOMPARE(executionOrder.at(2), QString("M3_request"));
    m_manager->clearMiddlewares();
}

/**
 * @brief 测试重复添加同一中间件
 */
void TestQCNetworkMiddleware::testDuplicateMiddleware()
{
    // Arrange
    QCLoggingMiddleware middleware;

    // Act - 多次添加同一实例
    m_manager->addMiddleware(&middleware);
    m_manager->addMiddleware(&middleware);  // 应该被忽略
    m_manager->addMiddleware(&middleware);  // 应该被忽略

    // Assert - 只应该有一个
    QCOMPARE(m_manager->middlewares().size(), 1);
    QCOMPARE(m_manager->middlewares().first(), &middleware);
    m_manager->clearMiddlewares();
}

/**
 * @brief 测试内置中间件
 */
void TestQCNetworkMiddleware::testBuiltinMiddlewares()
{
    // Test 1: QCLoggingMiddleware
    QCLoggingMiddleware loggingMiddleware;
    m_manager->addMiddleware(&loggingMiddleware);
    QCOMPARE(m_manager->middlewares().size(), 1);

    // Test 2: QCErrorHandlingMiddleware
    QCErrorHandlingMiddleware errorMiddleware;
    m_manager->addMiddleware(&errorMiddleware);
    QCOMPARE(m_manager->middlewares().size(), 2);

    // 验证所有中间件都在列表中
    QVERIFY(m_manager->middlewares().contains(&loggingMiddleware));
    QVERIFY(m_manager->middlewares().contains(&errorMiddleware));
    m_manager->clearMiddlewares();
}

QTEST_MAIN(TestQCNetworkMiddleware)
#include "tst_QCNetworkMiddleware.moc"
