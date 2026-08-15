/**
 * @file tst_QCNetworkCookieBridge.cpp
 * @brief Cookie bridge（share cookie store）离线门禁
 */

#include "QCNetworkAccessManager.h"

#include <QDateTime>
#include <QTimeZone>
#include <QtTest/QtTest>

using namespace QCurl;

namespace {

QStringList *g_capturedWarnings = nullptr;

void captureWarnings(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    if (type == QtWarningMsg && g_capturedWarnings) {
        g_capturedWarnings->append(message);
    }
}

class WarningCapture final
{
public:
    WarningCapture()
        : m_previous(qInstallMessageHandler(captureWarnings))
    {
        g_capturedWarnings = &messages;
    }

    ~WarningCapture()
    {
        g_capturedWarnings = nullptr;
        qInstallMessageHandler(m_previous);
    }

    QStringList messages;

private:
    QtMessageHandler m_previous;
};

bool hasCookie(const QList<QCCookie> &cookies, const QByteArray &name, const QByteArray &value)
{
    for (const QCCookie &c : cookies) {
        if (c.name() == name && c.value() == value) {
            return true;
        }
    }
    return false;
}

} // namespace

class TestQCNetworkCookieBridge : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void cleanup();
    void testImportExportRoundTrip();
    void testExportFilter_HostAndPathIsolation();
    void testClearAllCookies();
    void testImportRejectsInvalidCookieBeforeMutation();
    void testRequiredSetupOptionFailureIsStructuredAndRedacted();
    void testRequiredEasyShareOptionFailure();
    void testRequiredShareCookieOptionFailure();
    void testExportRequiredSetupOptionFailure();
    void testRequiredClearAllFailurePreservesStore();
    void testRequiredClearFlushFailureIsPersistenceFailure();
    void testPartialImportFailureRollsBackSnapshot();
    void testSnapshotFailureRejectsBeforeMutation();
    void testRollbackFailurePoisonsCookieStore();
    void testFlushFailureIsPersistenceFailure();
};

void TestQCNetworkCookieBridge::cleanup()
{
    qunsetenv("QCURL_TEST_FORCE_COOKIE_OPTION_ERROR");
    qunsetenv("QCURL_TEST_FORCE_SHARE_OPTION_ERROR");
}

void TestQCNetworkCookieBridge::testImportExportRoundTrip()
{
    QCNetworkAccessManager manager;
    QCNetworkAccessManager::ShareHandleConfig shareCfg;
    shareCfg.setShareCookies(true);
    manager.setShareHandleConfig(shareCfg);

    QCCookie sid("sid", "abc");
    sid.setDomain("example.com");
    sid.setPath("/foo");
    sid.setSecure(true);
    sid.setHttpOnly(true);
    sid.setExpirationDate(QDateTime::fromSecsSinceEpoch(1893456000, QTimeZone::utc()));

    QString err;
    QVERIFY(manager.importCookies({sid}, QUrl("https://example.com/"), &err));

    const auto exported = manager.exportCookies(QUrl("https://example.com/foo/bar"), &err);
    QVERIFY2(exported.has_value(), qPrintable(err));
    QVERIFY(!exported->isEmpty());

    for (const QCCookie &c : exported.value()) {
        if (c.name() == QByteArray("sid") && c.value() == QByteArray("abc")) {
            QVERIFY(c.isSecure());
            QVERIFY(c.isHttpOnly());
            QCOMPARE(c.expirationDate().toSecsSinceEpoch(), sid.expirationDate().toSecsSinceEpoch());
            break;
        }
    }
    QVERIFY(hasCookie(exported.value(), "sid", "abc"));
}

void TestQCNetworkCookieBridge::testExportFilter_HostAndPathIsolation()
{
    QCNetworkAccessManager manager;
    QCNetworkAccessManager::ShareHandleConfig shareCfg;
    shareCfg.setShareCookies(true);
    manager.setShareHandleConfig(shareCfg);

    QCCookie hostOnly("hostonly", "1");
    hostOnly.setDomain("example.com");
    hostOnly.setPath("/foo");

    QCCookie domainCookie("domain", "1");
    domainCookie.setDomain(".example.com");
    domainCookie.setPath("/foo");

    QString err;
    QVERIFY(manager.importCookies({hostOnly, domainCookie}, QUrl("https://example.com/"), &err));

    const auto ex1 = manager.exportCookies(QUrl("https://example.com/foo/bar"), &err);
    QVERIFY2(ex1.has_value(), qPrintable(err));
    QVERIFY(hasCookie(ex1.value(), "hostonly", "1"));
    QVERIFY(hasCookie(ex1.value(), "domain", "1"));

    const auto ex2 = manager.exportCookies(QUrl("https://sub.example.com/foo/bar"), &err);
    QVERIFY2(ex2.has_value(), qPrintable(err));
    QVERIFY(!hasCookie(ex2.value(), "hostonly", "1"));
    QVERIFY(hasCookie(ex2.value(), "domain", "1"));

    // 路径匹配要求边界正确：/foo 不应匹配 /foobar
    const auto ex3 = manager.exportCookies(QUrl("https://example.com/foobar"), &err);
    QVERIFY2(ex3.has_value(), qPrintable(err));
    QVERIFY(!hasCookie(ex3.value(), "hostonly", "1"));
    QVERIFY(!hasCookie(ex3.value(), "domain", "1"));
}

void TestQCNetworkCookieBridge::testClearAllCookies()
{
    QCNetworkAccessManager manager;
    QCNetworkAccessManager::ShareHandleConfig shareCfg;
    shareCfg.setShareCookies(true);
    manager.setShareHandleConfig(shareCfg);

    QCCookie sid("sid", "abc");
    sid.setDomain("example.com");
    sid.setPath("/");

    QString err;
    QVERIFY(manager.importCookies({sid}, QUrl("https://example.com/"), &err));
    auto exported = manager.exportCookies(QUrl("https://example.com/"), &err);
    QVERIFY2(exported.has_value(), qPrintable(err));
    QVERIFY(!exported->isEmpty());

    QVERIFY2(manager.clearAllCookies(&err), qPrintable(err));
    exported = manager.exportCookies(QUrl("https://example.com/"), &err);
    QVERIFY2(exported.has_value(), qPrintable(err));
    QVERIFY(exported->isEmpty());
}

void TestQCNetworkCookieBridge::testImportRejectsInvalidCookieBeforeMutation()
{
    QCNetworkAccessManager manager;
    QCNetworkAccessManager::ShareHandleConfig shareCfg;
    shareCfg.setShareCookies(true);
    manager.setShareHandleConfig(shareCfg);

    QCCookie baseline("baseline", "kept");
    baseline.setDomain("example.com");
    baseline.setPath("/");
    QString error;
    QVERIFY2(manager.importCookies({baseline}, QUrl("https://example.com/"), &error),
             qPrintable(error));

    QCCookie invalid("bad\tname", "secret-cookie-value");
    invalid.setDomain("example.com");
    invalid.setPath("/");
    const auto future = manager.importCookiesAsync({invalid}, QUrl("https://example.com/"));
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 1000);
    const auto result = future.result();
    QCOMPARE(result.policyCode(), QStringLiteral("cookie.invalid_input"));
    QVERIFY(!result.error().contains(QStringLiteral("secret-cookie-value")));

    const auto exported = manager.exportCookies(QUrl("https://example.com/"), &error);
    QVERIFY2(exported.has_value(), qPrintable(error));
    QVERIFY(hasCookie(*exported, "baseline", "kept"));
}

void TestQCNetworkCookieBridge::testRequiredSetupOptionFailureIsStructuredAndRedacted()
{
    QCNetworkAccessManager manager;
    QCNetworkAccessManager::ShareHandleConfig shareCfg;
    shareCfg.setShareCookies(true);
    manager.setShareHandleConfig(shareCfg);
    qputenv("QCURL_TEST_FORCE_COOKIE_OPTION_ERROR", "setup:CURLOPT_COOKIEFILE");

    QCCookie cookie("session", "top-secret");
    cookie.setDomain("example.com");
    cookie.setPath("/");
    WarningCapture warnings;
    const auto future = manager.importCookiesAsync({cookie},
                                                   QUrl("https://example.com/private/path"));
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 1000);
    const auto result = future.result();
    QCOMPARE(result.policyCode(), QStringLiteral("cookie.required_option_failed"));
    QVERIFY(result.error().contains(QStringLiteral("CURLOPT_COOKIEFILE")));
    QVERIFY(!result.error().contains(QStringLiteral("top-secret")));
    QVERIFY(!result.error().contains(QStringLiteral("/private/path")));
    const QString logged = warnings.messages.join(QLatin1Char('\n'));
    QVERIFY(logged.contains(QStringLiteral("cookie.required_option_failed")));
    QVERIFY(!logged.contains(QStringLiteral("top-secret")));
    QVERIFY(!logged.contains(QStringLiteral("/private/path")));
}

void TestQCNetworkCookieBridge::testRequiredEasyShareOptionFailure()
{
    QCNetworkAccessManager manager;
    QCNetworkAccessManager::ShareHandleConfig shareCfg;
    shareCfg.setShareCookies(true);
    manager.setShareHandleConfig(shareCfg);
    qputenv("QCURL_TEST_FORCE_COOKIE_OPTION_ERROR", "setup:CURLOPT_SHARE");

    QCCookie cookie("session", "secret");
    cookie.setDomain("example.com");
    cookie.setPath("/");
    const auto future = manager.importCookiesAsync({cookie}, QUrl("https://example.com/"));
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 1000);
    const auto result = future.result();
    QCOMPARE(result.policyCode(), QStringLiteral("cookie.required_option_failed"));
    QVERIFY(result.error().contains(QStringLiteral("CURLOPT_SHARE")));
    QVERIFY(!result.error().contains(QStringLiteral("secret")));
}

void TestQCNetworkCookieBridge::testRequiredShareCookieOptionFailure()
{
    QCNetworkAccessManager manager;
    QCNetworkAccessManager::ShareHandleConfig shareCfg;
    shareCfg.setShareCookies(true);
    manager.setShareHandleConfig(shareCfg);
    qputenv("QCURL_TEST_FORCE_SHARE_OPTION_ERROR", "CURLSHOPT_SHARE:CURL_LOCK_DATA_COOKIE");

    QCCookie cookie("session", "secret");
    cookie.setDomain("example.com");
    cookie.setPath("/");
    const auto future = manager.importCookiesAsync({cookie}, QUrl("https://example.com/"));
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 1000);
    const auto result = future.result();
    QCOMPARE(result.policyCode(), QStringLiteral("cookie.required_option_failed"));
    QVERIFY(result.error().contains(QStringLiteral("CURLSHOPT_SHARE")));
    QVERIFY(result.error().contains(QStringLiteral("CURL_LOCK_DATA_COOKIE")));
    QVERIFY(!result.error().contains(QStringLiteral("secret")));
}

void TestQCNetworkCookieBridge::testExportRequiredSetupOptionFailure()
{
    QCNetworkAccessManager manager;
    QCNetworkAccessManager::ShareHandleConfig shareCfg;
    shareCfg.setShareCookies(true);
    manager.setShareHandleConfig(shareCfg);

    QCCookie baseline("baseline", "kept");
    baseline.setDomain("example.com");
    baseline.setPath("/");
    QString error;
    QVERIFY2(manager.importCookies({baseline}, QUrl("https://example.com/"), &error),
             qPrintable(error));

    qputenv("QCURL_TEST_FORCE_COOKIE_OPTION_ERROR", "setup:CURLOPT_COOKIEFILE");
    const auto future = manager.exportCookiesAsync(QUrl("https://example.com/"));
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 1000);
    const auto result = future.result();
    QCOMPARE(result.policyCode(), QStringLiteral("cookie.required_option_failed"));
    QVERIFY(result.error().contains(QStringLiteral("CURLOPT_COOKIEFILE")));
    QVERIFY(result.cookies().isEmpty());
}

void TestQCNetworkCookieBridge::testRequiredClearAllFailurePreservesStore()
{
    QCNetworkAccessManager manager;
    QCNetworkAccessManager::ShareHandleConfig shareCfg;
    shareCfg.setShareCookies(true);
    manager.setShareHandleConfig(shareCfg);

    QCCookie baseline("baseline", "kept");
    baseline.setDomain("example.com");
    baseline.setPath("/");
    QString error;
    QVERIFY2(manager.importCookies({baseline}, QUrl("https://example.com/"), &error),
             qPrintable(error));

    qputenv("QCURL_TEST_FORCE_COOKIE_OPTION_ERROR", "clear:CURLOPT_COOKIELIST:1");
    const auto future = manager.clearAllCookiesAsync();
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 1000);
    const auto result = future.result();
    QCOMPARE(result.policyCode(), QStringLiteral("cookie.clear_failed"));
    QVERIFY(result.error().contains(QStringLiteral("CURLOPT_COOKIELIST")));

    qunsetenv("QCURL_TEST_FORCE_COOKIE_OPTION_ERROR");
    const auto exported = manager.exportCookies(QUrl("https://example.com/"), &error);
    QVERIFY2(exported.has_value(), qPrintable(error));
    QVERIFY(hasCookie(*exported, "baseline", "kept"));
}

void TestQCNetworkCookieBridge::testRequiredClearFlushFailureIsPersistenceFailure()
{
    QCNetworkAccessManager manager;
    QCNetworkAccessManager::ShareHandleConfig shareCfg;
    shareCfg.setShareCookies(true);
    manager.setShareHandleConfig(shareCfg);

    QCCookie baseline("baseline", "kept");
    baseline.setDomain("example.com");
    baseline.setPath("/");
    QString error;
    QVERIFY2(manager.importCookies({baseline}, QUrl("https://example.com/"), &error),
             qPrintable(error));

    qputenv("QCURL_TEST_FORCE_COOKIE_OPTION_ERROR", "persist:CURLOPT_COOKIELIST:1");
    const auto future = manager.clearAllCookiesAsync();
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 1000);
    const auto result = future.result();
    QCOMPARE(result.policyCode(), QStringLiteral("cookie.persistence_failed"));
    QVERIFY(result.error().contains(QStringLiteral("CURLOPT_COOKIELIST")));
}

void TestQCNetworkCookieBridge::testPartialImportFailureRollsBackSnapshot()
{
    QCNetworkAccessManager manager;
    QCNetworkAccessManager::ShareHandleConfig shareCfg;
    shareCfg.setShareCookies(true);
    manager.setShareHandleConfig(shareCfg);

    QCCookie baseline("baseline", "kept");
    baseline.setDomain("example.com");
    baseline.setPath("/");
    QString error;
    QVERIFY2(manager.importCookies({baseline}, QUrl("https://example.com/"), &error),
             qPrintable(error));

    QCCookie first("first", "one");
    first.setDomain("example.com");
    first.setPath("/");
    QCCookie second("second", "two");
    second.setDomain("example.com");
    second.setPath("/");
    qputenv("QCURL_TEST_FORCE_COOKIE_OPTION_ERROR", "apply:CURLOPT_COOKIELIST:2");
    const auto future = manager.importCookiesAsync({first, second}, QUrl("https://example.com/"));
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 1000);
    const auto result = future.result();
    QCOMPARE(result.policyCode(), QStringLiteral("cookie.apply_failed_rolled_back"));

    qunsetenv("QCURL_TEST_FORCE_COOKIE_OPTION_ERROR");
    const auto exported = manager.exportCookies(QUrl("https://example.com/"), &error);
    QVERIFY2(exported.has_value(), qPrintable(error));
    QVERIFY(hasCookie(*exported, "baseline", "kept"));
    QVERIFY(!hasCookie(*exported, "first", "one"));
    QVERIFY(!hasCookie(*exported, "second", "two"));
}

void TestQCNetworkCookieBridge::testSnapshotFailureRejectsBeforeMutation()
{
    QCNetworkAccessManager manager;
    QCNetworkAccessManager::ShareHandleConfig shareCfg;
    shareCfg.setShareCookies(true);
    manager.setShareHandleConfig(shareCfg);

    QCCookie baseline("baseline", "kept");
    baseline.setDomain("example.com");
    baseline.setPath("/");
    QString error;
    QVERIFY2(manager.importCookies({baseline}, QUrl("https://example.com/"), &error),
             qPrintable(error));

    QCCookie candidate("candidate", "not-applied");
    candidate.setDomain("example.com");
    candidate.setPath("/");
    qputenv("QCURL_TEST_FORCE_COOKIE_OPTION_ERROR", "snapshot:CURLINFO_COOKIELIST:1");
    const auto future = manager.importCookiesAsync({candidate}, QUrl("https://example.com/"));
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 1000);
    QCOMPARE(future.result().policyCode(), QStringLiteral("cookie.snapshot_failed"));

    qunsetenv("QCURL_TEST_FORCE_COOKIE_OPTION_ERROR");
    const auto exported = manager.exportCookies(QUrl("https://example.com/"), &error);
    QVERIFY2(exported.has_value(), qPrintable(error));
    QVERIFY(hasCookie(*exported, "baseline", "kept"));
    QVERIFY(!hasCookie(*exported, "candidate", "not-applied"));
}

void TestQCNetworkCookieBridge::testRollbackFailurePoisonsCookieStore()
{
    QCNetworkAccessManager manager;
    QCNetworkAccessManager::ShareHandleConfig shareCfg;
    shareCfg.setShareCookies(true);
    manager.setShareHandleConfig(shareCfg);

    QCCookie first("first", "one");
    first.setDomain("example.com");
    first.setPath("/");
    QCCookie second("second", "two");
    second.setDomain("example.com");
    second.setPath("/");
    qputenv("QCURL_TEST_FORCE_COOKIE_OPTION_ERROR",
            "apply:CURLOPT_COOKIELIST:2,rollback:CURLOPT_COOKIELIST:1");
    const auto future = manager.importCookiesAsync({first, second}, QUrl("https://example.com/"));
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 1000);
    const auto result = future.result();
    QCOMPARE(result.policyCode(), QStringLiteral("cookie.rollback_failed"));

    qunsetenv("QCURL_TEST_FORCE_COOKIE_OPTION_ERROR");
    const auto exportFuture = manager.exportCookiesAsync(QUrl("https://example.com/"));
    QTRY_VERIFY_WITH_TIMEOUT(exportFuture.isFinished(), 1000);
    QCOMPARE(exportFuture.result().policyCode(), QStringLiteral("cookie.store_poisoned"));
}

void TestQCNetworkCookieBridge::testFlushFailureIsPersistenceFailure()
{
    QCNetworkAccessManager manager;
    QCNetworkAccessManager::ShareHandleConfig shareCfg;
    shareCfg.setShareCookies(true);
    manager.setShareHandleConfig(shareCfg);
    qputenv("QCURL_TEST_FORCE_COOKIE_OPTION_ERROR", "persist:CURLOPT_COOKIELIST:1");

    QCCookie cookie("session", "secret");
    cookie.setDomain("example.com");
    cookie.setPath("/");
    const auto future = manager.importCookiesAsync({cookie}, QUrl("https://example.com/"));
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 1000);
    const auto result = future.result();
    QCOMPARE(result.policyCode(), QStringLiteral("cookie.persistence_failed"));
    QVERIFY(result.error().contains(QStringLiteral("CURLOPT_COOKIELIST")));
    QVERIFY(!result.error().contains(QStringLiteral("secret")));
}

QTEST_MAIN(TestQCNetworkCookieBridge)

#include "tst_QCNetworkCookieBridge.moc"
