// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkAccessManager.h"
#include "QCNetworkCancelToken.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "qcnetwork_mock_test_support.h"

#include <QCoreApplication>
#include <QEvent>
#include <QMetaObject>
#include <QSignalSpy>
#include <QThread>
#include <QUrl>
#include <QtTest>

#include <future>

using namespace QCurl;

/**
 * @brief 验证 cancel token 的 attach/detach/cancel/timeout 合同。
 */
class TestQCNetworkCancelToken : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // 基础功能测试
    void testCreateToken();
    void testAttachReply();
    void testAttachResultContract();
    void testAttachRejectsWrongThreadAndForeignAffinity();
    void testCommandsRejectWrongThreadWithoutMutation();
    void testDestroyedReplyRemovesRegistration();
    void testDetachReply();
    void testAttachMultiple();

    // 取消功能测试
    void testCancelSignal();
    void testCancelAttachedReplies();
    void testDestructorTeardownIsSilent();
    void testClearReplies();

    // 自动超时测试
    void testAutoTimeout();

private:
    QCNetworkAccessManager *m_manager = nullptr;
    QCNetworkCancelToken *m_token     = nullptr;
    QCNetworkMockHandler m_mock;
};

void TestQCNetworkCancelToken::initTestCase() {}

void TestQCNetworkCancelToken::cleanupTestCase() {}

void TestQCNetworkCancelToken::init()
{
    m_manager = new QCNetworkAccessManager(this);
    m_token   = new QCNetworkCancelToken(this);

    m_mock.clear();
    m_mock.clearCapturedRequests();
    m_mock.setCaptureEnabled(false);
    m_mock.setGlobalDelay(0);

    // 离线门禁：该套件不应触发真实网络。为 test case 中使用的 URL 配置 mock 回放。
    m_mock.mockResponse(HttpMethod::Get, QUrl("http://example.com"), QByteArray("OK"));
    m_mock.mockResponse(HttpMethod::Get, QUrl("http://example.com/1"), QByteArray("OK"));
    m_mock.mockResponse(HttpMethod::Get, QUrl("http://example.com/2"), QByteArray("OK"));
    m_mock.mockResponse(HttpMethod::Get, QUrl("http://example.com/3"), QByteArray("OK"));
    QCurl::TestSupport::setMockHandler(*m_manager, &m_mock);
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
    auto *reply = m_manager->get(request);

    QCOMPARE(m_token->attach(reply), QCNetworkCancelToken::AttachResult::Attached);

    QCOMPARE(m_token->attachedCount(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    QCOMPARE(m_token->attachedCount(), 0);

    reply->deleteLater();
}

/**
 * @brief 验证所有同步命令在错误线程 fail-closed，且不读取或修改 reply registration。
 */
void TestQCNetworkCancelToken::testCommandsRejectWrongThreadWithoutMutation()
{
    m_mock.setGlobalDelay(5000);
    QCNetworkRequest request(QUrl("http://example.com"));
    auto *reply = m_manager->get(request);
    QCOMPARE(m_token->attach(reply), QCNetworkCancelToken::AttachResult::Attached);

    const auto results
        = std::async(std::launch::async, [this, reply]() {
              return QList<QCNetworkCancelToken::CommandResult>{m_token->detach(reply),
                                                                m_token->cancel(),
                                                                m_token->clear(),
                                                                m_token->setAutoTimeout(100)};
          }).get();
    for (QCNetworkCancelToken::CommandResult result : results) {
        QCOMPARE(result, QCNetworkCancelToken::CommandResult::WrongThread);
    }

    QCOMPARE(m_token->attachedCount(), 1);
    QCOMPARE(m_token->isCancelled(), false);
    QCOMPARE(m_token->clear(), QCNetworkCancelToken::CommandResult::Applied);

    reply->deleteLater();
    m_mock.setGlobalDelay(0);
}

/**
 * @brief 验证 attach() 对成功、重复、空指针和已取消状态返回稳定结构化结果。
 */
void TestQCNetworkCancelToken::testAttachResultContract()
{
    QCNetworkRequest request(QUrl("http://example.com"));
    auto *reply = m_manager->get(request);

    QCOMPARE(m_token->attach(reply), QCNetworkCancelToken::AttachResult::Attached);
    QCOMPARE(m_token->attach(reply), QCNetworkCancelToken::AttachResult::AlreadyAttached);
    QCOMPARE(m_token->attach(nullptr), QCNetworkCancelToken::AttachResult::NullReply);
    QCOMPARE(m_token->attachedCount(), 1);

    QCOMPARE(m_token->cancel(), QCNetworkCancelToken::CommandResult::Applied);
    QCOMPARE(m_token->attach(reply), QCNetworkCancelToken::AttachResult::TokenCancelled);
    QCOMPARE(m_token->attachedCount(), 0);

    reply->deleteLater();
}

/**
 * @brief 验证 attach() 只接受 token owner thread 中、且与 token 同 affinity 的 reply。
 */
void TestQCNetworkCancelToken::testAttachRejectsWrongThreadAndForeignAffinity()
{
    QCNetworkRequest request(QUrl("http://example.com"));
    auto *reply = m_manager->get(request);

    QThread tokenThread;
    auto *token = new QCNetworkCancelToken;
    token->moveToThread(&tokenThread);
    tokenThread.start();

    QCOMPARE(token->attach(reply), QCNetworkCancelToken::AttachResult::WrongThread);

    QCNetworkCancelToken::AttachResult affinityResult = QCNetworkCancelToken::AttachResult::Attached;
    int attachedCount = -1;
    QVERIFY(QMetaObject::invokeMethod(
        token,
        [&]() {
            affinityResult = token->attach(reply);
            attachedCount  = token->attachedCount();
        },
        Qt::BlockingQueuedConnection));
    QCOMPARE(affinityResult, QCNetworkCancelToken::AttachResult::ThreadAffinityMismatch);
    QCOMPARE(attachedCount, 0);

    QVERIFY(
        QMetaObject::invokeMethod(token, [token]() { delete token; }, Qt::BlockingQueuedConnection));
    tokenThread.quit();
    QVERIFY(tokenThread.wait(1000));

    reply->deleteLater();
}

/**
 * @brief 验证 reply 析构只按 registration id 清理，不保留或操作已析构对象地址。
 */
void TestQCNetworkCancelToken::testDestroyedReplyRemovesRegistration()
{
    m_mock.setGlobalDelay(5000);
    QCNetworkRequest request(QUrl("http://example.com"));
    auto *reply = m_manager->get(request);

    QCOMPARE(m_token->attach(reply), QCNetworkCancelToken::AttachResult::Attached);
    QCOMPARE(m_token->attachedCount(), 1);

    delete reply;
    QCOMPARE(m_token->attachedCount(), 0);
    QCOMPARE(m_token->isCancelled(), false);

    m_mock.setGlobalDelay(0);
}

/**
 * @brief 验证 detach() 只解除 token 关联，不强制改变 reply 生命周期。
 */
void TestQCNetworkCancelToken::testDetachReply()
{
    QCNetworkRequest request(QUrl("http://example.com"));
    auto *reply = m_manager->get(request);
    QCOMPARE(m_token->attach(reply), QCNetworkCancelToken::AttachResult::Attached);
    QCOMPARE(m_token->attachedCount(), 1);

    QCOMPARE(m_token->detach(reply), QCNetworkCancelToken::CommandResult::Applied);

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

    auto *reply1 = m_manager->get(request1);
    auto *reply2 = m_manager->get(request2);
    auto *reply3 = m_manager->get(request3);

    QList<QCNetworkReply *> replies = {reply1, reply2, reply3};

    const QList<QCNetworkCancelToken::AttachResult> results = m_token->attachMultiple(replies);
    QCOMPARE(results.size(), replies.size());
    for (QCNetworkCancelToken::AttachResult result : results) {
        QCOMPARE(result, QCNetworkCancelToken::AttachResult::Attached);
    }

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

    QCOMPARE(m_token->cancel(), QCNetworkCancelToken::CommandResult::Applied);

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

    auto *reply1 = m_manager->get(request1);
    auto *reply2 = m_manager->get(request2);

    QSignalSpy cancelSpy1(reply1, &QCNetworkReply::cancelled);
    QSignalSpy cancelSpy2(reply2, &QCNetworkReply::cancelled);

    QCOMPARE(m_token->attach(reply1), QCNetworkCancelToken::AttachResult::Attached);
    QCOMPARE(m_token->attach(reply2), QCNetworkCancelToken::AttachResult::Attached);

    QCOMPARE(m_token->attachedCount(), 2);

    QCOMPARE(m_token->cancel(), QCNetworkCancelToken::CommandResult::Applied);

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

/// @brief 验证析构同步取消 reply，但不发射 token 或 reply 的业务信号。
void TestQCNetworkCancelToken::testDestructorTeardownIsSilent()
{
    m_mock.setGlobalDelay(5000);

    QCNetworkRequest request(QUrl("http://example.com"));
    auto *reply = m_manager->get(request);
    QSignalSpy replyCancelledSpy(reply, &QCNetworkReply::cancelled);
    QSignalSpy replyFinishedSpy(reply, &QCNetworkReply::finished);

    QObject observer;
    int tokenCancelledCount = 0;
    {
        QObject tokenOwner;
        auto *token = new QCNetworkCancelToken(&tokenOwner);
        QObject::connect(token,
                         &QCNetworkCancelToken::cancelled,
                         &observer,
                         [&tokenCancelledCount]() { ++tokenCancelledCount; });
        QCOMPARE(token->attach(reply), QCNetworkCancelToken::AttachResult::Attached);
        QCOMPARE(token->setAutoTimeout(5000), QCNetworkCancelToken::CommandResult::Applied);
        QCOMPARE(token->attachedCount(), 1);
    }

    QCOMPARE(tokenCancelledCount, 0);
    QCOMPARE(replyCancelledSpy.count(), 0);
    QCOMPARE(replyFinishedSpy.count(), 0);
    QCOMPARE(reply->state(), ReplyState::Cancelled);
    QCOMPARE(reply->error(), NetworkError::OperationCancelled);

    reply->deleteLater();
    m_mock.setGlobalDelay(0);
}

/**
 * @brief 验证 clear() 只清空附着列表，不会把 token 标记为已取消。
 */
void TestQCNetworkCancelToken::testClearReplies()
{
    QCNetworkRequest request1(QUrl("http://example.com/1"));
    QCNetworkRequest request2(QUrl("http://example.com/2"));

    auto *reply1 = m_manager->get(request1);
    auto *reply2 = m_manager->get(request2);

    QCOMPARE(m_token->attach(reply1), QCNetworkCancelToken::AttachResult::Attached);
    QCOMPARE(m_token->attach(reply2), QCNetworkCancelToken::AttachResult::Attached);
    QCOMPARE(m_token->attachedCount(), 2);

    QCOMPARE(m_token->clear(), QCNetworkCancelToken::CommandResult::Applied);

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
    QCOMPARE(m_token->setAutoTimeout(100), QCNetworkCancelToken::CommandResult::Applied);

    // 应在超时窗口内自动取消。
    QVERIFY(spy.wait(1000));
    QCOMPARE(m_token->isCancelled(), true);

    // 验证禁用 auto-timeout 后不会再触发取消。
    auto *token2 = new QCNetworkCancelToken(this);
    QSignalSpy spy2(token2, &QCNetworkCancelToken::cancelled);
    QCOMPARE(token2->setAutoTimeout(100), QCNetworkCancelToken::CommandResult::Applied);
    QCOMPARE(token2->setAutoTimeout(0), QCNetworkCancelToken::CommandResult::Applied); // 禁用

    QVERIFY(!spy2.wait(200));
    QCOMPARE(token2->isCancelled(), false); // 应该未取消

    token2->deleteLater();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

QTEST_MAIN(TestQCNetworkCancelToken)
#include "tst_QCNetworkCancelToken.moc"
