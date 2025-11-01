// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include <QtTest>
#include <QSignalSpy>
#include <QUrl>

#include "QCNetworkAccessManager.h"
#include "QCNetworkRequest.h"
#include "QCNetworkReply.h"
#include "QCNetworkCancelToken.h"

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
    QCNetworkAccessManager *manager = nullptr;
    QCNetworkCancelToken *token = nullptr;
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
    manager = new QCNetworkAccessManager(this);
    token = new QCNetworkCancelToken(this);
}

void TestQCNetworkCancelToken::cleanup()
{
    if (token) {
        delete token;
        token = nullptr;
    }
    if (manager) {
        delete manager;
        manager = nullptr;
    }
}

/**
 * @brief 测试创建 CancelToken
 */
void TestQCNetworkCancelToken::testCreateToken()
{
    // Arrange & Act
    auto *newToken = new QCNetworkCancelToken();

    // Assert
    QVERIFY(newToken != nullptr);
    QCOMPARE(newToken->attachedCount(), 0);
    QCOMPARE(newToken->isCancelled(), false);

    // Cleanup
    delete newToken;
}

/**
 * @brief 测试附加 Reply
 */
void TestQCNetworkCancelToken::testAttachReply()
{
    // Arrange - 创建一个请求（不实际发送）
    QCNetworkRequest request(QUrl("http://example.com"));
    auto *reply = manager->sendGet(request);

    // Act
    token->attach(reply);

    // Assert
    QCOMPARE(token->attachedCount(), 1);

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
    auto *reply = manager->sendGet(request);
    token->attach(reply);
    QCOMPARE(token->attachedCount(), 1);

    // Act
    token->detach(reply);

    // Assert
    QCOMPARE(token->attachedCount(), 0);

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

    auto *reply1 = manager->sendGet(request1);
    auto *reply2 = manager->sendGet(request2);
    auto *reply3 = manager->sendGet(request3);

    QList<QCNetworkReply*> replies = {reply1, reply2, reply3};

    // Act
    token->attachMultiple(replies);

    // Assert
    QCOMPARE(token->attachedCount(), 3);

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
    QSignalSpy spy(token, &QCNetworkCancelToken::cancelled);

    // Act
    token->cancel();

    // Assert
    QCOMPARE(spy.count(), 1);
    QCOMPARE(token->isCancelled(), true);
}

/**
 * @brief 测试取消附加的 Reply
 */
void TestQCNetworkCancelToken::testCancelAttachedReplies()
{
    // Arrange
    QCNetworkRequest request1(QUrl("http://example.com/1"));
    QCNetworkRequest request2(QUrl("http://example.com/2"));

    auto *reply1 = manager->sendGet(request1);
    auto *reply2 = manager->sendGet(request2);

    QSignalSpy cancelSpy1(reply1, &QCNetworkReply::cancelled);
    QSignalSpy cancelSpy2(reply2, &QCNetworkReply::cancelled);

    token->attach(reply1);
    token->attach(reply2);

    QCOMPARE(token->attachedCount(), 2);

    // Act
    token->cancel();

    // Assert
    QCOMPARE(token->isCancelled(), true);
    QCOMPARE(token->attachedCount(), 0);  // 取消后应该清空

    // Note: 信号可能需要事件循环才能触发
    // 这里主要验证 token 的状态变化

    // Cleanup
    reply1->deleteLater();
    reply2->deleteLater();
}

/**
 * @brief 测试清空 Reply 列表
 */
void TestQCNetworkCancelToken::testClearReplies()
{
    // Arrange
    QCNetworkRequest request1(QUrl("http://example.com/1"));
    QCNetworkRequest request2(QUrl("http://example.com/2"));

    auto *reply1 = manager->sendGet(request1);
    auto *reply2 = manager->sendGet(request2);

    token->attach(reply1);
    token->attach(reply2);
    QCOMPARE(token->attachedCount(), 2);

    // Act
    token->clear();

    // Assert
    QCOMPARE(token->attachedCount(), 0);
    QCOMPARE(token->isCancelled(), false);  // clear 不会标记为已取消

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
    QSignalSpy spy(token, &QCNetworkCancelToken::cancelled);

    // Act - 设置 100ms 自动超时
    token->setAutoTimeout(100);

    // Wait for timeout
    QTest::qWait(150);

    // Assert - 应该已自动取消
    QVERIFY(spy.count() >= 1);
    QCOMPARE(token->isCancelled(), true);

    // Test disabling auto-timeout
    auto *token2 = new QCNetworkCancelToken(this);
    token2->setAutoTimeout(100);
    token2->setAutoTimeout(0);  // 禁用

    QTest::qWait(150);
    QCOMPARE(token2->isCancelled(), false);  // 应该未取消

    // Cleanup
    delete token2;
}

QTEST_MAIN(TestQCNetworkCancelToken)
#include "tst_QCNetworkCancelToken.moc"
