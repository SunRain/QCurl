/**
 * @file tst_QCNetworkRequestCanonicalApi.cpp
 * @brief QCNetworkRequest canonical send* API 语义回归测试（离线：MockHandler）
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"

#include <QCoreApplication>
#include <QEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>
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

class TestQCNetworkRequestCanonicalApi : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testSendGet_preservesHeaderWhenAddingQueryParam();
    void testSendPost_sendsJsonBodyAndContentType();
    void testSendPut_preservesBodyWithExplicitContentType();
    void testSendHead_sendsHeadWithoutBody();

private:
    QCNetworkAccessManager *m_manager = nullptr;
    QCNetworkMockHandler m_mock;
};

void TestQCNetworkRequestCanonicalApi::init()
{
    m_manager = new QCNetworkAccessManager(this);

    m_mock.clear();
    m_mock.clearCapturedRequests();
    m_mock.setCaptureEnabled(true);
    m_mock.setGlobalDelay(0);
    m_manager->setMockHandler(&m_mock);
}

void TestQCNetworkRequestCanonicalApi::cleanup()
{
    if (m_manager) {
        m_manager->deleteLater();
        m_manager = nullptr;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

void TestQCNetworkRequestCanonicalApi::testSendGet_preservesHeaderWhenAddingQueryParam()
{
    const QUrl expectedUrl("http://example.com/api?page=1");
    m_mock.mockResponse(HttpMethod::Get, expectedUrl, QByteArray("OK"));
    m_mock.clearCapturedRequests();

    QCNetworkRequest request(QUrl("http://example.com/api"));
    request.setRawHeader("User-Agent", "TestAgent");

    QUrl url = request.url();
    QUrlQuery query(url);
    query.addQueryItem("page", "1");
    url.setQuery(query);
    request.setUrl(url);

    auto *reply = m_manager->sendGet(request);
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

void TestQCNetworkRequestCanonicalApi::testSendPost_sendsJsonBodyAndContentType()
{
    const QUrl url("http://example.com/users");
    m_mock.mockResponse(HttpMethod::Post, url, QByteArray("CREATED"), 201);
    m_mock.clearCapturedRequests();

    QJsonObject json;
    json["name"] = QStringLiteral("Alice");
    json["age"]  = 30;
    const QByteArray expectedBody = QJsonDocument(json).toJson(QJsonDocument::Compact);

    QCNetworkRequest request(url);
    request.setRawHeader("User-Agent", "QCurl-Test/2.9.0");
    request.setRawHeader("Content-Type", "application/json");

    auto *reply = m_manager->sendPost(request, expectedBody);
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

void TestQCNetworkRequestCanonicalApi::testSendPut_preservesBodyWithExplicitContentType()
{
    const QUrl url("http://example.com/upload");
    m_mock.mockResponse(HttpMethod::Put, url, QByteArray("OK"));
    m_mock.clearCapturedRequests();

    const QByteArray body("raw binary data");
    QCNetworkRequest request(url);
    request.setRawHeader("Content-Type", "application/octet-stream");

    auto *reply = m_manager->sendPut(request, body);
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

void TestQCNetworkRequestCanonicalApi::testSendHead_sendsHeadWithoutBody()
{
    const QUrl url("http://example.com/head");
    m_mock.mockResponse(HttpMethod::Head, url, QByteArray(), 200);
    m_mock.clearCapturedRequests();

    QCNetworkRequest request(url);
    auto *reply = m_manager->sendHead(request);
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

QTEST_MAIN(TestQCNetworkRequestCanonicalApi)
#include "tst_QCNetworkRequestCanonicalApi.moc"
