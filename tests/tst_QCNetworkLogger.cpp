// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include <QtTest>
#include <QUrl>
#include <QCoreApplication>
#include <QEvent>

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
    QCNetworkAccessManager *m_manager = nullptr;
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
    m_manager = new QCNetworkAccessManager(this);
}

void TestQCNetworkLogger::cleanup()
{
    // 每个测试用例后清理
    if (m_manager) {
        m_manager->setLogger(nullptr);
        m_manager->deleteLater();
        m_manager = nullptr;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

/**
 * @brief 测试默认 Logger 的创建和使用
 */
void TestQCNetworkLogger::testDefaultLogger()
{
    // Arrange
    QCNetworkDefaultLogger logger;

    // Act
    m_manager->setLogger(&logger);
    auto *retrievedLogger = m_manager->logger();

    // Assert
    QCOMPARE(retrievedLogger, &logger);
    m_manager->setLogger(nullptr);
}

/**
 * @brief 测试 Logger 的设置和获取
 */
void TestQCNetworkLogger::testSetAndGetLogger()
{
    // Arrange
    QCNetworkDefaultLogger logger1;
    QCNetworkDefaultLogger logger2;

    // Act - 设置第一个 logger
    m_manager->setLogger(&logger1);
    QCOMPARE(m_manager->logger(), &logger1);

    // Act - 替换为第二个 logger
    m_manager->setLogger(&logger2);
    QCOMPARE(m_manager->logger(), &logger2);
    m_manager->setLogger(nullptr);
}

/**
 * @brief 测试设置 nullptr Logger
 */
void TestQCNetworkLogger::testLoggerNullptr()
{
    // Arrange
    QCNetworkDefaultLogger logger;
    m_manager->setLogger(&logger);

    // Act - 设置为 nullptr
    m_manager->setLogger(nullptr);

    // Assert
    QCOMPARE(m_manager->logger(), nullptr);
}

/**
 * @brief 测试多个 Logger 的独立性
 */
void TestQCNetworkLogger::testMultipleLoggers()
{
    // Arrange
    auto *manager1 = new QCNetworkAccessManager(this);
    auto *manager2 = new QCNetworkAccessManager(this);

    QCNetworkDefaultLogger logger1;
    QCNetworkDefaultLogger logger2;

    // Act
    manager1->setLogger(&logger1);
    manager2->setLogger(&logger2);

    // Assert - 验证 Logger 独立
    QCOMPARE(manager1->logger(), &logger1);
    QCOMPARE(manager2->logger(), &logger2);
    QVERIFY(manager1->logger() != manager2->logger());

    // Cleanup
    manager1->setLogger(nullptr);
    manager2->setLogger(nullptr);
    manager1->deleteLater();
    manager2->deleteLater();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

/**
 * @brief 测试请求日志记录
 */
void TestQCNetworkLogger::testRequestLogging()
{
    // Arrange
    QCNetworkDefaultLogger logger;
    m_manager->setLogger(&logger);

    // Act - 创建请求（不实际发送）
    QCNetworkRequest request(QUrl("http://example.com/test"));
    request.setRawHeader("User-Agent", "QCurl Test");

    // Assert - Logger 已设置
    QVERIFY(m_manager->logger() != nullptr);

    // Note: 实际的日志记录会在请求发送时触发
    // 这里只验证 Logger 可以被正确设置和获取
    m_manager->setLogger(nullptr);
}

/**
 * @brief 测试响应日志记录
 */
void TestQCNetworkLogger::testResponseLogging()
{
    // Arrange
    QCNetworkDefaultLogger logger;
    m_manager->setLogger(&logger);

    // Assert - Logger 已设置，可以记录响应
    QVERIFY(m_manager->logger() != nullptr);

    // Note: 实际的响应日志记录需要真实网络请求
    // 这里只验证基础设置功能
    m_manager->setLogger(nullptr);
}

/**
 * @brief 测试错误日志记录
 */
void TestQCNetworkLogger::testErrorLogging()
{
    // Arrange
    QCNetworkDefaultLogger logger;
    m_manager->setLogger(&logger);

    // Assert - Logger 可以记录错误
    QVERIFY(m_manager->logger() != nullptr);

    // Note: 错误日志记录需要触发实际错误
    // 这里验证基础设置功能
    m_manager->setLogger(nullptr);
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
        int m_logCount = 0;
        NetworkLogLevel m_minLevel = NetworkLogLevel::Info;
        QString m_lastCategory;
        QString m_lastMessage;

        // 实现纯虚函数
        void log(NetworkLogLevel level, const QString &category, const QString &message) override {
            Q_UNUSED(level);
            m_logCount++;
            m_lastCategory = category;
            m_lastMessage = message;
        }

        void setMinLogLevel(NetworkLogLevel level) override {
            m_minLevel = level;
        }

        NetworkLogLevel minLogLevel() const override {
            return m_minLevel;
        }
    };

    // Arrange
    TestLogger customLogger;

    // Act
    m_manager->setLogger(&customLogger);

    // Assert
    QCOMPARE(m_manager->logger(), &customLogger);
    QCOMPARE(customLogger.m_logCount, 0);

    // 手动调用 log 方法
    customLogger.log(NetworkLogLevel::Info, "Test", "Message 1");
    QCOMPARE(customLogger.m_logCount, 1);
    QCOMPARE(customLogger.m_lastCategory, QString("Test"));
    QCOMPARE(customLogger.m_lastMessage, QString("Message 1"));

    customLogger.log(NetworkLogLevel::Error, "Error", "Message 2");
    QCOMPARE(customLogger.m_logCount, 2);
    QCOMPARE(customLogger.m_lastCategory, QString("Error"));

    // Test log level
    customLogger.setMinLogLevel(NetworkLogLevel::Warning);
    QCOMPARE(customLogger.minLogLevel(), NetworkLogLevel::Warning);
    m_manager->setLogger(nullptr);
}

QTEST_MAIN(TestQCNetworkLogger)
#include "tst_QCNetworkLogger.moc"
