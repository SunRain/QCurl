#include "QCBlockingNetworkClient.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "qcurl_http_script_server.h"

#include <QSignalSpy>
#include <QtTest>

#include <curl/curl.h>
#include <future>

using namespace QCurl;

namespace {
bool nativeRequest(const QUrl &url, const QByteArray &method)
{
    CURL *handle = curl_easy_init();
    if (!handle) {
        return false;
    }
    const QByteArray encoded = url.toEncoded();
    curl_slist *headers      = curl_slist_append(nullptr, "accept: second");
    curl_easy_setopt(handle, CURLOPT_URL, encoded.constData());
    curl_easy_setopt(handle, CURLOPT_PROXY, "");
    curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, method.constData());
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(
        handle, CURLOPT_WRITEFUNCTION, +[](char *, size_t size, size_t count, void *) {
            return size * count;
        });
    curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, 5000L);
    const auto code = curl_easy_perform(handle);
    curl_easy_cleanup(handle);
    curl_slist_free_all(headers);
    return code == CURLE_OK;
}
} // namespace

class tst_QCNetworkHttpSemantics : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(tst_QCNetworkHttpSemantics)

public:
    tst_QCNetworkHttpSemantics() = default;

private Q_SLOTS:
    void methodAndHeaders_data();
    void methodAndHeaders();
};

void tst_QCNetworkHttpSemantics::methodAndHeaders_data()
{
    QTest::addColumn<QByteArray>("method");
    QTest::newRow("mixed-case") << QByteArray("mIxEd");
    QTest::newRow("lowercase-standard-name") << QByteArray("get");
    QTest::newRow("extension-token") << QByteArray("X!TOKEN");
}

void tst_QCNetworkHttpSemantics::methodAndHeaders()
{
    QFETCH(QByteArray, method);
    HttpScriptServer server({HttpScriptServer::response(200, "ok")});
    QVERIFY(server.start());
    QCNetworkRequest request(server.url());
    request.setRawHeader("Accept", "first");
    request.setRawHeader("accept", "second");
    QCOMPARE(request.rawHeaderList().size(), 1);
    QCOMPARE(request.rawHeader("ACCEPT"), QByteArray("second"));
    QCNetworkAccessManager manager;
    auto *reply = manager.sendCustomRequest(request, method);
    QSignalSpy firstDone(reply, &QCNetworkReply::finished);
    QVERIFY(firstDone.wait());
    QCOMPARE(reply->error(), NetworkError::NoError);
    manager.enableRequestScheduler(true);
    reply = manager.sendCustomRequest(request, method);
    QSignalSpy secondDone(reply, &QCNetworkReply::finished);
    QVERIFY(secondDone.wait());
    QCOMPARE(reply->error(), NetworkError::NoError);
    auto blocking = std::async(std::launch::async, [request, method]() {
        return QCBlockingNetworkClient().sendCustomRequest(request, method);
    });
    QTRY_VERIFY(blocking.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);
    QCOMPARE(blocking.get().error(), NetworkError::NoError);
    auto native = std::async(std::launch::async,
                             [url = server.url(), method]() { return nativeRequest(url, method); });
    QTRY_VERIFY(native.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);
    QVERIFY(native.get());
    QCOMPARE(server.requests().size(), 4);
    for (const auto &wireRequest : server.requests()) {
        QCOMPARE(wireRequest.split(' ').first(), method);
        int headerCount = 0;
        for (const auto &line : wireRequest.split('\n')) {
            if (line.toLower().startsWith("accept:")) {
                ++headerCount;
                QCOMPARE(line.mid(line.indexOf(':') + 1).trimmed(), QByteArray("second"));
            }
        }
        QCOMPARE(headerCount, 1);
    }
}

QTEST_GUILESS_MAIN(tst_QCNetworkHttpSemantics)
#include "tst_QCNetworkHttpSemantics.moc"
