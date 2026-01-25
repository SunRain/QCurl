/**
 * @file tst_QCRequest.cpp
 * @brief QCRequest 流式 API 单元测试（离线：MockHandler）
 * @version v2.9.0
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCRequest.h"

#include <QCoreApplication>
#include <QEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

#include <optional>

using namespace QCurl;

static std::optional<QByteArray> findHeaderValue(const QList<QPair<QByteArray, QByteArray>> &headers,
                                                 const QByteArray &nameLower)
{
    for (const auto &kv : headers) {
        if (kv.first.trimmed().toLower() == nameLower) {
            return kv.second;
        }
    }
    return std::nullopt;
}

class TestQCRequest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testGet_preservesHeaderWhenAddingQueryParam();
    void testPost_withJson_sendsJsonBodyAndContentType();
    void testPut_withBody_usesDefaultContentType();
    void testHead_sendsHeadWithoutBody();

private:
    QCNetworkAccessManager *m_manager = nullptr;
    QCNetworkMockHandler m_mock;
};

void TestQCRequest::init()
{
    m_manager = new QCNetworkAccessManager(this);

    m_mock.clear();
    m_mock.clearCapturedRequests();
    m_mock.setCaptureEnabled(true);
    m_mock.setGlobalDelay(0);
    m_manager->setMockHandler(&m_mock);
}

void TestQCRequest::cleanup()
{
    if (m_manager) {
        m_manager->deleteLater();
        m_manager = nullptr;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

void TestQCRequest::testGet_preservesHeaderWhenAddingQueryParam()
{
    const QUrl expectedUrl("http://example.com/api?page=1");
    m_mock.mockResponse(HttpMethod::Get, expectedUrl, QByteArray("OK"));
    m_mock.clearCapturedRequests();

    auto request = QCRequest::get("http://example.com/api")
                       .withHeader("User-Agent", "TestAgent")
                       .withQueryParam("page", "1");

    auto *reply = request.send(m_manager);
    QVERIFY(reply != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    QCOMPARE(reply->error(), NetworkError::NoError);

    auto data = reply->readAll();
    QVERIFY(data.has_value());
    QCOMPARE(*data, QByteArray("OK"));

    const auto captured = m_mock.takeCapturedRequests();
    QCOMPARE(captured.size(), 1);
    QCOMPARE(captured.first().url, expectedUrl);
    QCOMPARE(captured.first().method, HttpMethod::Get);

    const auto userAgent = findHeaderValue(captured.first().headers, QByteArrayLiteral("user-agent"));
    QVERIFY(userAgent.has_value());
    QCOMPARE(*userAgent, QByteArray("TestAgent"));

    reply->deleteLater();
}

void TestQCRequest::testPost_withJson_sendsJsonBodyAndContentType()
{
    const QUrl url("http://example.com/users");
    m_mock.mockResponse(HttpMethod::Post, url, QByteArray("CREATED"), 201);
    m_mock.clearCapturedRequests();

    QJsonObject json;
    json["name"] = QStringLiteral("Alice");
    json["age"]  = 30;
    const QByteArray expectedBody = QJsonDocument(json).toJson(QJsonDocument::Compact);

    auto request = QCRequest::post(url)
                       .withHeader("User-Agent", "QCurl-Test/2.9.0")
                       .withJson(json);

    auto *reply = request.send(m_manager);
    QVERIFY(reply != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->httpStatusCode(), 201);

    auto data = reply->readAll();
    QVERIFY(data.has_value());
    QCOMPARE(*data, QByteArray("CREATED"));

    const auto captured = m_mock.takeCapturedRequests();
    QCOMPARE(captured.size(), 1);
    QCOMPARE(captured.first().url, url);
    QCOMPARE(captured.first().method, HttpMethod::Post);
    QCOMPARE(captured.first().bodySize, expectedBody.size());
    QCOMPARE(captured.first().bodyPreview, expectedBody);

    const auto contentType = findHeaderValue(captured.first().headers, QByteArrayLiteral("content-type"));
    QVERIFY(contentType.has_value());
    QCOMPARE(*contentType, QByteArray("application/json"));

    const auto userAgent = findHeaderValue(captured.first().headers, QByteArrayLiteral("user-agent"));
    QVERIFY(userAgent.has_value());
    QCOMPARE(*userAgent, QByteArray("QCurl-Test/2.9.0"));

    reply->deleteLater();
}

void TestQCRequest::testPut_withBody_usesDefaultContentType()
{
    const QUrl url("http://example.com/upload");
    m_mock.mockResponse(HttpMethod::Put, url, QByteArray("OK"));
    m_mock.clearCapturedRequests();

    const QByteArray body("raw binary data");
    auto request = QCRequest::put(url).withBody(body);

    auto *reply = request.send(m_manager);
    QVERIFY(reply != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    QCOMPARE(reply->error(), NetworkError::NoError);

    const auto captured = m_mock.takeCapturedRequests();
    QCOMPARE(captured.size(), 1);
    QCOMPARE(captured.first().url, url);
    QCOMPARE(captured.first().method, HttpMethod::Put);
    QCOMPARE(captured.first().bodySize, body.size());
    QCOMPARE(captured.first().bodyPreview, body);

    const auto contentType = findHeaderValue(captured.first().headers, QByteArrayLiteral("content-type"));
    QVERIFY(contentType.has_value());
    QCOMPARE(*contentType, QByteArray("application/octet-stream"));

    reply->deleteLater();
}

void TestQCRequest::testHead_sendsHeadWithoutBody()
{
    const QUrl url("http://example.com/head");
    m_mock.mockResponse(HttpMethod::Head, url, QByteArray(), 200);
    m_mock.clearCapturedRequests();

    auto *reply = QCRequest::head(url).send(m_manager);
    QVERIFY(reply != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    QCOMPARE(reply->error(), NetworkError::NoError);

    auto body = reply->readAll();
    QVERIFY(body.has_value());
    QCOMPARE(body->size(), 0);

    const auto captured = m_mock.takeCapturedRequests();
    QCOMPARE(captured.size(), 1);
    QCOMPARE(captured.first().url, url);
    QCOMPARE(captured.first().method, HttpMethod::Head);
    QCOMPARE(captured.first().bodySize, 0);

    reply->deleteLater();
}

QTEST_MAIN(TestQCRequest)
#include "tst_QCRequest.moc"
