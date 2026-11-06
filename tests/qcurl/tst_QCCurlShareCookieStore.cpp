#include "QCCurlHandleManager.h"
#include "QCNetworkAccessManager.h"
#include "private/QCCurlRequiredOptionAdapter_p.h"
#include "private/QCCurlShareCookieStoreTestAccess_p.h"
#include "private/QCurlRuntimeState_p.h"

#include <QScopeGuard>
#include <QtTest>

using namespace QCurl;
using namespace QCurl::Internal;
using Access = QCCurlShareCookieStoreTestAccess;

namespace {

QCNetworkAccessManager::ShareHandleConfig config(int flags)
{
    QCNetworkAccessManager::ShareHandleConfig result;
    result.setShareCookies(flags == 1 || flags == 3);
    result.setShareSslSession(flags == 2 || flags == 3);
    return result;
}

class OperationChange final
{
public:
    OperationChange(QCCurlShareCookieStore &store,
                    QCNetworkAccessManager &manager,
                    bool close,
                    CURL *lastTransfer)
        : m_store(store)
        , m_manager(manager)
        , m_close(close)
        , m_lastTransfer(lastTransfer)
    {}

    static void observe(void *userdata, const char *event)
    {
        auto &change = *static_cast<OperationChange *>(userdata);
        if (QByteArray(event) != "operation-enter") {
            return;
        }
        change.m_result = change.m_close
                              ? change.m_store.closeScope(&change.m_manager)
                              : change.m_store.synchronizeScope(&change.m_manager, config(2));
        if (change.m_lastTransfer) {
            change.m_result = change.m_store.unbindHttp(change.m_lastTransfer, "test-last-transfer");
        }
        change.m_snapshot = Access::snapshot(change.m_store, &change.m_manager);
    }

    [[nodiscard]] bool retained() const
    {
        return m_result.state == ShareCallState::Callable && m_snapshot.operations == 1
               && m_snapshot.handles == 1;
    }

private:
    QCCurlShareCookieStore &m_store;
    QCNetworkAccessManager &m_manager;
    bool m_close         = false;
    CURL *m_lastTransfer = nullptr;
    ShareCallResult m_result;
    ShareCookieSnapshot m_snapshot;
};

} // namespace

class TestQCCurlShareCookieStore final : public QObject
{
    Q_OBJECT

public:
    TestQCCurlShareCookieStore() = default;

private Q_SLOTS:
    void cleanup();
    void pendingCommitsOnlyAfterLastUser_data();
    void pendingCommitsOnlyAfterLastUser();
    void temporaryOperationFinishesBeforeShareChanges_data();
    void temporaryOperationFinishesBeforeShareChanges();
    void scopeIsLazyAndRetiredIdentityIsIsolated();

private:
    Q_DISABLE_COPY_MOVE(TestQCCurlShareCookieStore)
};

void TestQCCurlShareCookieStore::cleanup()
{
    qunsetenv("QCURL_TEST_FORCE_COOKIE_OPTION_ERROR");
    qunsetenv("QCURL_TEST_FORCE_SHARE_INIT_ERROR");
}

void TestQCCurlShareCookieStore::pendingCommitsOnlyAfterLastUser_data()
{
    QTest::addColumn<int>("flags");
    QTest::addColumn<bool>("initFails");
    QTest::newRow("latest") << 3 << false;
    QTest::newRow("withdrawn") << 1 << false;
    QTest::newRow("all-off") << 0 << false;
    QTest::newRow("safe-pending-init-failure") << 2 << true;
}

void TestQCCurlShareCookieStore::pendingCommitsOnlyAfterLastUser()
{
    QFETCH(int, flags);
    QFETCH(bool, initFails);
    auto lease = acquireRuntimeLease();
    QVERIFY(lease.isValid());
    QCNetworkAccessManager manager;
    QCCurlShareCookieStore store(QThread::currentThread());
    QCCurlHandleManager first, second;
    const auto finish = qScopeGuard([&]() {
        static_cast<void>(store.unbindHttp(first.handle(), "test-cleanup"));
        static_cast<void>(store.unbindHttp(second.handle(), "test-cleanup"));
        static_cast<void>(store.shutdownScopes());
    });
    QVERIFY(store.openScope(&manager));
    QCOMPARE(Access::snapshot(store).handles, 0);
    QCOMPARE(store.bindHttp(&manager, config(1), first.handle(), false).state,
             ShareCallState::Callable);
    QCOMPARE(store.synchronizeScope(&manager, config(2)).state, ShareCallState::Callable);
    QCOMPARE(store.synchronizeScope(&manager, config(flags)).state, ShareCallState::Callable);
    auto next = store.bindHttp(&manager, config(flags), second.handle(), false);
    QCOMPARE(next.state, ShareCallState::Callable);
    QCOMPARE(Access::hasBinding(store, second.handle()), flags != 0);
    QCOMPARE(next.warnings.isEmpty(), flags == 0 || flags == 1);
    QCOMPARE(Access::snapshot(store, &manager).applied.shareCookies(), true);
    if (initFails) {
        qputenv("QCURL_TEST_FORCE_SHARE_INIT_ERROR", "1");
    }
    QCOMPARE(store.unbindHttp(first.handle(), "test-first-exit").state, ShareCallState::Callable);
    const auto last = store.unbindHttp(second.handle(), "test-last-exit");
    QCOMPARE(last.state, ShareCallState::Callable);
    const auto final = Access::snapshot(store, &manager);
    QCOMPARE(final.users, 0);
    QVERIFY(!final.pending);
    QCOMPARE(final.applied.shareCookies(), !initFails && config(flags).shareCookies());
    QCOMPARE(final.applied.shareSslSession(), !initFails && config(flags).shareSslSession());
    if (initFails) {
        QVERIFY(!last.warnings.isEmpty());
        QCOMPARE(store.bindHttp(&manager, config(flags), first.handle(), false).state,
                 ShareCallState::Rejected);
    }
}

void TestQCCurlShareCookieStore::temporaryOperationFinishesBeforeShareChanges_data()
{
    QTest::addColumn<QByteArray>("failurePoint");
    QTest::addColumn<bool>("closeScope");
    QTest::addColumn<bool>("lastTransfer");
    QTest::newRow("last-operation") << QByteArray{} << false << false;
    QTest::newRow("last-transfer-during-operation") << QByteArray{} << false << true;
    QTest::newRow("retired-operation") << QByteArray{} << true << false;
    QTest::newRow("setup") << QByteArrayLiteral("setup:CURLOPT_COOKIEFILE") << false << false;
    QTest::newRow("snapshot") << QByteArrayLiteral("snapshot:CURLINFO_COOKIELIST") << false
                              << false;
    QTest::newRow("export") << QByteArrayLiteral("export:CURLINFO_COOKIELIST") << true << false;
    QTest::newRow("persist") << QByteArrayLiteral("persist:CURLOPT_COOKIELIST") << true << false;
}

void TestQCCurlShareCookieStore::temporaryOperationFinishesBeforeShareChanges()
{
    QFETCH(QByteArray, failurePoint);
    QFETCH(bool, closeScope);
    QFETCH(bool, lastTransfer);
    auto lease = acquireRuntimeLease();
    QVERIFY(lease.isValid());
    QCNetworkAccessManager manager;
    QCCurlShareCookieStore store(QThread::currentThread());
    QCCurlHandleManager transfer;
    QVERIFY(store.openScope(&manager));
    QCOMPARE(store.bindHttp(&manager, config(1), transfer.handle(), false).state,
             ShareCallState::Callable);
    if (!lastTransfer) {
        QCOMPARE(store.unbindHttp(transfer.handle(), "test-seed").state, ShareCallState::Callable);
    }
    OperationChange change(store, manager, closeScope, lastTransfer ? transfer.handle() : nullptr);
    Access::observe(store, OperationChange::observe, &change);
    clearShareCookieLifecycleForTest();
    qputenv("QCURL_TEST_FORCE_COOKIE_OPTION_ERROR", failurePoint);
    const auto result = failurePoint.startsWith("export")
                            ? store.exportCookies(&manager, config(1), {})
                            : store.importCookies(&manager,
                                                  config(1),
                                                  {QCCookie(QByteArrayLiteral("test"),
                                                            QByteArrayLiteral("value"))},
                                                  QUrl(QStringLiteral("https://example.com/")));
    Access::observe(store, nullptr, nullptr);
    QCOMPARE(result.lifecycle.state, ShareCallState::Callable);
    QCOMPARE(result.store.isSuccess(), failurePoint.isEmpty());
    QVERIFY(change.retained());
    const auto events = shareCookieLifecycleForTest();
    const auto unbind = events.lastIndexOf(QStringLiteral("share-unbind"));
    const auto easy   = events.indexOf(QStringLiteral("easy-cleanup"));
    const auto exit   = events.indexOf(QStringLiteral("operation-exit"));
    const auto share  = events.indexOf(QStringLiteral("share-cleanup"));
    QVERIFY2(unbind >= 0 && easy > unbind && exit > easy && share > exit,
             qPrintable(events.join(QLatin1Char(','))));
    const auto final = Access::snapshot(store, &manager);
    QCOMPARE(final.operations, 0);
    QCOMPARE(final.handles, closeScope ? 0 : 1);
    QCOMPARE(final.applied.shareSslSession(), !closeScope);
    QCOMPARE(store.shutdownScopes().state, ShareCallState::Callable);
}

void TestQCCurlShareCookieStore::scopeIsLazyAndRetiredIdentityIsIsolated()
{
    auto lease = acquireRuntimeLease();
    QVERIFY(lease.isValid());
    QCNetworkAccessManager manager;
    QCCurlShareCookieStore store(QThread::currentThread());
    QCCurlHandleManager old;
    QVERIFY(store.openScope(&manager));
    QVERIFY(!store.openScope(&manager));
    QCOMPARE(store.synchronizeScope(&manager, config(1)).state, ShareCallState::Callable);
    QCOMPARE(Access::snapshot(store).contexts, 0);
    QCOMPARE(store.bindHttp(&manager, config(1), old.handle(), false).state,
             ShareCallState::Callable);
    QCOMPARE(store.closeScope(&manager).state, ShareCallState::Callable);
    QVERIFY(store.openScope(&manager));
    const auto fresh = store.exportCookies(&manager, config(1), {});
    QVERIFY(fresh.store.isSuccess());
    QVERIFY(fresh.store.cookies.isEmpty());
    QCOMPARE(Access::snapshot(store).contexts, 2);
    QCOMPARE(store.unbindHttp(old.handle(), "test-retired").state, ShareCallState::Callable);
    QCOMPARE(Access::snapshot(store).contexts, 1);
    QCOMPARE(store.shutdownScopes().state, ShareCallState::Callable);
}

QTEST_GUILESS_MAIN(TestQCCurlShareCookieStore)
#include "tst_QCCurlShareCookieStore.moc"
