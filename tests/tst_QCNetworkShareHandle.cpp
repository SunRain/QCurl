/**
 * @file tst_QCNetworkShareHandle.cpp
 * @brief multi share handle（DNS/Cookie/SSL session）基础契约测试
 *
 * 目标：
 * - 默认关闭：不共享 Cookie（两次独立请求无法复用登录态）
 * - 显式开启 shareCookies：同一 QCNetworkAccessManager 内可共享 Cookie（跨请求）
 * - 隔离：不同 manager 间 Cookie 不互通（避免多账号/多租户污染）
 * - 并发 smoke：开启 shareCookies 后高并发请求不死锁不崩溃
 */

#include <QtTest/QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QHostAddress>
#include <QProcess>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrlQuery>
#include <QVector>

#include "QCNetworkAccessManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"

using namespace QCurl;

class TestQCNetworkShareHandle : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void testCookieShareDisabledByDefault();
    void testCookieShareWithinManager();
    void testCookieShareIsolationAcrossManagers();
    void testCookieShareConcurrencySmoke();

private:
    [[nodiscard]] bool startObserveServer();
    void stopObserveServer();

    static bool waitForPortReady(quint16 port, int timeoutMs);
    static bool waitForFinished(QCNetworkReply *reply, int timeoutMs);

    QTemporaryDir m_tempDir;
    QProcess m_server;
    QString m_skipReason;
    quint16 m_port = 0;
    QString m_baseUrl;
};

void TestQCNetworkShareHandle::initTestCase()
{
    if (!startObserveServer()) {
        QWARN(qPrintable(m_skipReason));
    }
}

void TestQCNetworkShareHandle::cleanupTestCase()
{
    stopObserveServer();
}

bool TestQCNetworkShareHandle::waitForFinished(QCNetworkReply *reply, int timeoutMs)
{
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    QObject::connect(reply, &QCNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    timer.start(timeoutMs);
    loop.exec();

    return reply->isFinished();
}

bool TestQCNetworkShareHandle::waitForPortReady(quint16 port, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs) {
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, port);
        if (socket.waitForConnected(200)) {
            socket.disconnectFromHost();
            return true;
        }
        QTest::qWait(50);
    }
    return false;
}

bool TestQCNetworkShareHandle::startObserveServer()
{
    stopObserveServer();
    m_skipReason.clear();

    if (!m_tempDir.isValid()) {
        m_skipReason = QStringLiteral("无法创建临时目录，跳过 share handle 测试");
        return false;
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString scriptPath = QDir(appDir).absoluteFilePath(
        QStringLiteral("../../tests/libcurl_consistency/http_observe_server.py"));
    if (!QFileInfo::exists(scriptPath)) {
        m_skipReason = QStringLiteral("未找到 http_observe_server.py，跳过 share handle 测试");
        return false;
    }

    QTcpServer portProbe;
    if (!portProbe.listen(QHostAddress::LocalHost, 0)) {
        m_skipReason = QStringLiteral("无法申请本地端口，跳过 share handle 测试");
        return false;
    }
    m_port = portProbe.serverPort();
    portProbe.close();

    const QString logPath = QDir(m_tempDir.path()).absoluteFilePath(QStringLiteral("observe.jsonl"));
    m_server.setProgram(QStringLiteral("python3"));
    m_server.setArguments({scriptPath,
                           QStringLiteral("--port"),
                           QString::number(m_port),
                           QStringLiteral("--log-file"),
                           logPath});
    m_server.setProcessChannelMode(QProcess::MergedChannels);
    m_server.start();

    if (!m_server.waitForStarted(2000)) {
        m_skipReason = QStringLiteral("无法启动本地观测服务端（python3），跳过 share handle 测试");
        return false;
    }

    if (!waitForPortReady(m_port, 3000)) {
        const QString output = QString::fromUtf8(m_server.readAll());
        if (!output.isEmpty()) {
            qWarning().noquote() << "observe server output:\n" << output;
        }

        stopObserveServer();
        m_skipReason = QStringLiteral("本地观测服务端未就绪（端口未监听），跳过 share handle 测试");
        return false;
    }

    m_baseUrl = QStringLiteral("http://localhost:%1").arg(m_port);
    return true;
}

void TestQCNetworkShareHandle::stopObserveServer()
{
    if (m_server.state() == QProcess::NotRunning) {
        return;
    }

    m_server.terminate();
    if (!m_server.waitForFinished(1500)) {
        m_server.kill();
        m_server.waitForFinished(1500);
    }
}

void TestQCNetworkShareHandle::testCookieShareDisabledByDefault()
{
    if (!m_skipReason.isEmpty()) {
        QSKIP(qPrintable(m_skipReason));
    }

    QCNetworkAccessManager manager;

    QCNetworkRequest loginReq(QUrl(m_baseUrl + QStringLiteral("/login")));
    loginReq.setFollowLocation(false);
    QCNetworkReply *loginReply = manager.sendGet(loginReq);
    QVERIFY(waitForFinished(loginReply, 5000));
    QCOMPARE(loginReply->error(), NetworkError::NoError);
    loginReply->deleteLater();

    QCNetworkRequest homeReq(QUrl(m_baseUrl + QStringLiteral("/home")));
    homeReq.setFollowLocation(false);
    QCNetworkReply *homeReply = manager.sendGet(homeReq);
    QVERIFY(waitForFinished(homeReply, 5000));
    QVERIFY(homeReply->error() != NetworkError::NoError);
    homeReply->deleteLater();

    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void TestQCNetworkShareHandle::testCookieShareWithinManager()
{
    if (!m_skipReason.isEmpty()) {
        QSKIP(qPrintable(m_skipReason));
    }

    QCNetworkAccessManager manager;
    QCNetworkAccessManager::ShareHandleConfig shareCfg;
    shareCfg.shareCookies = true;
    manager.setShareHandleConfig(shareCfg);

    QCNetworkRequest loginReq(QUrl(m_baseUrl + QStringLiteral("/login")));
    loginReq.setFollowLocation(false);
    QCNetworkReply *loginReply = manager.sendGet(loginReq);
    QVERIFY(waitForFinished(loginReply, 5000));
    QCOMPARE(loginReply->error(), NetworkError::NoError);
    loginReply->deleteLater();

    QCNetworkRequest homeReq(QUrl(m_baseUrl + QStringLiteral("/home")));
    homeReq.setFollowLocation(false);
    QCNetworkReply *homeReply = manager.sendGet(homeReq);
    QVERIFY(waitForFinished(homeReply, 5000));
    QCOMPARE(homeReply->error(), NetworkError::NoError);
    const auto body = homeReply->readAll();
    QVERIFY(body.has_value());
    QCOMPARE(*body, QByteArray("home-ok\n"));
    homeReply->deleteLater();

    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void TestQCNetworkShareHandle::testCookieShareIsolationAcrossManagers()
{
    if (!m_skipReason.isEmpty()) {
        QSKIP(qPrintable(m_skipReason));
    }

    QCNetworkAccessManager managerA;
    QCNetworkAccessManager::ShareHandleConfig shareCfg;
    shareCfg.shareCookies = true;
    managerA.setShareHandleConfig(shareCfg);

    QCNetworkAccessManager managerB;
    managerB.setShareHandleConfig(shareCfg);

    QCNetworkRequest loginReq(QUrl(m_baseUrl + QStringLiteral("/login")));
    loginReq.setFollowLocation(false);
    QCNetworkReply *loginReply = managerA.sendGet(loginReq);
    QVERIFY(waitForFinished(loginReply, 5000));
    QCOMPARE(loginReply->error(), NetworkError::NoError);
    loginReply->deleteLater();

    QCNetworkRequest homeReq(QUrl(m_baseUrl + QStringLiteral("/home")));
    homeReq.setFollowLocation(false);

    QCNetworkReply *homeReplyB = managerB.sendGet(homeReq);
    QVERIFY(waitForFinished(homeReplyB, 5000));
    QVERIFY(homeReplyB->error() != NetworkError::NoError);
    homeReplyB->deleteLater();

    QCNetworkReply *homeReplyA = managerA.sendGet(homeReq);
    QVERIFY(waitForFinished(homeReplyA, 5000));
    QCOMPARE(homeReplyA->error(), NetworkError::NoError);
    homeReplyA->deleteLater();

    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void TestQCNetworkShareHandle::testCookieShareConcurrencySmoke()
{
    if (!m_skipReason.isEmpty()) {
        QSKIP(qPrintable(m_skipReason));
    }

    QCNetworkAccessManager manager;
    QCNetworkAccessManager::ShareHandleConfig shareCfg;
    shareCfg.shareCookies = true;
    manager.setShareHandleConfig(shareCfg);

    QCNetworkRequest loginReq(QUrl(m_baseUrl + QStringLiteral("/login")));
    loginReq.setFollowLocation(false);
    QCNetworkReply *loginReply = manager.sendGet(loginReq);
    QVERIFY(waitForFinished(loginReply, 5000));
    QCOMPARE(loginReply->error(), NetworkError::NoError);
    loginReply->deleteLater();

    constexpr int kConcurrency = 64;
    QVector<QPointer<QCNetworkReply>> replies;
    replies.reserve(kConcurrency);

    int finishedCount = 0;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(15000);

    for (int i = 0; i < kConcurrency; ++i) {
        QUrl url(m_baseUrl + QStringLiteral("/home"));
        QUrlQuery query(url);
        query.addQueryItem(QStringLiteral("id"), QString::number(i));
        url.setQuery(query);

        QCNetworkRequest req(url);
        req.setFollowLocation(false);
        QCNetworkReply *reply = manager.sendGet(req);
        replies.append(reply);

        QObject::connect(reply, &QCNetworkReply::finished, this, [&finishedCount, &loop]() {
            finishedCount += 1;
            if (finishedCount >= kConcurrency) {
                loop.quit();
            }
        });
    }

    loop.exec();
    QCOMPARE(finishedCount, kConcurrency);

    for (const auto &r : replies) {
        QVERIFY(r);
        QVERIFY(r->isFinished());
        QCOMPARE(r->error(), NetworkError::NoError);
        r->deleteLater();
    }

    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

QTEST_MAIN(TestQCNetworkShareHandle)
#include "tst_QCNetworkShareHandle.moc"

