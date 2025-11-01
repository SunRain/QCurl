// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include <QtTest>
#include <QSignalSpy>
#include <QUrl>
#include <QFile>
#include <QTemporaryFile>

#include "QCNetworkAccessManager.h"
#include "QCNetworkRequest.h"
#include "QCNetworkReply.h"
#include "QCNetworkLogger.h"

using namespace QCurl;

/**
 * @brief Logger 系统单元测试
 *
 * 测试 Logger 的设置、获取、日志记录和自定义实现。
 *
 */
class TestQCNetworkLogger : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // 基础功能测试
    void testDefaultLogger();
    void testSetAndGetLogger();
    void testLoggerNullptr();
    void testMultipleLoggers();

    // 日志记录测试
    void testRequestLogging();
    void testResponseLogging();
    void testErrorLogging();

    // 自定义 Logger 测试
    void testCustomLogger();

private:
    QCNetworkAccessManager *manager = nullptr;
};

void TestQCNetworkLogger::initTestCase()
{
    // 测试套件初始化
    qDebug() << "=== TestQCNetworkLogger Test Suite ===";
}

void TestQCNetworkLogger::cleanupTestCase()
{
    // 测试套件清理
    qDebug() << "=== TestQCNetworkLogger Completed ===";
}

void TestQCNetworkLogger::init()
{
    // 每个测试用例前初始化
    manager = new QCNetworkAccessManager(this);
}

void TestQCNetworkLogger::cleanup()
{
    // 每个测试用例后清理
    if (manager) {
        delete manager;
        manager = nullptr;
    }
}

/**
 * @brief 测试默认 Logger 的创建和使用
 */
void TestQCNetworkLogger::testDefaultLogger()
{
    // Arrange
    auto *logger = new QCNetworkDefaultLogger();
    QVERIFY(logger != nullptr);

    // Act
    manager->setLogger(logger);
    auto *retrievedLogger = manager->logger();

    // Assert
    QCOMPARE(retrievedLogger, logger);

    // Cleanup
    delete logger;
}

/**
 * @brief 测试 Logger 的设置和获取
 */
void TestQCNetworkLogger::testSetAndGetLogger()
{
    // Arrange
    auto *logger1 = new QCNetworkDefaultLogger();
    auto *logger2 = new QCNetworkDefaultLogger();

    // Act - 设置第一个 logger
    manager->setLogger(logger1);
    QCOMPARE(manager->logger(), logger1);

    // Act - 替换为第二个 logger
    manager->setLogger(logger2);
    QCOMPARE(manager->logger(), logger2);

    // Cleanup
    delete logger1;
    delete logger2;
}

/**
 * @brief 测试设置 nullptr Logger
 */
void TestQCNetworkLogger::testLoggerNullptr()
{
    // Arrange
    auto *logger = new QCNetworkDefaultLogger();
    manager->setLogger(logger);

    // Act - 设置为 nullptr
    manager->setLogger(nullptr);

    // Assert
    QCOMPARE(manager->logger(), nullptr);

    // Cleanup
    delete logger;
}

/**
 * @brief 测试多个 Logger 的独立性
 */
void TestQCNetworkLogger::testMultipleLoggers()
{
    // Arrange
    auto *manager1 = new QCNetworkAccessManager(this);
    auto *manager2 = new QCNetworkAccessManager(this);

    auto *logger1 = new QCNetworkDefaultLogger();
    auto *logger2 = new QCNetworkDefaultLogger();

    // Act
    manager1->setLogger(logger1);
    manager2->setLogger(logger2);

    // Assert - 验证 Logger 独立
    QCOMPARE(manager1->logger(), logger1);
    QCOMPARE(manager2->logger(), logger2);
    QVERIFY(manager1->logger() != manager2->logger());

    // Cleanup
    delete manager1;
    delete manager2;
    delete logger1;
    delete logger2;
}

/**
 * @brief 测试请求日志记录
 */
void TestQCNetworkLogger::testRequestLogging()
{
    // Arrange
    auto *logger = new QCNetworkDefaultLogger();
    manager->setLogger(logger);

    // Act - 创建请求（不实际发送）
    QCNetworkRequest request(QUrl("http://example.com/test"));
    request.setRawHeader("User-Agent", "QCurl Test");

    // Assert - Logger 已设置
    QVERIFY(manager->logger() != nullptr);

    // Note: 实际的日志记录会在请求发送时触发
    // 这里只验证 Logger 可以被正确设置和获取

    // Cleanup
    delete logger;
}

/**
 * @brief 测试响应日志记录
 */
void TestQCNetworkLogger::testResponseLogging()
{
    // Arrange
    auto *logger = new QCNetworkDefaultLogger();
    manager->setLogger(logger);

    // Assert - Logger 已设置，可以记录响应
    QVERIFY(manager->logger() != nullptr);

    // Note: 实际的响应日志记录需要真实网络请求
    // 这里只验证基础设置功能

    // Cleanup
    delete logger;
}

/**
 * @brief 测试错误日志记录
 */
void TestQCNetworkLogger::testErrorLogging()
{
    // Arrange
    auto *logger = new QCNetworkDefaultLogger();
    manager->setLogger(logger);

    // Assert - Logger 可以记录错误
    QVERIFY(manager->logger() != nullptr);

    // Note: 错误日志记录需要触发实际错误
    // 这里验证基础设置功能

    // Cleanup
    delete logger;
}

/**
 * @brief 测试自定义 Logger 实现
 */
void TestQCNetworkLogger::testCustomLogger()
{
    // 定义自定义 Logger
    class TestLogger : public QCNetworkLogger
    {
    public:
        int logCount = 0;
        NetworkLogLevel m_minLevel = NetworkLogLevel::Info;
        QString lastCategory;
        QString lastMessage;

        // 实现纯虚函数
        void log(NetworkLogLevel level, const QString &category, const QString &message) override {
            Q_UNUSED(level);
            logCount++;
            lastCategory = category;
            lastMessage = message;
        }

        void setMinLogLevel(NetworkLogLevel level) override {
            m_minLevel = level;
        }

        NetworkLogLevel minLogLevel() const override {
            return m_minLevel;
        }
    };

    // Arrange
    auto *customLogger = new TestLogger();

    // Act
    manager->setLogger(customLogger);

    // Assert
    QCOMPARE(manager->logger(), customLogger);
    QCOMPARE(customLogger->logCount, 0);

    // 手动调用 log 方法
    customLogger->log(NetworkLogLevel::Info, "Test", "Message 1");
    QCOMPARE(customLogger->logCount, 1);
    QCOMPARE(customLogger->lastCategory, QString("Test"));
    QCOMPARE(customLogger->lastMessage, QString("Message 1"));

    customLogger->log(NetworkLogLevel::Error, "Error", "Message 2");
    QCOMPARE(customLogger->logCount, 2);
    QCOMPARE(customLogger->lastCategory, QString("Error"));

    // Test log level
    customLogger->setMinLogLevel(NetworkLogLevel::Warning);
    QCOMPARE(customLogger->minLogLevel(), NetworkLogLevel::Warning);

    // Cleanup
    delete customLogger;
}

QTEST_MAIN(TestQCNetworkLogger)
#include "tst_QCNetworkLogger.moc"
