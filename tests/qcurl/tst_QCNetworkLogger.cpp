// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkAccessManager.h"
#include "QCNetworkLogger.h"
#include "QCNetworkReply.h"
#include "QCNetworkReply_p.h"
#include "QCNetworkRequest.h"

#include <QCoreApplication>
#include <QEvent>
#include <QUrl>
#include <QtTest>

using namespace QCurl;

/**
 * @brief 验证 logger 接入点、debug trace 脱敏和自定义实现合同。
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
    void testDebugTraceFlag();
    void testDebugTraceRedaction();

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
{}

void TestQCNetworkLogger::cleanupTestCase()
{}

void TestQCNetworkLogger::init()
{
    m_manager = new QCNetworkAccessManager(this);
}

void TestQCNetworkLogger::cleanup()
{
    if (m_manager) {
        m_manager->setLogger(nullptr);
        m_manager->deleteLater();
        m_manager = nullptr;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

/**
 * @brief 验证默认 logger 可被 AccessManager 正确持有与读取。
 */
void TestQCNetworkLogger::testDefaultLogger()
{
    QCNetworkDefaultLogger logger;
    m_manager->setLogger(&logger);
    auto *retrievedLogger = m_manager->logger();
    QCOMPARE(retrievedLogger, &logger);
    m_manager->setLogger(nullptr);
}

/**
 * @brief 验证替换 logger 时，manager 总是暴露当前实例。
 */
void TestQCNetworkLogger::testSetAndGetLogger()
{
    QCNetworkDefaultLogger logger1;
    QCNetworkDefaultLogger logger2;

    m_manager->setLogger(&logger1);
    QCOMPARE(m_manager->logger(), &logger1);

    m_manager->setLogger(&logger2);
    QCOMPARE(m_manager->logger(), &logger2);
    m_manager->setLogger(nullptr);
}

/**
 * @brief 验证传入 nullptr 会显式关闭 logger。
 */
void TestQCNetworkLogger::testLoggerNullptr()
{
    QCNetworkDefaultLogger logger;
    m_manager->setLogger(&logger);

    m_manager->setLogger(nullptr);

    QCOMPARE(m_manager->logger(), nullptr);
}

/**
 * @brief 验证不同 AccessManager 的 logger 状态彼此独立。
 */
void TestQCNetworkLogger::testMultipleLoggers()
{
    auto *manager1 = new QCNetworkAccessManager(this);
    auto *manager2 = new QCNetworkAccessManager(this);

    QCNetworkDefaultLogger logger1;
    QCNetworkDefaultLogger logger2;

    manager1->setLogger(&logger1);
    manager2->setLogger(&logger2);

    QCOMPARE(manager1->logger(), &logger1);
    QCOMPARE(manager2->logger(), &logger2);
    QVERIFY(manager1->logger() != manager2->logger());

    manager1->setLogger(nullptr);
    manager2->setLogger(nullptr);
    manager1->deleteLater();
    manager2->deleteLater();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void TestQCNetworkLogger::testDebugTraceFlag()
{
    QVERIFY(!m_manager->debugTraceEnabled());

    m_manager->setDebugTraceEnabled(true);
    QVERIFY(m_manager->debugTraceEnabled());

    m_manager->setDebugTraceEnabled(false);
    QVERIFY(!m_manager->debugTraceEnabled());
}

void TestQCNetworkLogger::testDebugTraceRedaction()
{
    const QByteArray raw = QByteArray("GET /path?token=abc&foo=bar HTTP/1.1\r\n"
                                      "Authorization: Bearer secret_token\r\n"
                                      "Proxy-Authorization: Basic dXNlcjpwYXNz\r\n"
                                      "Cookie: sessionid=abc\r\n"
                                      "Set-Cookie: sid=def\r\n");
    const QString message = QCNetworkReplyPrivate::formatDebugTraceMessage(CURLINFO_HEADER_OUT, raw);

    QVERIFY(!message.isEmpty());
    QVERIFY(message.startsWith("HEADER_OUT: "));
    QVERIFY(!message.contains("secret_token"));
    QVERIFY(!message.contains("dXNlcjpwYXNz"));
    QVERIFY(!message.contains("sessionid=abc"));
    QVERIFY(!message.contains("token=abc"));
    QVERIFY(message.contains("Authorization: [REDACTED]"));
    QVERIFY(message.contains("Proxy-Authorization: [REDACTED]"));
    QVERIFY(message.contains("Cookie: [REDACTED]"));
    QVERIFY(message.contains("Set-Cookie: [REDACTED]"));
    QVERIFY(message.contains("token=[REDACTED]"));
    QVERIFY(message.contains("foo=bar"));
}

/**
 * @brief 验证请求路径至少具备 logger wiring，不要求真实网络日志。
 */
void TestQCNetworkLogger::testRequestLogging()
{
    QCNetworkDefaultLogger logger;
    m_manager->setLogger(&logger);

    QCNetworkRequest request(QUrl("http://example.com/test"));
    request.setRawHeader("User-Agent", "QCurl Test");

    QVERIFY(m_manager->logger() != nullptr);

    // 该用例只验证 logger 能被挂接；真实日志内容由发送路径和实现细节决定。
    Q_UNUSED(request);
    m_manager->setLogger(nullptr);
}

/**
 * @brief 验证响应路径具备 logger wiring，不在此伪造响应日志。
 */
void TestQCNetworkLogger::testResponseLogging()
{
    QCNetworkDefaultLogger logger;
    m_manager->setLogger(&logger);

    QVERIFY(m_manager->logger() != nullptr);

    m_manager->setLogger(nullptr);
}

/**
 * @brief 验证错误路径具备 logger wiring，不在此构造额外错误源。
 */
void TestQCNetworkLogger::testErrorLogging()
{
    QCNetworkDefaultLogger logger;
    m_manager->setLogger(&logger);

    QVERIFY(m_manager->logger() != nullptr);

    m_manager->setLogger(nullptr);
}

/**
 * @brief 验证自定义 logger 实现能接收消息并维护最小状态。
 */
void TestQCNetworkLogger::testCustomLogger()
{
    class TestLogger : public QCNetworkLogger
    {
    public:
        int m_logCount             = 0;
        NetworkLogLevel m_minLevel = NetworkLogLevel::Info;
        QString m_lastCategory;
        QString m_lastMessage;

        void log(NetworkLogLevel level, const QString &category, const QString &message) override
        {
            Q_UNUSED(level);
            m_logCount++;
            m_lastCategory = category;
            m_lastMessage  = message;
        }

        void setMinLogLevel(NetworkLogLevel level) override { m_minLevel = level; }

        NetworkLogLevel minLogLevel() const override { return m_minLevel; }
    };

    TestLogger customLogger;
    m_manager->setLogger(&customLogger);

    QCOMPARE(m_manager->logger(), &customLogger);
    QCOMPARE(customLogger.m_logCount, 0);

    customLogger.log(NetworkLogLevel::Info, "Test", "Message 1");
    QCOMPARE(customLogger.m_logCount, 1);
    QCOMPARE(customLogger.m_lastCategory, QStringLiteral("Test"));
    QCOMPARE(customLogger.m_lastMessage, QStringLiteral("Message 1"));

    customLogger.log(NetworkLogLevel::Error, "Error", "Message 2");
    QCOMPARE(customLogger.m_logCount, 2);
    QCOMPARE(customLogger.m_lastCategory, QStringLiteral("Error"));

    customLogger.setMinLogLevel(NetworkLogLevel::Warning);
    QCOMPARE(customLogger.minLogLevel(), NetworkLogLevel::Warning);
    m_manager->setLogger(nullptr);
}

QTEST_MAIN(TestQCNetworkLogger)
#include "tst_QCNetworkLogger.moc"
