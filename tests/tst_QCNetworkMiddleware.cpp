// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include <QtTest>
#include <QSignalSpy>
#include <QUrl>

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
    QCNetworkAccessManager *manager = nullptr;
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
    manager = new QCNetworkAccessManager(this);
}

void TestQCNetworkMiddleware::cleanup()
{
    if (manager) {
        delete manager;
        manager = nullptr;
    }
}

/**
 * @brief 测试添加中间件
 */
void TestQCNetworkMiddleware::testAddMiddleware()
{
    // Arrange
    auto *middleware = new QCLoggingMiddleware();

    // Act
    manager->addMiddleware(middleware);

    // Assert
    QCOMPARE(manager->middlewares().size(), 1);
    QVERIFY(manager->middlewares().contains(middleware));

    // Cleanup
    delete middleware;
}

/**
 * @brief 测试移除中间件
 */
void TestQCNetworkMiddleware::testRemoveMiddleware()
{
    // Arrange
    auto *middleware1 = new QCLoggingMiddleware();
    auto *middleware2 = new QCErrorHandlingMiddleware();

    manager->addMiddleware(middleware1);
    manager->addMiddleware(middleware2);
    QCOMPARE(manager->middlewares().size(), 2);

    // Act
    manager->removeMiddleware(middleware1);

    // Assert
    QCOMPARE(manager->middlewares().size(), 1);
    QVERIFY(!manager->middlewares().contains(middleware1));
    QVERIFY(manager->middlewares().contains(middleware2));

    // Cleanup
    delete middleware1;
    delete middleware2;
}

/**
 * @brief 测试清空所有中间件
 */
void TestQCNetworkMiddleware::testClearMiddlewares()
{
    // Arrange
    auto *middleware1 = new QCLoggingMiddleware();
    auto *middleware2 = new QCErrorHandlingMiddleware();

    manager->addMiddleware(middleware1);
    manager->addMiddleware(middleware2);
    QCOMPARE(manager->middlewares().size(), 2);

    // Act
    manager->clearMiddlewares();

    // Assert
    QCOMPARE(manager->middlewares().size(), 0);
    QVERIFY(manager->middlewares().isEmpty());

    // Cleanup
    delete middleware1;
    delete middleware2;
}

/**
 * @brief 测试获取中间件列表
 */
void TestQCNetworkMiddleware::testGetMiddlewares()
{
    // Arrange
    auto *middleware1 = new QCLoggingMiddleware();
    auto *middleware2 = new QCErrorHandlingMiddleware();

    // Act
    manager->addMiddleware(middleware1);
    manager->addMiddleware(middleware2);

    auto middlewareList = manager->middlewares();

    // Assert
    QCOMPARE(middlewareList.size(), 2);
    QCOMPARE(middlewareList.at(0), middleware1);
    QCOMPARE(middlewareList.at(1), middleware2);

    // Cleanup
    delete middleware1;
    delete middleware2;
}

/**
 * @brief 测试多个中间件的添加
 */
void TestQCNetworkMiddleware::testMultipleMiddlewares()
{
    // Arrange
    auto *m1 = new QCLoggingMiddleware();
    auto *m2 = new QCErrorHandlingMiddleware();
    auto *m3 = new QCLoggingMiddleware();  // 允许同类型多个实例

    // Act
    manager->addMiddleware(m1);
    manager->addMiddleware(m2);
    manager->addMiddleware(m3);

    // Assert
    QCOMPARE(manager->middlewares().size(), 3);

    // Cleanup
    delete m1;
    delete m2;
    delete m3;
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
        QStringList &order;
        QString name;

        OrderMiddleware(const QString &n, QStringList &o) : name(n), order(o) {}

        void onRequestPreSend(QCNetworkRequest &request) override {
            Q_UNUSED(request);
            order.append(name + "_request");
        }

        void onResponseReceived(QCNetworkReply *reply) override {
            Q_UNUSED(reply);
            order.append(name + "_response");
        }
    };

    // Arrange
    QStringList executionOrder;
    auto *m1 = new OrderMiddleware("M1", executionOrder);
    auto *m2 = new OrderMiddleware("M2", executionOrder);
    auto *m3 = new OrderMiddleware("M3", executionOrder);

    // Act
    manager->addMiddleware(m1);
    manager->addMiddleware(m2);
    manager->addMiddleware(m3);

    // 手动触发（模拟请求/响应）
    QCNetworkRequest request(QUrl("http://example.com"));
    m1->onRequestPreSend(request);
    m2->onRequestPreSend(request);
    m3->onRequestPreSend(request);

    // Assert - 验证执行顺序
    QCOMPARE(executionOrder.size(), 3);
    QCOMPARE(executionOrder.at(0), QString("M1_request"));
    QCOMPARE(executionOrder.at(1), QString("M2_request"));
    QCOMPARE(executionOrder.at(2), QString("M3_request"));

    // Cleanup
    delete m1;
    delete m2;
    delete m3;
}

/**
 * @brief 测试重复添加同一中间件
 */
void TestQCNetworkMiddleware::testDuplicateMiddleware()
{
    // Arrange
    auto *middleware = new QCLoggingMiddleware();

    // Act - 多次添加同一实例
    manager->addMiddleware(middleware);
    manager->addMiddleware(middleware);  // 应该被忽略
    manager->addMiddleware(middleware);  // 应该被忽略

    // Assert - 只应该有一个
    QCOMPARE(manager->middlewares().size(), 1);
    QCOMPARE(manager->middlewares().first(), middleware);

    // Cleanup
    delete middleware;
}

/**
 * @brief 测试内置中间件
 */
void TestQCNetworkMiddleware::testBuiltinMiddlewares()
{
    // Test 1: QCLoggingMiddleware
    auto *loggingMiddleware = new QCLoggingMiddleware();
    QVERIFY(loggingMiddleware != nullptr);
    manager->addMiddleware(loggingMiddleware);
    QCOMPARE(manager->middlewares().size(), 1);

    // Test 2: QCErrorHandlingMiddleware
    auto *errorMiddleware = new QCErrorHandlingMiddleware();
    QVERIFY(errorMiddleware != nullptr);
    manager->addMiddleware(errorMiddleware);
    QCOMPARE(manager->middlewares().size(), 2);

    // 验证所有中间件都在列表中
    QVERIFY(manager->middlewares().contains(loggingMiddleware));
    QVERIFY(manager->middlewares().contains(errorMiddleware));

    // Cleanup
    delete loggingMiddleware;
    delete errorMiddleware;
}

QTEST_MAIN(TestQCNetworkMiddleware)
#include "tst_QCNetworkMiddleware.moc"
