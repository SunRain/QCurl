#include "QCBlockingNetworkClient.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkCachePolicy.h"
#include "QCNetworkCacheRequestKey.h"
#include "QCNetworkMemoryCache.h"
#include "QCNetworkMiddleware.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkReply.h"
#include "QCNetworkTimeoutConfig.h"
#include "qcnetwork_mock_test_support.h"
#include "qcnetwork_receive_idle_server.h"
#include "qcnetwork_receive_idle_test.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QtTest>

#include <ctime>
#include <limits>

using namespace QCurl;
using namespace std::chrono_literals;

namespace {

/// 观察实际 Qt timer 投递；不将 CPU 时间或上下文切换伪称 timer 唤醒。
class ReplyTimerObservation final : public QObject
{
public:
    ReplyTimerObservation() { qApp->installEventFilter(this); }
    ~ReplyTimerObservation() override { qApp->removeEventFilter(this); }
    int wakeups = 0;

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::Timer && qobject_cast<QTimer *>(object)
            && qobject_cast<QCNetworkReply *>(object->parent())) {
            ++wakeups;
        }
        return false;
    }

private:
    Q_DISABLE_COPY_MOVE(ReplyTimerObservation)
};

QCBlockingNetworkClient::Options blockingOptions()
{
    QCBlockingNetworkClient::Options options;
    options.setApplicationThreadPolicy(
        QCBlockingNetworkClient::ApplicationThreadPolicy::AllowForCliOrTests);
    return options;
}

class IdleMiddleware final : public QCNetworkMiddleware
{
public:
    explicit IdleMiddleware(qint64 duration)
        : m_duration(duration)
    {}
    void onRequestPreSend(QCNetworkRequest &request) override
    {
        auto config = request.timeoutConfig();
        config.setReceiveIdleTimeout(std::chrono::milliseconds(m_duration));
        request.setTimeoutConfig(config);
    }

private:
    qint64 m_duration;
};

void populateCache(QCNetworkMemoryCache &cache, const QUrl &url)
{
    QCNetworkCacheMetadata metadata;
    metadata.setUrl(url);
    metadata.setStatusCode(200);
    metadata.setExpirationDate(QDateTime::currentDateTime().addSecs(60));
    metadata.setSize(6);
    cache.insert(QCNetworkCacheRequestKey(HttpMethod::Get, url),
                 QByteArrayLiteral("cached"),
                 metadata);
}

} // namespace

void TestQCNetworkReceiveIdleTimeout::disabledAndMaximum_data()
{
    QTest::addColumn<qint64>("duration");
    QTest::newRow("unset") << qint64(-1);
    QTest::newRow("zero") << qint64(0);
    QTest::newRow("maximum") << qint64(std::numeric_limits<int>::max());
}

void TestQCNetworkReceiveIdleTimeout::disabledAndMaximum()
{
    QFETCH(qint64, duration);
    IdleResponse response;
    response.headerDelayMs = 120;
    IdleServer server({response});
    QVERIFY(server.started());
    QCNetworkRequest request(server.url());
    QCNetworkTimeoutConfig config;
    if (duration >= 0) {
        config.setReceiveIdleTimeout(std::chrono::milliseconds(duration));
    }
    request.setTimeoutConfig(config);
    QCNetworkAccessManager manager;
    auto *reply = manager.get(request);
    QSignalSpy done(reply, &QCNetworkReply::finished);
    QVERIFY(done.wait(3000));
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->readAll().value(), QByteArrayLiteral("ok"));
    QVERIFY(!reply->isReceiveIdleTimeout());
    QCBlockingNetworkClient client(blockingOptions());
    const auto result = client.get(request);
    QCOMPARE(result.error(),
             duration > 0 ? NetworkError::UnsupportedCapability : NetworkError::NoError);
    QCOMPARE(server.requests(), duration > 0 ? 1 : 2);
}

void TestQCNetworkReceiveIdleTimeout::firstError_data()
{
    QTest::addColumn<int>("first");
    QTest::addColumn<QString>("category");
    QTest::newRow("connect-before-total-idle") << 0 << QStringLiteral("connect timeout");
    QTest::newRow("total-before-idle") << 1 << QStringLiteral("total timeout");
    QTest::newRow("idle") << 2 << QStringLiteral("receive idle timeout");
}

void TestQCNetworkReceiveIdleTimeout::firstError()
{
    QFETCH(int, first);
    QFETCH(QString, category);
    QCNetworkRequest request(QUrl(QStringLiteral("http://mock.invalid/invalid-timeout")));
    QCNetworkTimeoutConfig config;
    config.setConnectTimeout(first == 0 ? -1ms : 1s);
    config.setTotalTimeout(first <= 1 ? -1ms : 1s);
    config.setReceiveIdleTimeout(-1ms);
    request.setTimeoutConfig(config);
    QCNetworkAccessManager manager;
    QCNetworkMockHandler mock;
    mock.mockResponse(HttpMethod::Get, request.url(), QByteArrayLiteral("ok"));
    TestSupport::setMockHandler(manager, &mock);
    auto *reply = manager.get(request);
    QSignalSpy done(reply, &QCNetworkReply::finished);
    QVERIFY(done.wait(2000));
    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QVERIFY2(reply->errorString().contains(category), qPrintable(reply->errorString()));
    QCOMPARE(reply->diagnosticCurlCode(), 0);
    QCBlockingNetworkClient client(blockingOptions());
    const auto result = client.get(request);
    QCOMPARE(result.error(), NetworkError::InvalidRequest);
    QVERIFY2(result.errorMessage().contains(category), qPrintable(result.errorMessage()));
}

void TestQCNetworkReceiveIdleTimeout::preparedSnapshot_data()
{
    QTest::addColumn<bool>("cached");
    QTest::addColumn<qint64>("duration");
    for (bool cached : {false, true}) {
        for (qint64 duration : {-1, 0, 500}) {
            const auto row = QByteArray(cached ? "cache-" : "mock-") + QByteArray::number(duration);
            QTest::newRow(row.constData()) << cached << duration;
        }
    }
}

void TestQCNetworkReceiveIdleTimeout::preparedSnapshot()
{
    QFETCH(bool, cached);
    QFETCH(qint64, duration);
    const QUrl url(QStringLiteral("http://mock.invalid/prepared-idle"));
    QCNetworkAccessManager manager;
    QCNetworkMemoryCache cache;
    populateCache(cache, url);
    manager.setCache(&cache);
    const auto clearCache = qScopeGuard([&] { manager.setCache(nullptr); });
    QCNetworkMockHandler mock;
    mock.mockResponse(HttpMethod::Get, url, QByteArrayLiteral("mock"));
    mock.setCaptureEnabled(true);
    mock.setGlobalDelay(120);
    TestSupport::setMockHandler(manager, &mock);
    IdleMiddleware middleware(duration);
    manager.addMiddleware(&middleware);
    const auto clearMiddleware = qScopeGuard([&] { manager.clearMiddlewares(); });
    QCNetworkRequest request(url);
    request.setCachePolicy(cached ? QCNetworkCachePolicy::AlwaysCache
                                  : QCNetworkCachePolicy::OnlyNetwork);
    auto *reply = manager.get(request);
    QSignalSpy done(reply, &QCNetworkReply::finished);
    QVERIFY(done.wait(2000));
    QCOMPARE(done.size(), 1);
    QCOMPARE(reply->error(), duration < 0 ? NetworkError::InvalidRequest : NetworkError::NoError);
    QVERIFY(!reply->isReceiveIdleTimeout());
    QCOMPARE(reply->diagnosticCurlCode(), 0);
    if (duration >= 0) {
        QCOMPARE(reply->readAll().value(),
                 cached ? QByteArrayLiteral("cached") : QByteArrayLiteral("mock"));
        if (!cached) {
            QCOMPARE(mock.capturedRequests().size(), 1);
            QCOMPARE(mock.capturedRequests().first().receiveIdleTimeoutMs(),
                     std::optional<qint64>(duration));
        }
    } else {
        QVERIFY(mock.capturedRequests().isEmpty());
    }
}

void TestQCNetworkReceiveIdleTimeout::cacheFallbackClearsSource()
{
    IdleResponse response;
    response.headerDelayMs = 800;
    IdleServer server({response});
    QVERIFY(server.started());
    QCNetworkAccessManager manager;
    QCNetworkMemoryCache cache;
    populateCache(cache, server.url());
    manager.setCache(&cache);
    const auto clearCache = qScopeGuard([&] { manager.setCache(nullptr); });
    QCNetworkRequest request(server.url());
    request.setCachePolicy(QCNetworkCachePolicy::PreferNetwork);
    QCNetworkTimeoutConfig timeout;
    timeout.setReceiveIdleTimeout(100ms);
    request.setTimeoutConfig(timeout);
    auto *reply = manager.get(request);
    QSignalSpy done(reply, &QCNetworkReply::finished);
    QVERIFY(done.wait(2000));
    QCOMPARE(server.requests(), 1);
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->readAll().value(), QByteArrayLiteral("cached"));
    QVERIFY(!reply->isReceiveIdleTimeout());
    QCOMPARE(reply->diagnosticCurlCode(), 0);
    QTest::qWait(150);
    QCOMPARE(done.size(), 1);
    QVERIFY(!reply->isReceiveIdleTimeout());
}

void TestQCNetworkReceiveIdleTimeout::receiveIdleCost_data()
{
    QTest::addColumn<bool>("enabled");
    QTest::addColumn<int>("concurrency");
    for (const int concurrency : {4, 32, 128}) {
        for (const bool enabled : {false, true}) {
            const auto row = QByteArray(enabled ? "enabled-" : "disabled-")
                             + QByteArray::number(concurrency);
            QTest::newRow(row.constData()) << enabled << concurrency;
        }
    }
}

void TestQCNetworkReceiveIdleTimeout::receiveIdleCost()
{
    QFETCH(bool, enabled);
    QFETCH(int, concurrency);
    IdleResponse response;
    response.headerDelayMs = 50;
    response.chunkDelayMs  = 25;
    response.chunks        = {"a", "b", "c", "d"};
    IdleServer server({response});
    QVERIFY(server.started());
    QCNetworkAccessManager manager;
    ReplyTimerObservation timers;
    QElapsedTimer wall;
    wall.start();
    const auto cpu = std::clock();
    int completed  = 0;
    int failed     = 0;
    for (int batch = 0; batch < 4; ++batch) {
        for (int index = 0; index < concurrency; ++index) {
            QCNetworkRequest request(server.url());
            QCNetworkTimeoutConfig timeout;
            if (enabled) {
                timeout.setReceiveIdleTimeout(5s);
            }
            request.setTimeoutConfig(timeout);
            auto *reply = manager.get(request);
            connect(reply, &QCNetworkReply::finished, this, [&, reply] {
                failed += reply->error() != NetworkError::NoError
                          || reply->readAll().value_or(QByteArray{}) != QByteArrayLiteral("abcd");
                ++completed;
                reply->deleteLater();
            });
        }
        QTRY_COMPARE_WITH_TIMEOUT(completed, (batch + 1) * concurrency, 15000);
    }
    QCOMPARE(failed, 0);
    QCOMPARE(server.requests(), 4 * concurrency);
    qInfo().nospace() << "receive_idle_cost enabled=" << enabled << " concurrency=" << concurrency
                      << " requests=" << completed << " wall_ms=" << wall.elapsed()
                      << " cpu_ms=" << 1000.0 * (std::clock() - cpu) / CLOCKS_PER_SEC
                      << " reply_timer_wakeups=" << timers.wakeups;
}
