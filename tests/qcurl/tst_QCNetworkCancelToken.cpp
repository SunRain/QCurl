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
 * @brief 验证 cancel token 的 attach/detach/cancel/timeout 合同。
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
{}

void TestQCNetworkCancelToken::cleanupTestCase()
{}

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
 * @brief 验证新建 token 处于未取消且未附着任何 reply 的初始状态。
 */
void TestQCNetworkCancelToken::testCreateToken()
{
    auto *newToken = new QCNetworkCancelToken(this);

    QVERIFY(newToken != nullptr);
    QCOMPARE(newToken->attachedCount(), 0);
    QCOMPARE(newToken->isCancelled(), false);

    newToken->deleteLater();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

/**
 * @brief 验证 attach() 会把 reply 纳入 token 管理范围。
 */
void TestQCNetworkCancelToken::testAttachReply()
{
    // 使用 MockHandler 离线回放，避免该套件退化为真实网络依赖。
    QCNetworkRequest request(QUrl("http://example.com"));
    auto *reply = m_manager->sendGet(request);

    m_token->attach(reply);

    QCOMPARE(m_token->attachedCount(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);

    reply->deleteLater();
}

/**
 * @brief 验证 detach() 只解除 token 关联，不强制改变 reply 生命周期。
 */
void TestQCNetworkCancelToken::testDetachReply()
{
    QCNetworkRequest request(QUrl("http://example.com"));
    auto *reply = m_manager->sendGet(request);
    m_token->attach(reply);
    QCOMPARE(m_token->attachedCount(), 1);

    m_token->detach(reply);

    QCOMPARE(m_token->attachedCount(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);

    reply->deleteLater();
}

/**
 * @brief 验证 attachMultiple() 会一次性接管多个 reply。
 */
void TestQCNetworkCancelToken::testAttachMultiple()
{
    QCNetworkRequest request1(QUrl("http://example.com/1"));
    QCNetworkRequest request2(QUrl("http://example.com/2"));
    QCNetworkRequest request3(QUrl("http://example.com/3"));

    auto *reply1 = m_manager->sendGet(request1);
    auto *reply2 = m_manager->sendGet(request2);
    auto *reply3 = m_manager->sendGet(request3);

    QList<QCNetworkReply *> replies = {reply1, reply2, reply3};

    m_token->attachMultiple(replies);

    QCOMPARE(m_token->attachedCount(), 3);
    for (auto *reply : replies) {
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    }

    reply1->deleteLater();
    reply2->deleteLater();
    reply3->deleteLater();
}

/**
 * @brief 验证 cancel() 会立刻发出 cancelled 信号并更新 token 状态。
 */
void TestQCNetworkCancelToken::testCancelSignal()
{
    QSignalSpy spy(m_token, &QCNetworkCancelToken::cancelled);

    m_token->cancel();

    QCOMPARE(spy.count(), 1);
    QCOMPARE(m_token->isCancelled(), true);
}

/**
 * @brief 验证 cancel() 会把已附着 reply 推向 OperationCancelled 终态。
 */
void TestQCNetworkCancelToken::testCancelAttachedReplies()
{
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

    m_token->cancel();

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

    reply1->deleteLater();
    reply2->deleteLater();

    // 恢复为默认，避免影响其他用例的 reply 完成时序。
    m_mock.setGlobalDelay(0);
}

/**
 * @brief 验证 clear() 只清空附着列表，不会把 token 标记为已取消。
 */
void TestQCNetworkCancelToken::testClearReplies()
{
    QCNetworkRequest request1(QUrl("http://example.com/1"));
    QCNetworkRequest request2(QUrl("http://example.com/2"));

    auto *reply1 = m_manager->sendGet(request1);
    auto *reply2 = m_manager->sendGet(request2);

    m_token->attach(reply1);
    m_token->attach(reply2);
    QCOMPARE(m_token->attachedCount(), 2);

    m_token->clear();

    QCOMPARE(m_token->attachedCount(), 0);
    QCOMPARE(m_token->isCancelled(), false); // clear 不会标记为已取消

    reply1->deleteLater();
    reply2->deleteLater();
}

/**
 * @brief 验证 auto-timeout 会触发取消，且可被显式关闭。
 */
void TestQCNetworkCancelToken::testAutoTimeout()
{
    QSignalSpy spy(m_token, &QCNetworkCancelToken::cancelled);

    // 设置 100ms 自动超时。
    m_token->setAutoTimeout(100);

    // 应在超时窗口内自动取消。
    QVERIFY(spy.wait(1000));
    QCOMPARE(m_token->isCancelled(), true);

    // 验证禁用 auto-timeout 后不会再触发取消。
    auto *token2 = new QCNetworkCancelToken(this);
    QSignalSpy spy2(token2, &QCNetworkCancelToken::cancelled);
    token2->setAutoTimeout(100);
    token2->setAutoTimeout(0); // 禁用

    QVERIFY(!spy2.wait(200));
    QCOMPARE(token2->isCancelled(), false); // 应该未取消

    token2->deleteLater();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

QTEST_MAIN(TestQCNetworkCancelToken)
#include "tst_QCNetworkCancelToken.moc"
