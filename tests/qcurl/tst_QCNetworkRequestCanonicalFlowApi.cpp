// SPDX-License-Identifier: MIT
// Copyright (c) 2025 QCurl Project

#include "QCNetworkAccessManager.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QUrl>
#include <QUrlQuery>
#include <QtTest>

#include <chrono>
#include <initializer_list>
#include <optional>
#include <utility>

using namespace QCurl;

/**
 * @brief QCNetworkRequest canonical flow API 语义回归测试
 *
 * 测试 QCNetworkRequest + QCNetworkAccessManager::send* 的链式配置与发送语义。
 *
 */
class TestQCNetworkRequestCanonicalFlowApi : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // 基础功能测试
    void testRequestHandoff();
    void testChainedCalls();
    void testWithHeader();
    void testWithTimeout();

    // 参数设置测试
    void testWithQueryParams();
    void testWithFollowLocation();
    void testUploadFileMissingPathFailsAsInvalidRequest();

    // 请求发送测试
    void testSendGet();
    void testSendPost();
    void testSendDeleteWithBody();

private:
    QCNetworkAccessManager *m_manager = nullptr;
    QCNetworkMockHandler m_mock;
};

void TestQCNetworkRequestCanonicalFlowApi::initTestCase()
{}

void TestQCNetworkRequestCanonicalFlowApi::cleanupTestCase()
{}

void TestQCNetworkRequestCanonicalFlowApi::init()
{
    m_manager = new QCNetworkAccessManager(this);

    m_mock.clear();
    m_mock.clearCapturedRequests();
    m_mock.setCaptureEnabled(true);
    m_mock.setGlobalDelay(0);
    m_manager->setMockHandler(&m_mock);
}

void TestQCNetworkRequestCanonicalFlowApi::cleanup()
{
    if (m_manager) {
        m_manager->deleteLater();
        m_manager = nullptr;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

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

static QUrl addQueryItems(QUrl url, std::initializer_list<std::pair<QString, QString>> items)
{
    QUrlQuery query(url);
    for (const auto &item : items) {
        query.addQueryItem(item.first, item.second);
    }
    url.setQuery(query);
    return url;
}

static QCNetworkRequest handoffRequest(QCNetworkRequest request)
{
    return request;
}

/**
 * @brief 测试临时 request 交接
 */
void TestQCNetworkRequestCanonicalFlowApi::testRequestHandoff()
{
    const QUrl url("http://example.com/request-handoff");
    m_mock.mockResponse(HttpMethod::Get, url, QByteArray("OK"));
    m_mock.clearCapturedRequests();

    QCNetworkRequest request(url);
    auto *reply = m_manager->sendGet(handoffRequest(std::move(request)));
    QVERIFY(reply != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    QCOMPARE(reply->error(), NetworkError::NoError);

    const auto captured = m_mock.takeCapturedRequests();
    QCOMPARE(captured.size(), 1);
    QCOMPARE(captured.first().url, url);
    QCOMPARE(captured.first().method, HttpMethod::Get);

    reply->deleteLater();
}

/**
 * @brief 测试 QCNetworkRequest 链式调用
 */
void TestQCNetworkRequestCanonicalFlowApi::testChainedCalls()
{
    QCNetworkRequest request(QUrl("http://example.com/api"));

    auto &result = request.setRawHeader("User-Agent", "QCurl Test")
                       .setRawHeader("Accept", "application/json")
                       .setTimeout(std::chrono::milliseconds(5000))
                       .setFollowLocation(true);

    QCOMPARE(&result, &request);
}

/**
 * @brief 测试 withHeader
 */
void TestQCNetworkRequestCanonicalFlowApi::testWithHeader()
{
    // Arrange
    QUrl url("http://example.com/with-header");
    m_mock.mockResponse(HttpMethod::Get, url, QByteArray("OK"));
    m_mock.clearCapturedRequests();

    QCNetworkRequest request(url);
    request.setRawHeader("User-Agent", "TestAgent").setRawHeader("Accept", "text/html");

    auto *reply = m_manager->sendGet(request);
    QVERIFY(reply != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);

    // Assert - 通过 MockHandler 请求捕获做离线可观测断言
    const auto captured = m_mock.takeCapturedRequests();
    QCOMPARE(captured.size(), 1);
    QCOMPARE(captured.first().url, url);
    QCOMPARE(captured.first().method, HttpMethod::Get);
    QCOMPARE(findHeaderValue(captured.first().headers, QByteArrayLiteral("user-agent")).value(),
             QByteArray("TestAgent"));
    QCOMPARE(findHeaderValue(captured.first().headers, QByteArrayLiteral("accept")).value(),
             QByteArray("text/html"));

    reply->deleteLater();
}

/**
 * @brief 测试 withTimeout
 */
void TestQCNetworkRequestCanonicalFlowApi::testWithTimeout()
{
    // Arrange
    QUrl url("http://example.com/with-timeout");
    m_mock.mockResponse(HttpMethod::Get, url, QByteArray("OK"));
    m_mock.clearCapturedRequests();

    QCNetworkRequest request(url);
    request.setTimeout(std::chrono::milliseconds(3000));

    auto *reply = m_manager->sendGet(request);
    QVERIFY(reply != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);

    const auto captured = m_mock.takeCapturedRequests();
    QCOMPARE(captured.size(), 1);
    QCOMPARE(captured.first().url, url);
    QCOMPARE(captured.first().method, HttpMethod::Get);
    QVERIFY(captured.first().totalTimeoutMs.has_value());
    QCOMPARE(*captured.first().totalTimeoutMs, qint64(3000));

    reply->deleteLater();
}

/**
 * @brief 测试 withQueryParams
 */
void TestQCNetworkRequestCanonicalFlowApi::testWithQueryParams()
{
    QUrl baseUrl("http://example.com/api");
    const QUrl expectedUrl = addQueryItems(baseUrl,
                                           {{"page", "1"}, {"limit", "20"}, {"sort", "desc"}});
    m_mock.mockResponse(HttpMethod::Get, expectedUrl, QByteArray("OK"));
    m_mock.clearCapturedRequests();

    QCNetworkRequest request(expectedUrl);
    auto *reply = m_manager->sendGet(request);
    QVERIFY(reply != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);

    const auto captured = m_mock.takeCapturedRequests();
    QCOMPARE(captured.size(), 1);
    QCOMPARE(captured.first().url, expectedUrl);
    QCOMPARE(captured.first().method, HttpMethod::Get);

    reply->deleteLater();
}

/**
 * @brief 测试 withFollowLocation
 */
void TestQCNetworkRequestCanonicalFlowApi::testWithFollowLocation()
{
    const QUrl url1("http://example.com/follow-default");
    const QUrl url2("http://example.com/follow-on");
    const QUrl url3("http://example.com/follow-off");
    m_mock.mockResponse(HttpMethod::Get, url1, QByteArray("OK"));
    m_mock.mockResponse(HttpMethod::Get, url2, QByteArray("OK"));
    m_mock.mockResponse(HttpMethod::Get, url3, QByteArray("OK"));

    // Case 1: default（未调用 withFollowLocation）
    {
        m_mock.clearCapturedRequests();
        QCNetworkRequest request(url1);
        auto *reply = m_manager->sendGet(request);
        QVERIFY(reply != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);

        const auto captured = m_mock.takeCapturedRequests();
        QCOMPARE(captured.size(), 1);
        QCOMPARE(captured.first().url, url1);
        QVERIFY(captured.first().followLocation);

        reply->deleteLater();
    }

    // Case 2: followLocation=true
    {
        m_mock.clearCapturedRequests();
        QCNetworkRequest request(url2);
        request.setFollowLocation(true);

        auto *reply = m_manager->sendGet(request);
        QVERIFY(reply != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);

        const auto captured = m_mock.takeCapturedRequests();
        QCOMPARE(captured.size(), 1);
        QCOMPARE(captured.first().url, url2);
        QVERIFY(captured.first().followLocation);

        reply->deleteLater();
    }

    // Case 3: followLocation=false（显式覆盖）
    {
        m_mock.clearCapturedRequests();
        QCNetworkRequest request(url3);
        request.setFollowLocation(false);

        auto *reply = m_manager->sendGet(request);
        QVERIFY(reply != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);

        const auto captured = m_mock.takeCapturedRequests();
        QCOMPARE(captured.size(), 1);
        QCOMPARE(captured.first().url, url3);
        QVERIFY(!captured.first().followLocation);

        reply->deleteLater();
    }
}

/**
 * @brief 测试 sendGet
 */
void TestQCNetworkRequestCanonicalFlowApi::testSendGet()
{
    // Arrange
    QUrl url("http://example.com/test");
    m_mock.mockResponse(HttpMethod::Get, url, QByteArray("OK"));
    m_mock.clearCapturedRequests();

    QCNetworkRequest request(url);
    request.setRawHeader("User-Agent", "QCurl Test").setTimeout(std::chrono::milliseconds(5000));

    auto *reply = m_manager->sendGet(request);

    // Assert
    QVERIFY(reply != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);

    const auto captured = m_mock.takeCapturedRequests();
    QCOMPARE(captured.size(), 1);
    QCOMPARE(captured.first().url, url);
    QCOMPARE(captured.first().method, HttpMethod::Get);
    QCOMPARE(findHeaderValue(captured.first().headers, QByteArrayLiteral("user-agent")).value(),
             QByteArray("QCurl Test"));

    // Cleanup
    reply->deleteLater();
}

/**
 * @brief 测试 sendPost
 */
void TestQCNetworkRequestCanonicalFlowApi::testSendPost()
{
    // Arrange
    QUrl url("http://example.com/submit");
    m_mock.mockResponse(HttpMethod::Post, url, QByteArray("OK"));
    m_mock.clearCapturedRequests();

    QByteArray postData = "test=data";
    QCNetworkRequest request(url);
    request.setRawHeader("Content-Type", "application/x-www-form-urlencoded")
        .setTimeout(std::chrono::milliseconds(5000));

    auto *reply = m_manager->sendPost(request, postData);

    // Assert
    QVERIFY(reply != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);

    const auto captured = m_mock.takeCapturedRequests();
    QCOMPARE(captured.size(), 1);
    QCOMPARE(captured.first().url, url);
    QCOMPARE(captured.first().method, HttpMethod::Post);
    QCOMPARE(captured.first().bodySize, postData.size());
    QCOMPARE(captured.first().bodyPreview, postData);
    QCOMPARE(findHeaderValue(captured.first().headers, QByteArrayLiteral("content-type")).value(),
             QByteArray("application/x-www-form-urlencoded"));

    // Cleanup
    reply->deleteLater();
}

/**
 * @brief 测试 sendDelete（携带 body）
 */
void TestQCNetworkRequestCanonicalFlowApi::testSendDeleteWithBody()
{
    // Arrange
    QUrl url1("http://example.com/delete");
    QUrl url2("http://example.com/delete2");
    m_mock.mockResponse(HttpMethod::Delete, url1, QByteArray("OK"));
    m_mock.mockResponse(HttpMethod::Delete, url2, QByteArray("OK"));
    m_mock.clearCapturedRequests();

    QByteArray body = "test=data";
    QCNetworkRequest request(url1);
    auto *reply = m_manager->sendDelete(request, body);

    // Assert
    QVERIFY(reply != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);
    reply->deleteLater();

    QCNetworkRequest request2(url2);
    auto *reply2 = m_manager->sendDelete(request2, body);
    QVERIFY(reply2 != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(reply2->isFinished(), 2000);
    reply2->deleteLater();

    const auto captured = m_mock.takeCapturedRequests();
    QCOMPARE(captured.size(), 2);
    QCOMPARE(captured.at(0).method, HttpMethod::Delete);
    QCOMPARE(captured.at(0).url, url1);
    QCOMPARE(captured.at(0).bodySize, body.size());
    QCOMPARE(captured.at(0).bodyPreview, body);
    QCOMPARE(captured.at(1).method, HttpMethod::Delete);
    QCOMPARE(captured.at(1).url, url2);
    QCOMPARE(captured.at(1).bodySize, body.size());
    QCOMPARE(captured.at(1).bodyPreview, body);
}

void TestQCNetworkRequestCanonicalFlowApi::testUploadFileMissingPathFailsAsInvalidRequest()
{
    const QString missingPath = QDir::temp().filePath(
        QStringLiteral("qcurl-missing-upload-%1.bin")
            .arg(QUuid::createUuid().toString(QUuid::Id128)));
    QVERIFY(!QFileInfo::exists(missingPath));

    auto *reply = m_manager->uploadFile(QUrl(QStringLiteral("http://example.com/upload")),
                                        missingPath);
    QVERIFY(reply != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);

    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QVERIFY(reply->errorString().contains(QStringLiteral("uploadFile")));
    QCOMPARE(m_mock.takeCapturedRequests().size(), 0);

    reply->deleteLater();
}

QTEST_MAIN(TestQCNetworkRequestCanonicalFlowApi)
#include "tst_QCNetworkRequestCanonicalFlowApi.moc"
