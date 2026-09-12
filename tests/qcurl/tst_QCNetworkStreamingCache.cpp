#include "QCNetworkAccessManager.h"
#include "QCNetworkCachePolicy.h"
#include "QCNetworkMemoryCache.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "qcurl_http_script_server.h"

#include <QSignalSpy>
#include <QtTest>

using namespace QCurl;

class tst_QCNetworkStreamingCache : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(tst_QCNetworkStreamingCache)

public:
    tst_QCNetworkStreamingCache() = default;

private Q_SLOTS:
    void boundedCollection_data();
    void boundedCollection();
};

void tst_QCNetworkStreamingCache::boundedCollection_data()
{
    QTest::addColumn<qint64>("bytes");
    QTest::addColumn<qint64>("capacity");
    QTest::addColumn<bool>("onlyNetwork");
    QTest::addColumn<bool>("noStore");
    QTest::addColumn<bool>("cached");
    constexpr qint64 mib = 1024 * 1024;
    QTest::newRow("no-cache-64MiB") << 64 * mib << qint64(-1) << false << false << false;
    QTest::newRow("no-cache-128MiB") << 128 * mib << qint64(-1) << false << false << false;
    QTest::newRow("only-network-64MiB") << 64 * mib << mib << true << false << false;
    QTest::newRow("only-network-128MiB") << 128 * mib << mib << true << false << false;
    QTest::newRow("within-capacity") << mib << 2 * mib << false << false << true;
    QTest::newRow("at-capacity") << mib << mib << false << false << true;
    QTest::newRow("over-capacity") << 2 * mib << mib << false << false << false;
    QTest::newRow("zero-capacity") << mib << qint64(0) << false << false << false;
    QTest::newRow("no-store") << mib << 2 * mib << false << true << false;
}

void tst_QCNetworkStreamingCache::boundedCollection()
{
    QFETCH(qint64, bytes);
    QFETCH(qint64, capacity);
    QFETCH(bool, onlyNetwork);
    QFETCH(bool, noStore);
    QFETCH(bool, cached);
    HttpScriptServer server({{"HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(bytes)
                                  + "\r\nCache-Control: max-age=300\r\nConnection: close\r\n\r\n",
                              {},
                              bytes}});
    QVERIFY(server.start());
    QCNetworkMemoryCache cache;
    cache.setMaxCacheSize(qMax<qint64>(0, capacity));
    QCNetworkAccessManager manager;
    if (capacity >= 0) {
        manager.setCache(&cache);
    }
    QCNetworkRequest request(server.url());
    request.setCachePolicy(onlyNetwork ? QCNetworkCachePolicy::OnlyNetwork
                                       : QCNetworkCachePolicy::PreferCache);
    if (noStore) {
        request.setRawHeader("Cache-Control", "no-store");
    }
    QCOMPARE(request.setBackpressureLimitBytes(65536), QCNetworkConfigUpdateResult::Applied);
    QCOMPARE(request.setBackpressureResumeBytes(32768), QCNetworkConfigUpdateResult::Applied);
    auto *reply = manager.get(request);
    QSignalSpy finished(reply, &QCNetworkReply::finished);
    qint64 consumed     = 0;
    qint64 retainedPeak = 0;
    bool bytesCorrect   = true;
    connect(reply, &QCNetworkReply::readyRead, this, [&]() {
        const auto data = reply->readAll().value_or(QByteArray());
        consumed += data.size();
        bytesCorrect = bytesCorrect && data == QByteArray(data.size(), 'x');
        retainedPeak = qMax(retainedPeak, reply->retainedCacheBodyBytesForTesting());
    });
    QVERIFY(finished.wait(30000));
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(consumed, bytes);
    QVERIFY(bytesCorrect);
    const qint64 allowed = capacity < 0 || onlyNetwork || noStore ? 0 : capacity;
    QVERIFY2(retainedPeak <= allowed, qPrintable(QString::number(retainedPeak)));
    QCOMPARE(reply->retainedCacheBodyBytesForTesting(), qint64(0));
    const auto entry = cache.lookup(QCNetworkCacheRequestKey(HttpMethod::Get, server.url()),
                                    QCNetworkCacheReadMode::FreshOnly);
    QCOMPARE(entry.hit(), cached);
    if (cached) {
        QCOMPARE(entry.body(), QByteArray(bytes, 'x'));
    }
    QVERIFY(reply->backpressureBufferedBytesPeak() <= 65536 + 16384);
    QCOMPARE(finished.size(), 1);
}

QTEST_GUILESS_MAIN(tst_QCNetworkStreamingCache)
#include "tst_QCNetworkStreamingCache.moc"
