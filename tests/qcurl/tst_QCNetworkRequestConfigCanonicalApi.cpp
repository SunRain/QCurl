/**
 * @file tst_QCNetworkRequestConfigCanonicalApi.cpp
 * @brief QCNetworkRequest canonical config API 语义回归测试
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkRequest.h"
#include "QCNetworkReply.h"
#include "QCNetworkTimeoutConfig.h"

#include <QCoreApplication>
#include <QEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>
#include <QtTest/QtTest>

#include <chrono>
#include <initializer_list>
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

static QUrl withQueryItems(QUrl url,
                           std::initializer_list<std::pair<QString, QString>> items)
{
    QUrlQuery query(url);
    for (const auto &item : items) {
        query.addQueryItem(item.first, item.second);
    }
    url.setQuery(query);
    return url;
}

class TestQCNetworkRequestConfigCanonicalApi : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void testSetUrlAndHeaders();
    void testAddQueryParamViaUrlQuery();
    void testSetTimeoutVariants();
    void testSendPostRawBody();
    void testSendPostJsonBody();
    void testSetFollowRedirects();
    void testBuildEmptyUrl();
    void testMethodChaining();
    void testComplexConfig();
    void testSendDeleteWithBody();

private:
    QCNetworkAccessManager *m_manager = nullptr;
    QCNetworkMockHandler m_mock;
};

void TestQCNetworkRequestConfigCanonicalApi::initTestCase()
{
    qDebug() << "初始化 QCNetworkRequest canonical config 测试套件";
}

void TestQCNetworkRequestConfigCanonicalApi::cleanupTestCase()
{
    qDebug() << "清理 QCNetworkRequest canonical config 测试套件";
}

void TestQCNetworkRequestConfigCanonicalApi::init()
{
    m_manager = new QCNetworkAccessManager(this);

    m_mock.clear();
    m_mock.clearCapturedRequests();
    m_mock.setCaptureEnabled(true);
    m_mock.setGlobalDelay(0);
    m_manager->setMockHandler(&m_mock);
}

void TestQCNetworkRequestConfigCanonicalApi::cleanup()
{
    if (m_manager) {
        m_manager->deleteLater();
        m_manager = nullptr;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

void TestQCNetworkRequestConfigCanonicalApi::testSetUrlAndHeaders()
{
    QCNetworkRequest request(QUrl("https://example.com/api"));
    request.setRawHeader("Authorization", "Bearer token123")
        .setRawHeader("User-Agent", "QCurl-Test/2.9.0");

    QCOMPARE(request.url().toString(), QStringLiteral("https://example.com/api"));
    QCOMPARE(request.rawHeader("Authorization"), QByteArray("Bearer token123"));
    QCOMPARE(request.rawHeader("User-Agent"), QByteArray("QCurl-Test/2.9.0"));
}

void TestQCNetworkRequestConfigCanonicalApi::testAddQueryParamViaUrlQuery()
{
    QCNetworkRequest request(withQueryItems(QUrl("https://example.com/api"),
                                            {{"page", "1"}, {"limit", "10"}}));

    const QString urlString = request.url().toString();
    QVERIFY(urlString.contains("page=1"));
    QVERIFY(urlString.contains("limit=10"));
}

void TestQCNetworkRequestConfigCanonicalApi::testSetTimeoutVariants()
{
    QCNetworkRequest request(QUrl("https://example.com"));
    request.setTimeout(std::chrono::seconds(30));

    const auto timeout = request.timeoutConfig();
    QVERIFY(timeout.totalTimeout.has_value());
    QCOMPARE(timeout.totalTimeout->count(), std::chrono::milliseconds(30000).count());
    request.setTimeout(std::chrono::milliseconds(5000));
    const auto timeoutMs = request.timeoutConfig();
    QVERIFY(timeoutMs.totalTimeout.has_value());
    QCOMPARE(timeoutMs.totalTimeout->count(), std::chrono::milliseconds(5000).count());
}

void TestQCNetworkRequestConfigCanonicalApi::testSendPostRawBody()
{
    const QUrl url("http://example.com/body");
    const QByteArray body("raw test data");
    m_mock.mockResponse(HttpMethod::Post, url, QByteArray("OK"));
    m_mock.clearCapturedRequests();

    QCNetworkRequest request(url);
    request.setRawHeader("Content-Type", "application/octet-stream");

    auto *reply = m_manager->sendPost(request, body);
    QVERIFY(reply != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    QCOMPARE(reply->error(), NetworkError::NoError);

    const auto captured = m_mock.takeCapturedRequests();
    QCOMPARE(captured.size(), 1);
    QCOMPARE(captured.first().method, HttpMethod::Post);
    QCOMPARE(captured.first().bodySize, body.size());
    QCOMPARE(captured.first().bodyPreview, body);

    const auto contentType = findHeaderValue(captured.first().headers, QByteArrayLiteral("content-type"));
    QVERIFY(contentType.has_value());
    QCOMPARE(*contentType, QByteArray("application/octet-stream"));

    reply->deleteLater();
}

void TestQCNetworkRequestConfigCanonicalApi::testSendPostJsonBody()
{
    QJsonObject json;
    json["name"] = "Alice";
    json["age"]  = 30;

    const QUrl url("http://example.com/users");
    const QByteArray expectedBody = QJsonDocument(json).toJson(QJsonDocument::Compact);
    m_mock.mockResponse(HttpMethod::Post, url, QByteArray("OK"));
    m_mock.clearCapturedRequests();

    QCNetworkRequest request(url);
    request.setRawHeader("Content-Type", "application/json");

    auto *reply = m_manager->sendPost(request, expectedBody);
    QVERIFY(reply != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    QCOMPARE(reply->error(), NetworkError::NoError);

    const auto captured = m_mock.takeCapturedRequests();
    QCOMPARE(captured.size(), 1);
    QCOMPARE(captured.first().method, HttpMethod::Post);
    QCOMPARE(captured.first().bodySize, expectedBody.size());
    QCOMPARE(captured.first().bodyPreview, expectedBody);

    const auto contentType = findHeaderValue(captured.first().headers, QByteArrayLiteral("content-type"));
    QVERIFY(contentType.has_value());
    QCOMPARE(*contentType, QByteArray("application/json"));

    reply->deleteLater();
}

void TestQCNetworkRequestConfigCanonicalApi::testSetFollowRedirects()
{
    QCNetworkRequest request(QUrl("https://example.com"));

    request.setFollowLocation(true);
    QVERIFY(request.followLocation());

    request.setFollowLocation(false);
    QVERIFY(!request.followLocation());
}

void TestQCNetworkRequestConfigCanonicalApi::testBuildEmptyUrl()
{
    QCNetworkRequest request;
    QVERIFY(request.url().isEmpty());
}

void TestQCNetworkRequestConfigCanonicalApi::testMethodChaining()
{
    QCNetworkRequest request(QUrl("https://api.example.com"));
    const QUrl expectedUrl = withQueryItems(request.url(), {{"version", "v1"}});

    auto &result = request.setUrl(expectedUrl)
                       .setRawHeader("Accept", "application/json")
                       .setTimeout(std::chrono::seconds(20));

    QCOMPARE(&result, &request);
    QCOMPARE(request.url().toString(), expectedUrl.toString());
    QCOMPARE(request.rawHeader("Accept"), QByteArray("application/json"));
}

void TestQCNetworkRequestConfigCanonicalApi::testComplexConfig()
{
    QCNetworkRequest request(QUrl("https://api.example.com/register"));
    request.setRawHeader("User-Agent", "QCurl/2.9.0")
        .setRawHeader("Accept", "application/json")
        .setRawHeader("Content-Type", "application/json")
        .setTimeout(std::chrono::seconds(30))
        .setFollowLocation(true);

    QCOMPARE(request.url().toString(), QStringLiteral("https://api.example.com/register"));
    QCOMPARE(request.rawHeader("User-Agent"), QByteArray("QCurl/2.9.0"));
    QCOMPARE(request.rawHeader("Accept"), QByteArray("application/json"));
    QCOMPARE(request.rawHeader("Content-Type"), QByteArray("application/json"));
    QVERIFY(request.followLocation());
}

void TestQCNetworkRequestConfigCanonicalApi::testSendDeleteWithBody()
{
    const QByteArray body("test=data");
    const QUrl url1("http://example.com/delete-body-1");
    const QUrl url2("http://example.com/delete-body-2");
    m_mock.mockResponse(HttpMethod::Delete, url1, QByteArray("OK"));
    m_mock.mockResponse(HttpMethod::Delete, url2, QByteArray("OK"));
    m_mock.clearCapturedRequests();

    QCNetworkRequest request1(url1);
    auto *reply1 = m_manager->sendDelete(request1, body);
    QVERIFY(reply1 != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(reply1->isFinished(), 2000);

    QCNetworkRequest request2(url2);
    auto *reply2 = m_manager->sendDelete(request2, body);
    QVERIFY(reply2 != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(reply2->isFinished(), 2000);

    const auto captured = m_mock.takeCapturedRequests();
    QCOMPARE(captured.size(), 2);
    QCOMPARE(captured[0].method, HttpMethod::Delete);
    QCOMPARE(captured[0].bodyPreview, body);
    QCOMPARE(captured[1].method, HttpMethod::Delete);
    QCOMPARE(captured[1].bodyPreview, body);

    reply1->deleteLater();
    reply2->deleteLater();
}

QTEST_MAIN(TestQCNetworkRequestConfigCanonicalApi)
#include "tst_QCNetworkRequestConfigCanonicalApi.moc"
