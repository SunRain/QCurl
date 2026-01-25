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

bool hasCookie(const QList<QNetworkCookie> &cookies, const QByteArray &name, const QByteArray &value)
{
    for (const QNetworkCookie &c : cookies) {
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

private slots:
    void testImportExportRoundTrip();
    void testExportFilter_HostAndPathIsolation();
    void testClearAllCookies();
};

void TestQCNetworkCookieBridge::testImportExportRoundTrip()
{
    QCNetworkAccessManager manager;
    QCNetworkAccessManager::ShareHandleConfig shareCfg;
    shareCfg.shareCookies = true;
    manager.setShareHandleConfig(shareCfg);

    QNetworkCookie sid("sid", "abc");
    sid.setDomain("example.com");
    sid.setPath("/foo");
    sid.setSecure(true);
    sid.setHttpOnly(true);
    sid.setExpirationDate(QDateTime::fromSecsSinceEpoch(1893456000, QTimeZone::utc()));

    QString err;
    QVERIFY(manager.importCookies({sid}, QUrl("https://example.com/"), &err));

    const QList<QNetworkCookie> exported = manager.exportCookies(QUrl(
                                                                     "https://example.com/foo/bar"),
                                                                 &err);
    QVERIFY2(!exported.isEmpty(), qPrintable(err));

    for (const QNetworkCookie &c : exported) {
        if (c.name() == QByteArray("sid") && c.value() == QByteArray("abc")) {
            QVERIFY(c.isSecure());
            QVERIFY(c.isHttpOnly());
            QCOMPARE(c.expirationDate().toSecsSinceEpoch(), sid.expirationDate().toSecsSinceEpoch());
            break;
        }
    }
    QVERIFY(hasCookie(exported, "sid", "abc"));
}

void TestQCNetworkCookieBridge::testExportFilter_HostAndPathIsolation()
{
    QCNetworkAccessManager manager;
    QCNetworkAccessManager::ShareHandleConfig shareCfg;
    shareCfg.shareCookies = true;
    manager.setShareHandleConfig(shareCfg);

    QNetworkCookie hostOnly("hostonly", "1");
    hostOnly.setDomain("example.com");
    hostOnly.setPath("/foo");

    QNetworkCookie domainCookie("domain", "1");
    domainCookie.setDomain(".example.com");
    domainCookie.setPath("/foo");

    QString err;
    QVERIFY(manager.importCookies({hostOnly, domainCookie}, QUrl("https://example.com/"), &err));

    const QList<QNetworkCookie> ex1 = manager.exportCookies(QUrl("https://example.com/foo/bar"),
                                                            &err);
    QVERIFY(hasCookie(ex1, "hostonly", "1"));
    QVERIFY(hasCookie(ex1, "domain", "1"));

    const QList<QNetworkCookie> ex2 = manager.exportCookies(QUrl("https://sub.example.com/foo/bar"),
                                                            &err);
    QVERIFY(!hasCookie(ex2, "hostonly", "1"));
    QVERIFY(hasCookie(ex2, "domain", "1"));

    // 路径匹配要求边界正确：/foo 不应匹配 /foobar
    const QList<QNetworkCookie> ex3 = manager.exportCookies(QUrl("https://example.com/foobar"),
                                                            &err);
    QVERIFY(!hasCookie(ex3, "hostonly", "1"));
    QVERIFY(!hasCookie(ex3, "domain", "1"));
}

void TestQCNetworkCookieBridge::testClearAllCookies()
{
    QCNetworkAccessManager manager;
    QCNetworkAccessManager::ShareHandleConfig shareCfg;
    shareCfg.shareCookies = true;
    manager.setShareHandleConfig(shareCfg);

    QNetworkCookie sid("sid", "abc");
    sid.setDomain("example.com");
    sid.setPath("/");

    QString err;
    QVERIFY(manager.importCookies({sid}, QUrl("https://example.com/"), &err));
    QVERIFY(!manager.exportCookies(QUrl("https://example.com/"), &err).isEmpty());

    QVERIFY2(manager.clearAllCookies(&err), qPrintable(err));
    QVERIFY(manager.exportCookies(QUrl("https://example.com/"), &err).isEmpty());
}

QTEST_MAIN(TestQCNetworkCookieBridge)

#include "tst_QCNetworkCookieBridge.moc"
