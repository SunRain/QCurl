// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkAccessManager.h"
#include "QCNetworkCancelToken.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"

#include <QCoreApplication>
#include <QEvent>
#include <QSignalSpy>
#include <QUrl>
#include <QtTest>

using namespace QCurl;

/**
 * @brief CancelToken 单元测试
 *
 * 测试取消令牌的附加、分离、取消和自动超时功能。
 *
 */
class TestQCNetworkCancelToken : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // 基础功能测试
    void testCreateToken();
    void testAttachReply();
    void testDetachReply();
    void testAttachMultiple();

    // 取消功能测试
    void testCancelSignal();
    void testCancelAttachedReplies();
    void testClearReplies();

    // 自动超时测试
    void testAutoTimeout();

private:
    QCNetworkAccessManager *m_manager = nullptr;
    QCNetworkCancelToken *m_token     = nullptr;
    QCNetworkMockHandler m_mock;
};

void TestQCNetworkCancelToken::initTestCase()
{
    qDebug() << "=== TestQCNetworkCancelToken Test Suite ===";
}

void TestQCNetworkCancelToken::cleanupTestCase()
{
    qDebug() << "=== TestQCNetworkCancelToken Completed ===";
}

void TestQCNetworkCancelToken::init()
{
    m_manager = new QCNetworkAccessManager(this);
    m_token   = new QCNetworkCancelToken(this);

    m_mock.clear();
    m_mock.clearCapturedRequests();
    m_mock.setCaptureEnabled(false);
    m_mock.setGlobalDelay(0);

    // 离线门禁：该套件不应触发真实网络。为 test case 中使用的 URL 配置 mock 回放。
    m_mock.mockResponse(QUrl("http://example.com"), QByteArray("OK"));
    m_mock.mockResponse(QUrl("http://example.com/1"), QByteArray("OK"));
    m_mock.mockResponse(QUrl("http://example.com/2"), QByteArray("OK"));
    m_mock.mockResponse(QUrl("http://example.com/3"), QByteArray("OK"));
    m_manager->setMockHandler(&m_mock);
}

void TestQCNetworkCancelToken::cleanup()
{
    if (m_token) {
        m_token->deleteLater();
        m_token = nullptr;
    }
    if (m_manager) {
        m_manager->deleteLater();
        m_manager = nullptr;
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

/**
 * @brief 测试创建 CancelToken
 */
void TestQCNetworkCancelToken::testCreateToken()
{
    // Arrange & Act
    auto *newToken = new QCNetworkCancelToken(this);

    // Assert
    QVERIFY(newToken != nullptr);
    QCOMPARE(newToken->attachedCount(), 0);
    QCOMPARE(newToken->isCancelled(), false);

    // Cleanup
    newToken->deleteLater();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

/**
 * @brief 测试附加 Reply
 */
void TestQCNetworkCancelToken::testAttachReply()
{
    // Arrange - 创建一个请求（使用 MockHandler 离线回放）
    QCNetworkRequest request(QUrl("http://example.com"));
    auto *reply = m_manager->sendGet(request);

    // Act
    m_token->attach(reply);

    // Assert
    QCOMPARE(m_token->attachedCount(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);

    // Cleanup
    reply->deleteLater();
}

/**
 * @brief 测试分离 Reply
 */
void TestQCNetworkCancelToken::testDetachReply()
{
    // Arrange
    QCNetworkRequest request(QUrl("http://example.com"));
    auto *reply = m_manager->sendGet(request);
    m_token->attach(reply);
    QCOMPARE(m_token->attachedCount(), 1);

    // Act
    m_token->detach(reply);

    // Assert
    QCOMPARE(m_token->attachedCount(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);

    // Cleanup
    reply->deleteLater();
}

/**
 * @brief 测试附加多个 Reply
 */
void TestQCNetworkCancelToken::testAttachMultiple()
{
    // Arrange
    QCNetworkRequest request1(QUrl("http://example.com/1"));
    QCNetworkRequest request2(QUrl("http://example.com/2"));
    QCNetworkRequest request3(QUrl("http://example.com/3"));

    auto *reply1 = m_manager->sendGet(request1);
    auto *reply2 = m_manager->sendGet(request2);
    auto *reply3 = m_manager->sendGet(request3);

    QList<QCNetworkReply *> replies = {reply1, reply2, reply3};

    // Act
    m_token->attachMultiple(replies);

    // Assert
    QCOMPARE(m_token->attachedCount(), 3);
    for (auto *reply : replies) {
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    }

    // Cleanup
    reply1->deleteLater();
    reply2->deleteLater();
    reply3->deleteLater();
}

/**
 * @brief 测试取消信号
 */
void TestQCNetworkCancelToken::testCancelSignal()
{
    // Arrange
    QSignalSpy spy(m_token, &QCNetworkCancelToken::cancelled);

    // Act
    m_token->cancel();

    // Assert
    QCOMPARE(spy.count(), 1);
    QCOMPARE(m_token->isCancelled(), true);
}

/**
 * @brief 测试取消附加的 Reply
 */
void TestQCNetworkCancelToken::testCancelAttachedReplies()
{
    // Arrange
    QCNetworkRequest request1(QUrl("http://example.com/1"));
    QCNetworkRequest request2(QUrl("http://example.com/2"));

    // 使用 mock 延迟制造“in-flight”窗口，确保 cancel 覆盖 Running 语义
    m_mock.setGlobalDelay(5000);

    auto *reply1 = m_manager->sendGet(request1);
    auto *reply2 = m_manager->sendGet(request2);

    QSignalSpy cancelSpy1(reply1, &QCNetworkReply::cancelled);
    QSignalSpy cancelSpy2(reply2, &QCNetworkReply::cancelled);

    m_token->attach(reply1);
    m_token->attach(reply2);

    QCOMPARE(m_token->attachedCount(), 2);

    // Act
    m_token->cancel();

    // Assert
    QCOMPARE(m_token->isCancelled(), true);
    QCOMPARE(m_token->attachedCount(), 0); // 取消后应该清空

    // 注意：cancel() 可能同步发射 cancelled 信号；QSignalSpy::wait() 会以“等待新信号”为准，
    // 可能导致已发射但 wait 超时的假阴性。这里用 count + QTRY* 保证确定性。
    QTRY_VERIFY_WITH_TIMEOUT(cancelSpy1.count() >= 1, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(cancelSpy2.count() >= 1, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(reply1->error(), NetworkError::OperationCancelled, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(reply2->error(), NetworkError::OperationCancelled, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(reply1->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(reply2->isFinished(), 2000);

    // Cleanup
    reply1->deleteLater();
    reply2->deleteLater();

    // 恢复为默认（避免影响其他用例）
    m_mock.setGlobalDelay(0);
}

/**
 * @brief 测试清空 Reply 列表
 */
void TestQCNetworkCancelToken::testClearReplies()
{
    // Arrange
    QCNetworkRequest request1(QUrl("http://example.com/1"));
    QCNetworkRequest request2(QUrl("http://example.com/2"));

    auto *reply1 = m_manager->sendGet(request1);
    auto *reply2 = m_manager->sendGet(request2);

    m_token->attach(reply1);
    m_token->attach(reply2);
    QCOMPARE(m_token->attachedCount(), 2);

    // Act
    m_token->clear();

    // Assert
    QCOMPARE(m_token->attachedCount(), 0);
    QCOMPARE(m_token->isCancelled(), false); // clear 不会标记为已取消

    // Cleanup
    reply1->deleteLater();
    reply2->deleteLater();
}

/**
 * @brief 测试自动超时功能
 */
void TestQCNetworkCancelToken::testAutoTimeout()
{
    // Arrange
    QSignalSpy spy(m_token, &QCNetworkCancelToken::cancelled);

    // Act - 设置 100ms 自动超时
    m_token->setAutoTimeout(100);

    // Assert - 应该已自动取消
    QVERIFY(spy.wait(1000));
    QCOMPARE(m_token->isCancelled(), true);

    // Test disabling auto-timeout
    auto *token2 = new QCNetworkCancelToken(this);
    QSignalSpy spy2(token2, &QCNetworkCancelToken::cancelled);
    token2->setAutoTimeout(100);
    token2->setAutoTimeout(0); // 禁用

    QVERIFY(!spy2.wait(200));
    QCOMPARE(token2->isCancelled(), false); // 应该未取消

    // Cleanup
    token2->deleteLater();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

QTEST_MAIN(TestQCNetworkCancelToken)
#include "tst_QCNetworkCancelToken.moc"
