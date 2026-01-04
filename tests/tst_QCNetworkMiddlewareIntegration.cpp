// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include <QtTest>
#include <QSignalSpy>
#include <QEvent>
#include <QNetworkCookie>

#include "QCNetworkAccessManager.h"
#include "QCNetworkMiddleware.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRequestScheduler.h"

using namespace QCurl;

class tst_QCNetworkMiddlewareIntegration : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testSendGet_AutoWiredMiddleware();
    void testScheduleGet_AutoWiredMiddleware();
    void testXsrfCookieToHeader_InMiddleware();

private:
    QCNetworkAccessManager *m_manager = nullptr;
    QCNetworkMockHandler m_mockHandler;
};

void tst_QCNetworkMiddlewareIntegration::init()
{
    m_manager = new QCNetworkAccessManager(this);
    m_manager->enableRequestScheduler(false);

    m_mockHandler.clear();
    m_mockHandler.clearCapturedRequests();
    m_mockHandler.setCaptureEnabled(true);
    m_mockHandler.setCaptureBodyPreviewLimit(64);
    m_manager->setMockHandler(&m_mockHandler);

    QCNetworkRequestScheduler::instance()->cancelAllRequests();
}

void tst_QCNetworkMiddlewareIntegration::cleanup()
{
    QCNetworkRequestScheduler::instance()->cancelAllRequests();

    if (m_manager) {
        m_manager->clearMiddlewares();
        m_manager->setMockHandler(nullptr);
        m_manager->deleteLater();
        m_manager = nullptr;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

void tst_QCNetworkMiddlewareIntegration::testSendGet_AutoWiredMiddleware()
{
    class OrderMiddleware final : public QCNetworkMiddleware
    {
    public:
        OrderMiddleware(const QByteArray &headerValue, QStringList &events)
            : m_headerValue(headerValue),
              m_events(events)
        {
        }

        void onRequestPreSend(QCNetworkRequest &request) override
        {
            request.setRawHeader("X-Test", m_headerValue);
            m_events.append(QString::fromLatin1(m_headerValue) + QStringLiteral("_request"));
        }

        void onResponseReceived(QCNetworkReply *reply) override
        {
            Q_UNUSED(reply);
            m_events.append(QString::fromLatin1(m_headerValue) + QStringLiteral("_response"));
        }

    private:
        QByteArray m_headerValue;
        QStringList &m_events;
    };

    QStringList events;
    OrderMiddleware mw1("A", events);
    OrderMiddleware mw2("B", events);
    m_manager->addMiddleware(&mw1);
    m_manager->addMiddleware(&mw2);

    const QUrl url("http://example.com/mw/sendget");
    m_mockHandler.mockResponse(url, QByteArray("ok"), 200);

    QCNetworkRequest request(url);
    auto *reply = m_manager->sendGet(request);
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
    QVERIFY(finishedSpy.wait(1000));

    const auto captured = m_mockHandler.takeCapturedRequests();
    QCOMPARE(captured.size(), 1);

    QByteArray xTest;
    for (const auto &h : captured[0].headers) {
        if (h.first == "X-Test") {
            xTest = h.second;
            break;
        }
    }
    QCOMPARE(xTest, QByteArray("B"));

    QCOMPARE(events.size(), 4);
    QCOMPARE(events.at(0), QStringLiteral("A_request"));
    QCOMPARE(events.at(1), QStringLiteral("B_request"));
    QCOMPARE(events.at(2), QStringLiteral("A_response"));
    QCOMPARE(events.at(3), QStringLiteral("B_response"));

    reply->deleteLater();
}

void tst_QCNetworkMiddlewareIntegration::testScheduleGet_AutoWiredMiddleware()
{
    class HeaderOnlyMiddleware final : public QCNetworkMiddleware
    {
    public:
        explicit HeaderOnlyMiddleware(const QByteArray &value)
            : m_value(value)
        {
        }

        void onRequestPreSend(QCNetworkRequest &request) override
        {
            request.setRawHeader("X-Test", m_value);
        }

    private:
        QByteArray m_value;
    };

    HeaderOnlyMiddleware mw1("A");
    HeaderOnlyMiddleware mw2("B");
    m_manager->addMiddleware(&mw1);
    m_manager->addMiddleware(&mw2);

    m_manager->enableRequestScheduler(true);

    const QUrl url("http://example.com/mw/scheduleget");
    m_mockHandler.mockResponse(url, QByteArray("ok"), 200);

    QCNetworkRequest request(url);
    auto *reply = m_manager->scheduleGet(request);
    QVERIFY(reply->parent() == m_manager);

    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
    QVERIFY(finishedSpy.wait(1000));

    const auto captured = m_mockHandler.takeCapturedRequests();
    QCOMPARE(captured.size(), 1);

    QByteArray xTest;
    for (const auto &h : captured[0].headers) {
        if (h.first == "X-Test") {
            xTest = h.second;
            break;
        }
    }
    QCOMPARE(xTest, QByteArray("B"));

    reply->deleteLater();
}

void tst_QCNetworkMiddlewareIntegration::testXsrfCookieToHeader_InMiddleware()
{
    class XsrfCookieHeaderMiddleware final : public QCNetworkMiddleware
    {
    public:
        XsrfCookieHeaderMiddleware(QCNetworkAccessManager *manager, const QString &host, const QString &pathPrefix)
            : m_manager(manager),
              m_host(host),
              m_pathPrefix(pathPrefix)
        {
        }

        void onRequestPreSend(QCNetworkRequest &request) override
        {
            if (!m_manager) {
                return;
            }

            const QUrl url = request.url();
            if (!m_host.isEmpty() && url.host() != m_host) {
                return;
            }
            if (!m_pathPrefix.isEmpty() && !url.path().startsWith(m_pathPrefix)) {
                return;
            }

            // 不覆盖用户显式设置的 header（大小写不敏感）
            for (const QByteArray &name : request.rawHeaderList()) {
                if (QString::fromLatin1(name).compare(QStringLiteral("X-XSRF-TOKEN"), Qt::CaseInsensitive) == 0) {
                    return;
                }
            }

            const QList<QNetworkCookie> cookies = m_manager->exportCookies(url);
            for (const QNetworkCookie &c : cookies) {
                if (c.name() == QByteArray("XSRF-TOKEN")) {
                    request.setRawHeader("X-XSRF-TOKEN", c.value());
                    break;
                }
            }
        }

        QString name() const override
        {
            return QStringLiteral("XsrfCookieHeaderMiddleware");
        }

    private:
        QCNetworkAccessManager *m_manager = nullptr;
        QString m_host;
        QString m_pathPrefix;
    };

    auto hasHeader = [](const QList<QPair<QByteArray, QByteArray>> &headers,
                        const QByteArray &name,
                        const QByteArray &value) -> bool {
        for (const auto &h : headers) {
            if (h.first.compare(name, Qt::CaseInsensitive) == 0 && h.second == value) {
                return true;
            }
        }
        return false;
    };

    QCNetworkAccessManager::ShareHandleConfig shareCfg;
    shareCfg.shareCookies = true;
    m_manager->setShareHandleConfig(shareCfg);

    QNetworkCookie xsrf("XSRF-TOKEN", "token123");
    xsrf.setDomain("example.com");
    xsrf.setPath("/id");

    QString err;
    QVERIFY(m_manager->importCookies({xsrf}, QUrl("https://example.com/id/"), &err));

    XsrfCookieHeaderMiddleware mw(m_manager, QStringLiteral("example.com"), QStringLiteral("/id"));
    m_manager->addMiddleware(&mw);

    // 1) 命中 host/path：应注入 X-XSRF-TOKEN
    m_mockHandler.clear();
    m_mockHandler.clearCapturedRequests();
    const QUrl url1("https://example.com/id/api/csrf");
    m_mockHandler.mockResponse(url1, QByteArray("ok"), 200);
    {
        QCNetworkRequest request(url1);
        auto *reply = m_manager->sendGet(request);
        QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
        QVERIFY(finishedSpy.wait(1000));
        const auto captured = m_mockHandler.takeCapturedRequests();
        QCOMPARE(captured.size(), 1);
        QVERIFY(hasHeader(captured[0].headers, "X-XSRF-TOKEN", "token123"));
        reply->deleteLater();
    }

    // 2) 用户显式设置 header：不应被覆盖
    m_mockHandler.clear();
    m_mockHandler.clearCapturedRequests();
    const QUrl url2("https://example.com/id/api/csrf_explicit");
    m_mockHandler.mockResponse(url2, QByteArray("ok"), 200);
    {
        QCNetworkRequest request(url2);
        request.setRawHeader("X-XSRF-TOKEN", "manual");
        auto *reply = m_manager->sendGet(request);
        QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
        QVERIFY(finishedSpy.wait(1000));
        const auto captured = m_mockHandler.takeCapturedRequests();
        QCOMPARE(captured.size(), 1);
        QVERIFY(hasHeader(captured[0].headers, "X-XSRF-TOKEN", "manual"));
        reply->deleteLater();
    }

    // 3) 不命中 path：不应注入
    m_mockHandler.clear();
    m_mockHandler.clearCapturedRequests();
    const QUrl url3("https://example.com/other/path");
    m_mockHandler.mockResponse(url3, QByteArray("ok"), 200);
    {
        QCNetworkRequest request(url3);
        auto *reply = m_manager->sendGet(request);
        QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
        QVERIFY(finishedSpy.wait(1000));
        const auto captured = m_mockHandler.takeCapturedRequests();
        QCOMPARE(captured.size(), 1);
        QVERIFY(!hasHeader(captured[0].headers, "X-XSRF-TOKEN", "token123"));
        reply->deleteLater();
    }

    // 4) 不命中 host：不应注入
    m_mockHandler.clear();
    m_mockHandler.clearCapturedRequests();
    const QUrl url4("https://other.example/id/api/csrf");
    m_mockHandler.mockResponse(url4, QByteArray("ok"), 200);
    {
        QCNetworkRequest request(url4);
        auto *reply = m_manager->sendGet(request);
        QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
        QVERIFY(finishedSpy.wait(1000));
        const auto captured = m_mockHandler.takeCapturedRequests();
        QCOMPARE(captured.size(), 1);
        QVERIFY(!hasHeader(captured[0].headers, "X-XSRF-TOKEN", "token123"));
        reply->deleteLater();
    }
}

QTEST_MAIN(tst_QCNetworkMiddlewareIntegration)
#include "tst_QCNetworkMiddlewareIntegration.moc"
