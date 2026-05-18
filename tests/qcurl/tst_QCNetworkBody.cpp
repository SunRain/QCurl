/**
 * @file
 * @brief Tests value helpers for common request bodies.
 */

#include "QCNetworkBody.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QtTest/QtTest>

#include <optional>

using namespace QCurl;

namespace {

std::optional<QByteArray> findHeaderValue(const QList<QPair<QByteArray, QByteArray>> &headers,
                                          const QByteArray &nameLower)
{
    for (const auto &header : headers) {
        if (header.first.trimmed().toLower() == nameLower) {
            return header.second;
        }
    }
    return std::nullopt;
}

} // namespace

class TestQCNetworkBody : public QObject
{
    Q_OBJECT

private slots:
    void testJsonBodyUsesCompactEncodingAndContentType();
    void testFormBodyEncodesFieldsAndContentType();
    void testFormBodyPreservesDuplicateFieldsAndInputOrder();
    void testFormBodyHandlesEmptyFields();
    void testCopiesShareValueStateWithoutExternalViews();
    void testManagerBodyOverloadsSetContentTypeWhenMissing();
    void testManagerBodyOverloadsRespectExplicitContentType();
};

void TestQCNetworkBody::testJsonBodyUsesCompactEncodingAndContentType()
{
    QJsonObject object;
    object.insert(QStringLiteral("name"), QStringLiteral("QCurl"));
    object.insert(QStringLiteral("count"), 2);

    const QCNetworkBody body = QCNetworkBody::fromJson(object);

    QCOMPARE(body.contentType(), QByteArrayLiteral("application/json"));
    QCOMPARE(QJsonDocument::fromJson(body.data()).object(), object);
    QVERIFY(!body.data().contains('\n'));
    QVERIFY(!body.isEmpty());
}

void TestQCNetworkBody::testFormBodyEncodesFieldsAndContentType()
{
    QList<QPair<QString, QString>> fields;
    fields.append({QStringLiteral("empty"), QString()});
    fields.append({QStringLiteral("space key"), QStringLiteral("value with spaces")});
    fields.append({QStringLiteral("symbols"), QStringLiteral("a&b=c/中文")});

    const QCNetworkBody body = QCNetworkBody::fromFormUrlEncoded(fields);

    QCOMPARE(body.contentType(), QByteArrayLiteral("application/x-www-form-urlencoded"));
    QCOMPARE(
        body.data(),
        QByteArrayLiteral(
            "empty=&space%20key=value%20with%20spaces&symbols=a%26b%3Dc%2F%E4%B8%AD%E6%96%87"));
    QVERIFY(!body.isEmpty());
}

void TestQCNetworkBody::testFormBodyPreservesDuplicateFieldsAndInputOrder()
{
    const QList<QPair<QString, QString>> fields{
        {QStringLiteral("tag"), QStringLiteral("one")},
        {QStringLiteral("tag"), QStringLiteral("two")},
        {QStringLiteral("z"), QStringLiteral("last")},
        {QStringLiteral("a"), QStringLiteral("not-sorted")},
    };

    const QCNetworkBody body = QCNetworkBody::fromFormUrlEncoded(fields);

    QCOMPARE(body.data(), QByteArrayLiteral("tag=one&tag=two&z=last&a=not-sorted"));
}

void TestQCNetworkBody::testFormBodyHandlesEmptyFields()
{
    const QCNetworkBody body =
        QCNetworkBody::fromFormUrlEncoded(QList<QPair<QString, QString>>{});

    QCOMPARE(body.contentType(), QByteArrayLiteral("application/x-www-form-urlencoded"));
    QVERIFY(body.data().isEmpty());
    QVERIFY(body.isEmpty());
}

void TestQCNetworkBody::testCopiesShareValueStateWithoutExternalViews()
{
    QMap<QString, QString> fields;
    fields.insert(QStringLiteral("token"), QStringLiteral("initial"));

    const QCNetworkBody original = QCNetworkBody::fromFormUrlEncoded(fields);
    const QCNetworkBody copied(original);

    fields[QStringLiteral("token")] = QStringLiteral("mutated");

    QCOMPARE(original.data(), QByteArrayLiteral("token=initial"));
    QCOMPARE(copied.data(), original.data());
    QCOMPARE(copied.contentType(), original.contentType());
}

void TestQCNetworkBody::testManagerBodyOverloadsSetContentTypeWhenMissing()
{
    QCNetworkAccessManager manager;
    QCNetworkMockHandler mock;
    mock.setCaptureEnabled(true);
    mock.setCaptureBodyPreviewLimit(256);
    manager.setMockHandler(&mock);

    const QUrl postUrl(QStringLiteral("http://body.test/post"));
    const QUrl putUrl(QStringLiteral("http://body.test/put"));
    const QUrl patchUrl(QStringLiteral("http://body.test/patch"));
    mock.mockResponse(HttpMethod::Post, postUrl, QByteArrayLiteral("post-ok"));
    mock.mockResponse(HttpMethod::Put, putUrl, QByteArrayLiteral("put-ok"));
    mock.mockResponse(HttpMethod::Patch, patchUrl, QByteArrayLiteral("patch-ok"));

    const QCNetworkBody body = QCNetworkBody::fromFormUrlEncoded(QList<QPair<QString, QString>>{
        {QStringLiteral("tag"), QStringLiteral("one")},
        {QStringLiteral("tag"), QStringLiteral("two")},
    });

    auto *postReply = manager.sendPost(QCNetworkRequest(postUrl), body);
    auto *putReply = manager.sendPut(QCNetworkRequest(putUrl), body);
    auto *patchReply = manager.sendPatch(QCNetworkRequest(patchUrl), body);

    QTRY_VERIFY_WITH_TIMEOUT(postReply->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(putReply->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(patchReply->isFinished(), 2000);

    const auto captured = mock.takeCapturedRequests();
    QCOMPARE(captured.size(), 3);
    for (const auto &request : captured) {
        const auto contentType = findHeaderValue(request.headers(),
                                                 QByteArrayLiteral("content-type"));
        QVERIFY(contentType.has_value());
        QCOMPARE(*contentType, QByteArrayLiteral("application/x-www-form-urlencoded"));
        QCOMPARE(request.bodyPreview(), QByteArrayLiteral("tag=one&tag=two"));
    }

    postReply->deleteLater();
    putReply->deleteLater();
    patchReply->deleteLater();
}

void TestQCNetworkBody::testManagerBodyOverloadsRespectExplicitContentType()
{
    QCNetworkAccessManager manager;
    QCNetworkMockHandler mock;
    mock.setCaptureEnabled(true);
    mock.setCaptureBodyPreviewLimit(256);
    manager.setMockHandler(&mock);

    const QUrl url(QStringLiteral("http://body.test/explicit"));
    mock.mockResponse(HttpMethod::Patch, url, QByteArrayLiteral("ok"));

    QCNetworkRequest request(url);
    request.setRawHeader(QByteArrayLiteral("Content-Type"),
                         QByteArrayLiteral("application/vnd.qcurl.custom"));

    const QCNetworkBody body = QCNetworkBody::fromJson(
        QJsonObject{{QStringLiteral("name"), QStringLiteral("QCurl")}});
    auto *reply = manager.sendPatch(request, body);

    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 2000);

    const auto captured = mock.takeCapturedRequests();
    QCOMPARE(captured.size(), 1);
    const auto contentType = findHeaderValue(captured.first().headers(),
                                             QByteArrayLiteral("content-type"));
    QVERIFY(contentType.has_value());
    QCOMPARE(*contentType, QByteArrayLiteral("application/vnd.qcurl.custom"));
    QCOMPARE(captured.first().bodyPreview(), body.data());

    reply->deleteLater();
}

QTEST_MAIN(TestQCNetworkBody)

#include "tst_QCNetworkBody.moc"
