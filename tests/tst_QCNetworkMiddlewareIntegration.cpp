// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include <QtTest>
#include <QSignalSpy>
#include <QEvent>

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

QTEST_MAIN(tst_QCNetworkMiddlewareIntegration)
#include "tst_QCNetworkMiddlewareIntegration.moc"
