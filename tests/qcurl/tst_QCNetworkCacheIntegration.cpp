/**
 * @file tst_QCNetworkCacheIntegration.cpp
 * @brief 覆盖缓存策略、命中路径和边界条件的集成语义。
 *
 * 该套件验证自动缓存读写、五类策略、信号顺序和过期/no-cache 等边界。
 */

#include "QCCookie.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkCache.h"
#include "QCNetworkCachePolicy.h"
#include "QCNetworkCacheRequestKey.h"
#include "QCNetworkDiskCache.h"
#include "QCNetworkError.h"
#include "QCNetworkMemoryCache.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "test_httpbin_env.h"

#include <QCoreApplication>
#include <QEvent>
#include <QEventLoop>
#include <QHash>
#include <QHostAddress>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QVector>
#include <QtTest/QtTest>

#include <limits>

using namespace QCurl;

namespace {

QCNetworkCacheRequestKey requestKeyFor(const QCNetworkRequest &request,
                                       HttpMethod method = HttpMethod::Get)
{
    QCNetworkCacheRequestKey key(method, request.url());
    for (const QByteArray &name : request.rawHeaderList()) {
        key.setRequestHeader(name, request.rawHeader(name));
    }
    key.setCachePartitionKey(request.cachePartitionKey());
    return key;
}

QCNetworkCacheRequestKey requestKeyFor(const QUrl &url)
{
    return QCNetworkCacheRequestKey(HttpMethod::Get, url);
}

class CacheContractServer final : public QObject
{
    Q_OBJECT

public:
    explicit CacheContractServer(QObject *parent = nullptr)
        : QObject(parent)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (m_server.hasPendingConnections()) {
                QTcpSocket *socket = m_server.nextPendingConnection();
                if (!socket) {
                    continue;
                }
                socket->setParent(this);
                m_buffers.insert(socket, {});
                const QPointer<QTcpSocket> safeSocket(socket);
                connect(socket, &QTcpSocket::readyRead, this, [this, safeSocket]() {
                    if (safeSocket) {
                        readRequest(safeSocket.data());
                    }
                });
                connect(socket, &QTcpSocket::disconnected, this, [this, safeSocket]() {
                    if (safeSocket) {
                        m_buffers.remove(safeSocket.data());
                        safeSocket->deleteLater();
                    }
                });
            }
        });
    }

    bool start() { return m_server.listen(QHostAddress::LocalHost, 0); }

    QUrl url(const QString &path) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_server.serverPort()).arg(path));
    }

    int requestCount() const { return m_requests.size(); }

private:
    struct Request
    {
        QByteArray method;
        QByteArray path;
        QMap<QByteArray, QByteArray> headers;
    };

    static QByteArray routeOf(QByteArray target)
    {
        const int queryStart = target.indexOf('?');
        if (queryStart >= 0) {
            target.truncate(queryStart);
        }
        return target;
    }

    void readRequest(QTcpSocket *socket)
    {
        QByteArray &buffer = m_buffers[socket];
        buffer.append(socket->readAll());

        const int headerEnd = buffer.indexOf(QByteArrayLiteral("\r\n\r\n"));
        if (headerEnd < 0) {
            return;
        }

        const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
        if (lines.isEmpty()) {
            return;
        }

        const QList<QByteArray> requestLine = lines.constFirst().trimmed().split(' ');
        if (requestLine.size() < 2) {
            return;
        }

        Request request;
        request.method       = requestLine.at(0);
        request.path         = requestLine.at(1);
        qint64 contentLength = 0;
        for (int i = 1; i < lines.size(); ++i) {
            const QByteArray line = lines.at(i).trimmed();
            const int separator   = line.indexOf(':');
            if (separator <= 0) {
                continue;
            }
            const QByteArray name  = line.left(separator).trimmed().toLower();
            const QByteArray value = line.mid(separator + 1).trimmed();
            request.headers.insert(name, value);
            if (name == QByteArrayLiteral("content-length")) {
                bool ok             = false;
                const qint64 parsed = value.toLongLong(&ok);
                if (ok && parsed >= 0) {
                    contentLength = parsed;
                }
            }
        }

        const qint64 requestSize = static_cast<qint64>(headerEnd) + 4 + contentLength;
        if (buffer.size() < requestSize) {
            return;
        }
        buffer.remove(0, static_cast<qsizetype>(requestSize));
        m_requests.append(request);
        sendResponse(socket, request);
    }

    static QByteArray statusText(int status)
    {
        switch (status) {
            case 200:
                return QByteArrayLiteral("OK");
            case 201:
                return QByteArrayLiteral("Created");
            case 302:
                return QByteArrayLiteral("Found");
            case 304:
                return QByteArrayLiteral("Not Modified");
            default:
                return QByteArrayLiteral("Response");
        }
    }

    static QByteArray httpDateSecsAgo(qint64 seconds)
    {
        return QDateTime::currentDateTimeUtc().addSecs(-seconds).toString(Qt::RFC2822Date).toLatin1();
    }

    void sendResponse(QTcpSocket *socket, const Request &request)
    {
        const QByteArray route = routeOf(request.path);
        if (route == QByteArrayLiteral("/prefer-network-fallback") && m_requests.size() >= 2) {
            socket->disconnectFromHost();
            return;
        }
        if (route == QByteArrayLiteral("/prefer-network-partial") && m_requests.size() >= 2) {
            socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Length: 20\r\n"
                                            "Connection: close\r\n\r\npartial"));
            socket->disconnectFromHost();
            return;
        }
        if (route == QByteArrayLiteral("/redirect-auto-referer")) {
            socket->write(QByteArrayLiteral("HTTP/1.1 302 Found\r\n"
                                            "Location: /vary-referer\r\n"
                                            "Content-Length: 0\r\n"
                                            "Connection: close\r\n\r\n"));
            socket->disconnectFromHost();
            return;
        }
        const QByteArray etag        = route == QByteArrayLiteral("/revalidate")
                                           ? QByteArrayLiteral("\"revalidate-v1\"")
                                       : route == QByteArrayLiteral("/revalidate-raw")
                                           ? QByteArrayLiteral("\"revalidate-raw-v1\"")
                                       : route == QByteArrayLiteral("/nocache")
                                           ? QByteArrayLiteral("\"nocache-v1\"")
                                       : route == QByteArrayLiteral("/head")
                                           ? QByteArrayLiteral("\"head-v1\"")
                                       : route == QByteArrayLiteral("/head-mismatch")
                                           ? (request.method == QByteArrayLiteral("HEAD")
                                                  ? QByteArrayLiteral("\"head-v2\"")
                                                  : QByteArrayLiteral("\"head-v1\""))
                                           : QByteArray();
        const QByteArray ifNoneMatch = request.headers.value("if-none-match");
        const bool notModified       = !etag.isEmpty() && ifNoneMatch == etag
                                       && (route == QByteArrayLiteral("/revalidate")
                                           || route == QByteArrayLiteral("/revalidate-raw")
                                           || route == QByteArrayLiteral("/nocache")
                                           || route == QByteArrayLiteral("/head"));

        int status = notModified ? 304 : 200;
        QByteArray body;
        QByteArray cacheControl = QByteArrayLiteral("max-age=3600");
        QByteArray dateHeader;
        QByteArray ageHeader;
        QByteArray vary;
        if (route == QByteArrayLiteral("/vary")) {
            body = QByteArrayLiteral("vary:") + request.headers.value("accept-language");
            vary = QByteArrayLiteral("Accept-Language");
        } else if (route == QByteArrayLiteral("/vary-referer")) {
            body = QByteArrayLiteral("referer:") + request.headers.value("referer");
            vary = QByteArrayLiteral("Referer");
        } else if (route == QByteArrayLiteral("/vary-encoding")) {
            body = QByteArrayLiteral("encoding:") + request.headers.value("accept-encoding");
            vary = QByteArrayLiteral("Accept-Encoding");
        } else if (route == QByteArrayLiteral("/vary-cookie")) {
            body = QByteArrayLiteral("cookie:") + request.headers.value("cookie");
            vary = QByteArrayLiteral("Cookie");
        } else if (route == QByteArrayLiteral("/auth")) {
            body = QByteArrayLiteral("auth:") + request.headers.value("authorization");
        } else if (route == QByteArrayLiteral("/created")) {
            status = 201;
            body   = QByteArrayLiteral("created");
        } else if (route == QByteArrayLiteral("/post")) {
            body = QByteArrayLiteral("post");
        } else if (route == QByteArrayLiteral("/head-only")) {
            body = QByteArrayLiteral("head-only");
        } else if (route == QByteArrayLiteral("/head")) {
            cacheControl = QByteArrayLiteral("max-age=0");
            body         = QByteArrayLiteral("head-body");
        } else if (route == QByteArrayLiteral("/head-mismatch")) {
            body = request.method == QByteArrayLiteral("HEAD")
                       ? QByteArrayLiteral("new-longer-head-body")
                       : QByteArrayLiteral("old-body");
        } else if (route == QByteArrayLiteral("/age-stale")) {
            cacheControl = QByteArrayLiteral("max-age=60");
            dateHeader   = httpDateSecsAgo(120);
            ageHeader    = QByteArrayLiteral("120");
            body         = QByteArrayLiteral("age-stale:") + QByteArray::number(m_requests.size());
        } else if (route == QByteArrayLiteral("/age-hit")) {
            cacheControl = QByteArrayLiteral("max-age=3600");
            dateHeader   = httpDateSecsAgo(30);
            ageHeader    = QByteArrayLiteral("5");
            body         = QByteArrayLiteral("age-hit");
        } else if (route == QByteArrayLiteral("/age-int64-max")) {
            cacheControl = QByteArrayLiteral("max-age=60");
            ageHeader    = QByteArray::number(std::numeric_limits<qint64>::max());
            body = QByteArrayLiteral("age-int64-max:") + QByteArray::number(m_requests.size());
        } else if (route == QByteArrayLiteral("/prefer-network-fallback")) {
            body = QByteArrayLiteral("cached-fallback");
        } else if (route == QByteArrayLiteral("/prefer-network-partial")) {
            body = QByteArrayLiteral("cached-body");
        } else if (route == QByteArrayLiteral("/revalidate")) {
            cacheControl = QByteArrayLiteral("max-age=0");
            body         = QByteArrayLiteral("revalidated-body");
        } else if (route == QByteArrayLiteral("/revalidate-raw")) {
            cacheControl = QByteArrayLiteral("max-age=0");
            body         = QByteArrayLiteral("revalidated-raw-body");
        } else if (route == QByteArrayLiteral("/nocache")) {
            cacheControl = QByteArrayLiteral("no-cache");
            body         = QByteArrayLiteral("nocache-body");
        } else if (route == QByteArrayLiteral("/multi-cache-control")) {
            cacheControl = QByteArrayLiteral("no-store");
            body         = QByteArrayLiteral("not-stored");
        } else if (route == QByteArrayLiteral("/duplicate-date-age")) {
            dateHeader = httpDateSecsAgo(10);
            ageHeader  = QByteArrayLiteral("10");
            body       = QByteArrayLiteral("conflicting-single-values");
        } else {
            body = QByteArrayLiteral("default");
        }

        const QByteArray responseBody = request.method == QByteArrayLiteral("HEAD") ? QByteArray()
                                        : notModified                               ? QByteArray()
                                                                                    : body;
        QByteArray response = QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(status) + ' '
                              + statusText(status) + QByteArrayLiteral("\r\n");
        if (!notModified) {
            response += QByteArrayLiteral("Cache-Control: ") + cacheControl
                        + QByteArrayLiteral("\r\n");
        }
        if (!etag.isEmpty()) {
            response += QByteArrayLiteral("ETag: ") + etag + QByteArrayLiteral("\r\n");
        }
        if (!dateHeader.isEmpty()) {
            response += QByteArrayLiteral("Date: ") + dateHeader + QByteArrayLiteral("\r\n");
        }
        if (!ageHeader.isEmpty()) {
            response += QByteArrayLiteral("Age: ") + ageHeader + QByteArrayLiteral("\r\n");
        }
        if (route == QByteArrayLiteral("/multi-cache-control")) {
            response += QByteArrayLiteral("cAcHe-CoNtRoL: public, max-age=3600\r\n");
        }
        if (route == QByteArrayLiteral("/duplicate-date-age")) {
            response += QByteArrayLiteral("dAtE: ") + httpDateSecsAgo(20)
                        + QByteArrayLiteral("\r\naGe: 20\r\n");
        }
        if (!vary.isEmpty()) {
            response += QByteArrayLiteral("Vary: ") + vary + QByteArrayLiteral("\r\n");
        }
        if (route == QByteArrayLiteral("/raw-headers")) {
            response += QByteArrayLiteral("X-Dupe: first\r\nx-dupe: second\r\n");
        }
        if (route == QByteArrayLiteral("/revalidate-raw")) {
            if (notModified) {
                response += QByteArrayLiteral("X-Replace: new-first\r\n"
                                              "x-replace: new-second\r\n"
                                              "X-New: appended\r\n");
            } else {
                response += QByteArrayLiteral("X-Stable: keep\r\n"
                                              "X-Replace: old-first\r\n"
                                              "x-replace: old-second\r\n");
            }
        }
        if (route == QByteArrayLiteral("/sensitive-headers")) {
            response += QByteArrayLiteral("Set-Cookie: session=response-secret; HttpOnly\r\n");
        }
        response += QByteArrayLiteral("Content-Length: ") + QByteArray::number(body.size())
                    + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + responseBody;
        socket->write(response);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    QList<Request> m_requests;
};

} // namespace

class TestQCNetworkCacheIntegration : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // 基础缓存语义
    void testNoCacheBehavior();   // 无缓存时行为不变
    void testOnlyNetworkPolicy(); // OnlyNetwork 策略不缓存
    void testSignalOrder();       // 信号发射顺序正确
    void testDataConsistency();   // 数据完整性
    void testMultipleRequests();  // 多次请求一致性

    // 策略命中与写入
    void testPreferCacheHit();  // PreferCache 命中
    void testPreferCacheMiss(); // PreferCache 未命中
    void testOnlyCacheHit();    // OnlyCache 命中
    void testOnlyCacheMiss();   // OnlyCache 未命中（错误）
    void testOnlyCacheHitWithZeroByteBody();
    void testOnlyCacheHitWithZeroByteBodyAndNoHeaders();
    void testOnlyCacheHitWithExpiredBody();
    void testOnlyCacheHitWithExpiredZeroByteBody();
    void testPreferCacheHitWithZeroByteBody();
    void testAlwaysCache(); // AlwaysCache 策略
    void testAlwaysCacheHitWithExpiredBody();
    void testAlwaysCacheHitWithExpiredZeroByteBody();
    void testPreferNetworkSuccess();                          // PreferNetwork 成功
    void testAutoCacheWrite();                                // 自动缓存写入验证
    void testStaleEntryRevalidatedWith304();                  // stale entry 条件重验证
    void testVaryVariantsDoNotShare();                        // Vary 变体隔离
    void testAuthenticatedPartitionsDoNotShare();             // 认证分区隔离
    void testUnpartitionedAuthenticationIsNotStored();        // 未分区认证不存储
    void testNonGetAndNon200ResponsesAreNotStored();          // method/status 资格
    void testHeadDoesNotCreateBodyEntryAndUpdatesGet();       // HEAD 元数据语义
    void testHeadMismatchDoesNotRefreshCachedMetadata();      // HEAD 不得组合旧 body 与新 validator
    void testDateAgeCanMakeMaxAgeResponseStale();             // freshness 使用 RFC 9111 current-age
    void testAgeInt64MaxIsStaleWithoutOverflow();             // Age 饱和边界
    void testCacheHitUpdatesObservableAgeHeader();            // 命中更新可观测 Age
    void testPreferNetworkFallsBackBeforeResponseIsVisible(); // 前置网络失败回退缓存
    void testPreferNetworkDoesNotFallbackAfterPartialBody();  // partial body 后禁止混入缓存
    void testNoCacheResponseIsRevalidated();                  // no-cache 条件重验证
    void testRequestNoCacheBypassesFreshHit();                // 请求 no-cache 强制访问网络
    void testRequestNoStorePreventsResponseStorage();         // 请求 no-store 禁止写入
    void testStreamingReadDoesNotTruncateCachedBody();        // 应用 drain 与缓存 body 解耦
    void testCachedResponsePreservesOrderedRawHeaders();      // raw header 重复项与顺序
    void testSensitiveResponseIsNotStored();                  // 敏感响应整体不可存储
    void testEarlierNoStoreLinePreventsStorage();             // 多行 Cache-Control 全量生效
    void testDuplicateDateAgePreventsStorage();               // 冲突单值字段拒绝存储
    void testVaryUsesConfiguredReferer();                     // Vary 使用实际 CURLOPT_REFERER
    void testVaryUsesConfiguredAcceptEncoding();              // Vary 使用 CURLOPT_ACCEPT_ENCODING
    void testUnknownAutoAcceptEncodingVaryIsNotStored();      // 无法重现的 Vary 维度 fail closed
    void testCookieEngineVaryIsNotStored();                   // cookie engine 维度 fail closed
    void testAutoRefererVaryIsNotStored();                    // redirect 自动 Referer fail closed
    void testRevalidationPreservesOrderedRawHeaders();        // 304 合并保留重复项与顺序

    // 边界条件
    void testCacheExpiration();    // 缓存过期处理
    void testNoCacheHeader();      // no-cache 头部
    void testConcurrentRequests(); // 并发请求

private:
    QCNetworkAccessManager *m_manager = nullptr;
    QCNetworkMemoryCache *m_cache     = nullptr;
    QString m_httpbinBaseUrl;

    bool waitForFinished(QCNetworkReply *reply, int timeout = 5000);
    bool isHttpbinAvailable();
    void verifyCacheHitSignalContract(QCNetworkCachePolicy policy,
                                      const QUrl &url,
                                      const QByteArray &cachedData,
                                      bool expectReadyRead,
                                      int expiresInSeconds = 3600);
};

void TestQCNetworkCacheIntegration::initTestCase()
{
    m_httpbinBaseUrl = TestEnv::httpbinBaseUrl();
    QVERIFY2(!m_httpbinBaseUrl.isEmpty(), qPrintable(TestEnv::httpbinMissingReason()));

    // 该套件依赖可访问的本地 httpbin。
    QVERIFY2(isHttpbinAvailable(), qPrintable(TestEnv::httpbinUnavailableReason(m_httpbinBaseUrl)));
}

void TestQCNetworkCacheIntegration::cleanupTestCase() {}

void TestQCNetworkCacheIntegration::init()
{
    // 每个用例使用独立 manager/cache，避免缓存键互相污染。
    m_manager = new QCNetworkAccessManager(this);
    m_cache   = new QCNetworkMemoryCache(m_manager);
    m_cache->setMaxCacheSize(1024 * 1024); // 1 MiB 即可覆盖当前测试数据量。
}

void TestQCNetworkCacheIntegration::cleanup()
{
    if (m_manager) {
        m_manager->setCache(nullptr);
        m_manager->deleteLater();
        m_manager = nullptr;
        m_cache   = nullptr;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

bool TestQCNetworkCacheIntegration::waitForFinished(QCNetworkReply *reply, int timeout)
{
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    connect(reply, &QCNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    timer.start(timeout);
    loop.exec();

    return reply->isFinished();
}

bool TestQCNetworkCacheIntegration::isHttpbinAvailable()
{
    QCNetworkAccessManager testManager;
    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/get"));

    auto *reply    = testManager.get(request);
    bool available = waitForFinished(reply, 2000) && reply->error() == NetworkError::NoError;
    reply->deleteLater();

    return available;
}

void TestQCNetworkCacheIntegration::verifyCacheHitSignalContract(QCNetworkCachePolicy policy,
                                                                 const QUrl &url,
                                                                 const QByteArray &cachedData,
                                                                 bool expectReadyRead,
                                                                 int expiresInSeconds)
{
    m_manager->setCache(m_cache);

    QCNetworkCacheMetadata meta;
    meta.setUrl(url);
    meta.setExpirationDate(QDateTime::currentDateTime().addSecs(expiresInSeconds));
    meta.setHeader(QByteArrayLiteral("Content-Type"), QByteArrayLiteral("text/plain"));
    meta.setSize(cachedData.size());
    m_cache->insert(requestKeyFor(url), cachedData, meta);

    QCNetworkRequest request(url);
    request.setCachePolicy(policy);

    auto *reply = m_manager->get(request);
    QVERIFY(reply);
    QVERIFY2(!reply->isFinished(), "cache hit must finish asynchronously after get returns");

    QVector<QByteArray> events;
    QSignalSpy readySpy(reply, &QCNetworkReply::readyRead);
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
    connect(reply, &QCNetworkReply::readyRead, this, [&events]() {
        events.append(QByteArrayLiteral("readyRead"));
    });
    connect(reply, &QCNetworkReply::finished, this, [&events]() {
        events.append(QByteArrayLiteral("finished"));
    });

    QVERIFY(waitForFinished(reply, 100));
    QCOMPARE(finishedSpy.count(), 1);

    if (expectReadyRead) {
        QCOMPARE(readySpy.count(), 1);
        const qsizetype readyIndex    = events.indexOf(QByteArrayLiteral("readyRead"));
        const qsizetype finishedIndex = events.indexOf(QByteArrayLiteral("finished"));
        QVERIFY(readyIndex >= 0);
        QVERIFY(finishedIndex >= 0);
        QVERIFY(readyIndex < finishedIndex);
    } else {
        QCOMPARE(readySpy.count(), 0);
        QCOMPARE(events, QVector<QByteArray>{QByteArrayLiteral("finished")});
    }

    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->httpStatusCode(), 200);
    const auto data = reply->readAll();
    QVERIFY(data.has_value());
    QCOMPARE(data.value(), cachedData);

    reply->deleteLater();
}

// ============================================================================
// 基础缓存语义
// ============================================================================

void TestQCNetworkCacheIntegration::testNoCacheBehavior()
{
    // 无缓存时，请求应保持普通网络路径语义。
    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/get"));

    // 不设置缓存
    auto *reply = m_manager->get(request);

    QVERIFY(waitForFinished(reply));
    QCOMPARE(reply->error(), NetworkError::NoError);

    auto data = reply->readAll();
    QVERIFY(data.has_value());
    QVERIFY(!data->isEmpty());

    reply->deleteLater();
}

void TestQCNetworkCacheIntegration::testOnlyNetworkPolicy()
{
    // OnlyNetwork 只走网络路径，不应留下缓存副本。
    m_manager->setCache(m_cache);

    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/get?test=onlynetwork"));
    request.setCachePolicy(QCNetworkCachePolicy::OnlyNetwork);

    auto *reply = m_manager->get(request);
    QVERIFY(waitForFinished(reply));
    QCOMPARE(reply->error(), NetworkError::NoError);

    const auto cached = m_cache->lookup(requestKeyFor(request), QCNetworkCacheReadMode::FreshOnly);
    QCOMPARE(cached.status(), QCNetworkCacheLookupStatus::Miss);

    reply->deleteLater();
}

void TestQCNetworkCacheIntegration::testSignalOrder()
{
    // 命中网络路径时，readyRead 必须先于 finished。
    m_manager->setCache(m_cache);

    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/get"));
    request.setCachePolicy(QCNetworkCachePolicy::PreferCache);

    auto *reply = m_manager->get(request);

    QSignalSpy readySpy(reply, &QCNetworkReply::readyRead);
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);

    QVERIFY(waitForFinished(reply));

    QVERIFY(readySpy.count() >= 1);
    QCOMPARE(finishedSpy.count(), 1);

    reply->deleteLater();
}

void TestQCNetworkCacheIntegration::testDataConsistency()
{
    // 同一 URL 的缓存命中必须返回与首次网络读取一致的数据。
    m_manager->setCache(m_cache);

    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/get?test=consistency"));
    request.setCachePolicy(QCNetworkCachePolicy::AlwaysCache);

    auto *reply1 = m_manager->get(request);
    QVERIFY(waitForFinished(reply1));
    auto data1 = reply1->readAll();
    QVERIFY(data1.has_value());
    reply1->deleteLater();

    auto *reply2 = m_manager->get(request);
    QVERIFY(waitForFinished(reply2, 100)); // 缓存命中应该很快
    auto data2 = reply2->readAll();
    QVERIFY(data2.has_value());
    reply2->deleteLater();

    QCOMPARE(data1.value(), data2.value());
}

void TestQCNetworkCacheIntegration::testMultipleRequests()
{
    // 多次重复请求不应破坏缓存内容和成功路径。
    m_manager->setCache(m_cache);

    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/get?test=multiple"));
    request.setCachePolicy(QCNetworkCachePolicy::PreferCache);

    for (int i = 0; i < 3; ++i) {
        auto *reply = m_manager->get(request);
        QVERIFY(waitForFinished(reply));
        QCOMPARE(reply->error(), NetworkError::NoError);

        auto data = reply->readAll();
        QVERIFY(data.has_value());
        QVERIFY(!data->isEmpty());

        reply->deleteLater();
    }
}

// ============================================================================
// 策略命中与写入
// ============================================================================

void TestQCNetworkCacheIntegration::testPreferCacheHit()
{
    // PreferCache 命中时应直接读取缓存。
    QUrl url(m_httpbinBaseUrl + "/get?test=prefercache");
    QByteArray testData = "{\"cached\": true}";
    verifyCacheHitSignalContract(QCNetworkCachePolicy::PreferCache, url, testData, true);
}

void TestQCNetworkCacheIntegration::testPreferCacheMiss()
{
    // PreferCache 未命中时应回退到网络。
    m_manager->setCache(m_cache);

    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/get?test=cachemiss"));
    request.setCachePolicy(QCNetworkCachePolicy::PreferCache);

    auto *reply = m_manager->get(request);
    QVERIFY(waitForFinished(reply));
    QCOMPARE(reply->error(), NetworkError::NoError);

    auto data = reply->readAll();
    QVERIFY(data.has_value());
    QVERIFY(!data->isEmpty());

    reply->deleteLater();
}

void TestQCNetworkCacheIntegration::testOnlyCacheHit()
{
    // OnlyCache 命中时不应触发网络失败。
    QUrl url(m_httpbinBaseUrl + "/get?test=onlycache");
    QByteArray testData = "{\"only_cache\": true}";
    verifyCacheHitSignalContract(QCNetworkCachePolicy::OnlyCache, url, testData, true);
}

void TestQCNetworkCacheIntegration::testOnlyCacheMiss()
{
    // OnlyCache 未命中时必须稳定地走错误终态。

    m_manager->setCache(m_cache);

    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/get?test=onlycachemiss"));
    request.setCachePolicy(QCNetworkCachePolicy::OnlyCache);

    auto *reply = m_manager->get(request);

    QVERIFY2(waitForFinished(reply, 1000), "OnlyCache miss should trigger error signal");

    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QVERIFY(reply->isFinished());

    reply->deleteLater();
}

void TestQCNetworkCacheIntegration::testOnlyCacheHitWithZeroByteBody()
{
    const QUrl url(m_httpbinBaseUrl + "/status/204?test=onlycache-zero");
    verifyCacheHitSignalContract(QCNetworkCachePolicy::OnlyCache, url, QByteArray(), false);
}

void TestQCNetworkCacheIntegration::testOnlyCacheHitWithZeroByteBodyAndNoHeaders()
{
    m_manager->setCache(m_cache);

    const QUrl url(m_httpbinBaseUrl + "/status/204?test=onlycache-zero-no-headers");
    QCNetworkCacheMetadata meta;
    meta.setUrl(url);
    meta.setExpirationDate(QDateTime::currentDateTime().addSecs(3600));
    m_cache->insert(requestKeyFor(url), QByteArray(), meta);

    QCNetworkRequest request(url);
    request.setCachePolicy(QCNetworkCachePolicy::OnlyCache);

    auto *reply = m_manager->get(request);
    QVERIFY(waitForFinished(reply, 100));
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->httpStatusCode(), 200);

    const auto data = reply->readAll();
    QVERIFY(data.has_value());
    QVERIFY(data->isEmpty());

    reply->deleteLater();
}

void TestQCNetworkCacheIntegration::testOnlyCacheHitWithExpiredBody()
{
    const QUrl url(m_httpbinBaseUrl + "/get?test=onlycache-expired");
    const QByteArray testData = "{\"only_cache_expired\": true}";
    verifyCacheHitSignalContract(QCNetworkCachePolicy::OnlyCache, url, testData, true, -60);
}

void TestQCNetworkCacheIntegration::testOnlyCacheHitWithExpiredZeroByteBody()
{
    const QUrl url(m_httpbinBaseUrl + "/status/204?test=onlycache-expired-zero");
    verifyCacheHitSignalContract(QCNetworkCachePolicy::OnlyCache, url, QByteArray(), false, -60);
}

void TestQCNetworkCacheIntegration::testPreferCacheHitWithZeroByteBody()
{
    const QUrl url(m_httpbinBaseUrl + "/status/204?test=prefercache-zero");
    verifyCacheHitSignalContract(QCNetworkCachePolicy::PreferCache, url, QByteArray(), false);
}

void TestQCNetworkCacheIntegration::testAlwaysCache()
{
    // AlwaysCache 只消费已有缓存，不依赖当前网络可达性。
    m_manager->setCache(m_cache);

    QUrl url(m_httpbinBaseUrl + "/get?test=alwayscache_unique_12345");

    QByteArray testData = "{\"cached\": true}";
    QCNetworkCacheMetadata meta;
    meta.setUrl(url);
    meta.setCreationDate(QDateTime::currentDateTime());
    meta.setExpirationDate(QDateTime::currentDateTime().addSecs(3600)); // 未过期（1小时后）
    meta.setSize(testData.size());

    meta.setHeader("Content-Type", "application/json");
    meta.setHeader("Content-Length", QByteArray::number(testData.size()));

    m_cache->insert(requestKeyFor(url), testData, meta);

    const auto cached = m_cache->lookup(requestKeyFor(url), QCNetworkCacheReadMode::FreshOnly);
    QVERIFY2(cached.hit(), "Cache insert failed");
    QCOMPARE(cached.body(), testData);

    QCNetworkRequest request(url);
    request.setCachePolicy(QCNetworkCachePolicy::AlwaysCache);

    auto *reply = m_manager->get(request);
    QVERIFY(waitForFinished(reply, 200)); // 增加超时时间
    QCOMPARE(reply->error(), NetworkError::NoError);

    auto data = reply->readAll();
    QCOMPARE(data.value(), testData);

    reply->deleteLater();
}

void TestQCNetworkCacheIntegration::testAlwaysCacheHitWithExpiredBody()
{
    const QUrl url(m_httpbinBaseUrl + "/get?test=alwayscache-expired");
    const QByteArray testData = "{\"always_cache_expired\": true}";
    verifyCacheHitSignalContract(QCNetworkCachePolicy::AlwaysCache, url, testData, true, -60);
}

void TestQCNetworkCacheIntegration::testAlwaysCacheHitWithExpiredZeroByteBody()
{
    const QUrl url(m_httpbinBaseUrl + "/status/204?test=alwayscache-expired-zero");
    verifyCacheHitSignalContract(QCNetworkCachePolicy::AlwaysCache, url, QByteArray(), false, -60);
}

void TestQCNetworkCacheIntegration::testPreferNetworkSuccess()
{
    // PreferNetwork 在网络成功时应返回新鲜响应。
    m_manager->setCache(m_cache);

    QCNetworkRequest request(QUrl(m_httpbinBaseUrl + "/get?test=prefernetwork"));
    request.setCachePolicy(QCNetworkCachePolicy::PreferNetwork);

    auto *reply = m_manager->get(request);
    QVERIFY(waitForFinished(reply));
    QCOMPARE(reply->error(), NetworkError::NoError);

    auto data = reply->readAll();
    QVERIFY(data.has_value());

    reply->deleteLater();
}

void TestQCNetworkCacheIntegration::testAutoCacheWrite()
{
    // 首次网络读取后，同一 URL 应留下可用于重验证的缓存副本。
    m_manager->setCache(m_cache);

    QUrl url(m_httpbinBaseUrl + "/get?test=autowrite");

    QCNetworkRequest request(url);
    request.setCachePolicy(QCNetworkCachePolicy::PreferCache);

    auto *reply = m_manager->get(request);
    QVERIFY(waitForFinished(reply));
    QCOMPARE(reply->error(), NetworkError::NoError);

    auto networkData = reply->readAll();
    QVERIFY(networkData.has_value());
    reply->deleteLater();

    const auto cached = m_cache->lookup(requestKeyFor(url), QCNetworkCacheReadMode::AllowStale);
    QVERIFY(cached.hit());
    QCOMPARE(cached.body(), networkData.value());

    auto *reply2 = m_manager->get(request);
    QVERIFY(waitForFinished(reply2, 100));

    auto cacheData = reply2->readAll();
    QCOMPARE(cacheData.value(), networkData.value());

    reply2->deleteLater();
}

void TestQCNetworkCacheIntegration::testStaleEntryRevalidatedWith304()
{
    m_manager->setCache(m_cache);

    const QUrl url(m_httpbinBaseUrl + "/cache");
    QCNetworkRequest request(url);
    request.setCachePolicy(QCNetworkCachePolicy::PreferCache);

    auto *firstReply = m_manager->get(request);
    QVERIFY(waitForFinished(firstReply));
    QCOMPARE(firstReply->error(), NetworkError::NoError);
    QCOMPARE(firstReply->httpStatusCode(), 200);
    const auto firstBody = firstReply->readAll();
    QVERIFY(firstBody.has_value());
    firstReply->deleteLater();

    const auto firstEntry = m_cache->lookup(requestKeyFor(url), QCNetworkCacheReadMode::AllowStale);
    QVERIFY(firstEntry.hit());
    QVERIFY(!firstEntry.metadata().headers().value("etag").isEmpty());

    auto *revalidatedReply = m_manager->get(request);
    QVERIFY(waitForFinished(revalidatedReply));
    QCOMPARE(revalidatedReply->error(), NetworkError::NoError);
    QCOMPARE(revalidatedReply->httpStatusCode(), 200);
    const auto revalidatedBody = revalidatedReply->readAll();
    QVERIFY(revalidatedBody.has_value());
    QCOMPARE(revalidatedBody.value(), firstBody.value());
    revalidatedReply->deleteLater();
}

void TestQCNetworkCacheIntegration::testVaryVariantsDoNotShare()
{
    CacheContractServer server;
    QVERIFY(server.start());
    m_manager->setCache(m_cache);

    const QUrl url = server.url(QStringLiteral("/vary"));
    QCNetworkRequest english(url);
    english.setCachePolicy(QCNetworkCachePolicy::PreferCache);
    english.setRawHeader(QByteArrayLiteral("Accept-Language"), QByteArrayLiteral("en"));
    auto *englishReply = m_manager->get(english);
    QVERIFY(waitForFinished(englishReply));
    QCOMPARE(englishReply->error(), NetworkError::NoError);
    const auto englishBody = englishReply->readAll();
    QVERIFY(englishBody.has_value());
    QCOMPARE(englishBody.value(), QByteArrayLiteral("vary:en"));
    englishReply->deleteLater();

    QCNetworkRequest french(url);
    french.setCachePolicy(QCNetworkCachePolicy::PreferCache);
    french.setRawHeader(QByteArrayLiteral("accept-language"), QByteArrayLiteral("fr"));
    auto *frenchReply = m_manager->get(french);
    QVERIFY(waitForFinished(frenchReply));
    QCOMPARE(frenchReply->error(), NetworkError::NoError);
    const auto frenchBody = frenchReply->readAll();
    QVERIFY(frenchBody.has_value());
    QCOMPARE(frenchBody.value(), QByteArrayLiteral("vary:fr"));
    frenchReply->deleteLater();

    auto *cachedEnglishReply = m_manager->get(english);
    QVERIFY(waitForFinished(cachedEnglishReply));
    QCOMPARE(cachedEnglishReply->error(), NetworkError::NoError);
    const auto cachedEnglishBody = cachedEnglishReply->readAll();
    QVERIFY(cachedEnglishBody.has_value());
    QCOMPARE(cachedEnglishBody.value(), englishBody.value());
    cachedEnglishReply->deleteLater();

    QCOMPARE(server.requestCount(), 2);
}

void TestQCNetworkCacheIntegration::testAuthenticatedPartitionsDoNotShare()
{
    CacheContractServer server;
    QVERIFY(server.start());
    m_manager->setCache(m_cache);

    const QUrl url = server.url(QStringLiteral("/auth"));
    QCNetworkRequest tenantA(url);
    tenantA.setCachePolicy(QCNetworkCachePolicy::PreferCache);
    tenantA.setRawHeader(QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer tenant-a"));
    tenantA.setCachePartitionKey(QByteArrayLiteral("tenant-a"));
    auto *replyA = m_manager->get(tenantA);
    QVERIFY(waitForFinished(replyA));
    const auto bodyA = replyA->readAll();
    QVERIFY(bodyA.has_value());
    QCOMPARE(bodyA.value(), QByteArrayLiteral("auth:Bearer tenant-a"));
    replyA->deleteLater();

    QCNetworkRequest tenantB(url);
    tenantB.setCachePolicy(QCNetworkCachePolicy::PreferCache);
    tenantB.setRawHeader(QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer tenant-b"));
    tenantB.setCachePartitionKey(QByteArrayLiteral("tenant-b"));
    auto *replyB = m_manager->get(tenantB);
    QVERIFY(waitForFinished(replyB));
    const auto bodyB = replyB->readAll();
    QVERIFY(bodyB.has_value());
    QCOMPARE(bodyB.value(), QByteArrayLiteral("auth:Bearer tenant-b"));
    replyB->deleteLater();

    auto *cachedReplyA = m_manager->get(tenantA);
    QVERIFY(waitForFinished(cachedReplyA));
    const auto cachedBodyA = cachedReplyA->readAll();
    QVERIFY(cachedBodyA.has_value());
    QCOMPARE(cachedBodyA.value(), bodyA.value());
    cachedReplyA->deleteLater();

    QCOMPARE(server.requestCount(), 2);
}

void TestQCNetworkCacheIntegration::testUnpartitionedAuthenticationIsNotStored()
{
    CacheContractServer server;
    QVERIFY(server.start());
    m_manager->setCache(m_cache);

    const QUrl url = server.url(QStringLiteral("/auth"));
    QCNetworkRequest request(url);
    request.setCachePolicy(QCNetworkCachePolicy::PreferCache);
    request.setRawHeader(QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer secret"));

    auto *firstReply = m_manager->get(request);
    QVERIFY(waitForFinished(firstReply));
    QVERIFY(firstReply->readAll().has_value());
    firstReply->deleteLater();

    auto *secondReply = m_manager->get(request);
    QVERIFY(waitForFinished(secondReply));
    QVERIFY(secondReply->readAll().has_value());
    secondReply->deleteLater();

    QCNetworkCacheRequestKey key(HttpMethod::Get, url);
    key.setAuthenticationContext(true);
    QVERIFY(!m_cache->lookup(key, QCNetworkCacheReadMode::AllowStale).hit());
    QCOMPARE(server.requestCount(), 2);
}

void TestQCNetworkCacheIntegration::testNonGetAndNon200ResponsesAreNotStored()
{
    CacheContractServer server;
    QVERIFY(server.start());
    m_manager->setCache(m_cache);

    const QUrl createdUrl = server.url(QStringLiteral("/created"));
    QCNetworkRequest created(createdUrl);
    created.setCachePolicy(QCNetworkCachePolicy::PreferCache);
    auto *createdReply = m_manager->get(created);
    QVERIFY(waitForFinished(createdReply));
    QCOMPARE(createdReply->httpStatusCode(), 201);
    QVERIFY(createdReply->readAll().has_value());
    createdReply->deleteLater();
    QVERIFY(!m_cache->lookup(requestKeyFor(createdUrl), QCNetworkCacheReadMode::AllowStale).hit());

    const QUrl postUrl = server.url(QStringLiteral("/post"));
    QCNetworkRequest post(postUrl);
    post.setCachePolicy(QCNetworkCachePolicy::PreferCache);
    auto *postReply = m_manager->post(post, QByteArrayLiteral("request-body"));
    QVERIFY(waitForFinished(postReply));
    QCOMPARE(postReply->httpStatusCode(), 200);
    QVERIFY(postReply->readAll().has_value());
    postReply->deleteLater();

    const QCNetworkCacheRequestKey postKey(HttpMethod::Post, postUrl);
    QVERIFY(!m_cache->lookup(postKey, QCNetworkCacheReadMode::AllowStale).hit());
}

void TestQCNetworkCacheIntegration::testHeadDoesNotCreateBodyEntryAndUpdatesGet()
{
    CacheContractServer server;
    QVERIFY(server.start());
    m_manager->setCache(m_cache);

    const QUrl headOnlyUrl = server.url(QStringLiteral("/head-only"));
    QCNetworkRequest headOnly(headOnlyUrl);
    headOnly.setCachePolicy(QCNetworkCachePolicy::PreferCache);
    auto *headOnlyReply = m_manager->head(headOnly);
    QVERIFY(waitForFinished(headOnlyReply));
    QCOMPARE(headOnlyReply->httpStatusCode(), 200);
    QVERIFY(headOnlyReply->readAll().has_value());
    headOnlyReply->deleteLater();
    QVERIFY(!m_cache->lookup(requestKeyFor(headOnlyUrl), QCNetworkCacheReadMode::AllowStale).hit());

    const QUrl url = server.url(QStringLiteral("/head"));
    QCNetworkRequest getRequest(url);
    getRequest.setCachePolicy(QCNetworkCachePolicy::PreferCache);
    auto *getReply = m_manager->get(getRequest);
    QVERIFY(waitForFinished(getReply));
    const auto body = getReply->readAll();
    QVERIFY(body.has_value());
    getReply->deleteLater();

    auto *headReply = m_manager->head(getRequest);
    QVERIFY(waitForFinished(headReply));
    QCOMPARE(headReply->httpStatusCode(), 200);
    const auto headBody = headReply->readAll();
    QVERIFY(headBody.has_value());
    QVERIFY(headBody->isEmpty());
    headReply->deleteLater();

    const auto cached = m_cache->lookup(requestKeyFor(url), QCNetworkCacheReadMode::AllowStale);
    QVERIFY(cached.hit());
    QCOMPARE(cached.body(), body.value());
}

void TestQCNetworkCacheIntegration::testHeadMismatchDoesNotRefreshCachedMetadata()
{
    CacheContractServer server;
    QVERIFY(server.start());
    m_manager->setCache(m_cache);

    const QUrl url = server.url(QStringLiteral("/head-mismatch"));
    QCNetworkRequest getRequest(url);
    auto *getReply = m_manager->get(getRequest);
    QVERIFY(waitForFinished(getReply));
    QCOMPARE(getReply->readAll().value(), QByteArrayLiteral("old-body"));
    getReply->deleteLater();

    QCNetworkRequest headRequest(url);
    headRequest.setCachePolicy(QCNetworkCachePolicy::PreferNetwork);
    auto *headReply = m_manager->head(headRequest);
    QVERIFY(waitForFinished(headReply));
    QCOMPARE(headReply->error(), NetworkError::NoError);
    QCOMPARE(headReply->rawHeader(QByteArrayLiteral("ETag")), QByteArrayLiteral("\"head-v2\""));
    QCOMPARE(server.requestCount(), 2);
    headReply->deleteLater();

    const auto cached = m_cache->lookup(requestKeyFor(url), QCNetworkCacheReadMode::AllowStale);
    QVERIFY(cached.hit());
    QCOMPARE(cached.body(), QByteArrayLiteral("old-body"));
    QCOMPARE(cached.metadata().headers().value(QByteArrayLiteral("etag")),
             QByteArrayLiteral("\"head-v1\""));
    QCOMPARE(cached.metadata().headers().value(QByteArrayLiteral("content-length")),
             QByteArray::number(QByteArrayLiteral("old-body").size()));
}

void TestQCNetworkCacheIntegration::testDateAgeCanMakeMaxAgeResponseStale()
{
    CacheContractServer server;
    QVERIFY(server.start());
    m_manager->setCache(m_cache);

    const QUrl url = server.url(QStringLiteral("/age-stale"));
    QCNetworkRequest request(url);
    request.setCachePolicy(QCNetworkCachePolicy::PreferCache);

    auto *firstReply = m_manager->get(request);
    QVERIFY(waitForFinished(firstReply));
    QCOMPARE(firstReply->readAll().value(), QByteArrayLiteral("age-stale:1"));
    firstReply->deleteLater();

    auto *secondReply = m_manager->get(request);
    QVERIFY(waitForFinished(secondReply));
    QCOMPARE(secondReply->readAll().value(), QByteArrayLiteral("age-stale:2"));
    QCOMPARE(server.requestCount(), 2);
    secondReply->deleteLater();
}

void TestQCNetworkCacheIntegration::testAgeInt64MaxIsStaleWithoutOverflow()
{
    CacheContractServer server;
    QVERIFY(server.start());
    m_manager->setCache(m_cache);

    const QUrl url = server.url(QStringLiteral("/age-int64-max"));
    QCNetworkRequest request(url);
    request.setCachePolicy(QCNetworkCachePolicy::PreferCache);

    auto *firstReply = m_manager->get(request);
    QVERIFY(waitForFinished(firstReply));
    QCOMPARE(firstReply->readAll().value(), QByteArrayLiteral("age-int64-max:1"));
    firstReply->deleteLater();

    auto *secondReply = m_manager->get(request);
    QVERIFY(waitForFinished(secondReply));
    QCOMPARE(secondReply->readAll().value(), QByteArrayLiteral("age-int64-max:2"));
    QCOMPARE(server.requestCount(), 2);
    secondReply->deleteLater();
}

void TestQCNetworkCacheIntegration::testCacheHitUpdatesObservableAgeHeader()
{
    CacheContractServer server;
    QVERIFY(server.start());
    m_manager->setCache(m_cache);

    const QUrl url = server.url(QStringLiteral("/age-hit"));
    QCNetworkRequest networkRequest(url);
    auto *networkReply = m_manager->get(networkRequest);
    QVERIFY(waitForFinished(networkReply));
    QCOMPARE(networkReply->readAll().value(), QByteArrayLiteral("age-hit"));
    networkReply->deleteLater();

    QCNetworkRequest cacheOnly(url);
    cacheOnly.setCachePolicy(QCNetworkCachePolicy::OnlyCache);
    auto *cacheReply = m_manager->get(cacheOnly);
    QVERIFY(waitForFinished(cacheReply));
    QCOMPARE(cacheReply->error(), NetworkError::NoError);
    bool ok       = false;
    const int age = cacheReply->rawHeader(QByteArrayLiteral("Age")).toInt(&ok);
    QVERIFY(ok);
    QVERIFY2(age >= 25, qPrintable(QStringLiteral("Age header was %1").arg(age)));
    QCOMPARE(server.requestCount(), 1);
    cacheReply->deleteLater();
}

void TestQCNetworkCacheIntegration::testPreferNetworkFallsBackBeforeResponseIsVisible()
{
    CacheContractServer server;
    QVERIFY(server.start());
    m_manager->setCache(m_cache);
    const QUrl url = server.url(QStringLiteral("/prefer-network-fallback"));

    QCNetworkRequest firstRequest(url);
    auto *firstReply = m_manager->get(firstRequest);
    QVERIFY(waitForFinished(firstReply));
    QCOMPARE(firstReply->readAll().value(), QByteArrayLiteral("cached-fallback"));
    firstReply->deleteLater();

    QCNetworkRequest retryRequest(url);
    retryRequest.setCachePolicy(QCNetworkCachePolicy::PreferNetwork);
    auto *retryReply = m_manager->get(retryRequest);
    QVERIFY(waitForFinished(retryReply));
    QCOMPARE(retryReply->error(), NetworkError::NoError);
    QCOMPARE(retryReply->readAll().value(), QByteArrayLiteral("cached-fallback"));
    QCOMPARE(server.requestCount(), 2);
    retryReply->deleteLater();
}

void TestQCNetworkCacheIntegration::testPreferNetworkDoesNotFallbackAfterPartialBody()
{
    CacheContractServer server;
    QVERIFY(server.start());
    m_manager->setCache(m_cache);
    const QUrl url = server.url(QStringLiteral("/prefer-network-partial"));

    QCNetworkRequest firstRequest(url);
    auto *firstReply = m_manager->get(firstRequest);
    QVERIFY(waitForFinished(firstReply));
    QCOMPARE(firstReply->readAll().value(), QByteArrayLiteral("cached-body"));
    firstReply->deleteLater();

    QCNetworkRequest retryRequest(url);
    retryRequest.setCachePolicy(QCNetworkCachePolicy::PreferNetwork);
    auto *retryReply = m_manager->get(retryRequest);
    QByteArray streamedBody;
    connect(retryReply, &QCNetworkReply::readyRead, this, [&streamedBody, retryReply]() {
        streamedBody += retryReply->readAll().value_or(QByteArray());
    });
    QVERIFY(waitForFinished(retryReply));
    QVERIFY(retryReply->error() != NetworkError::NoError);
    QCOMPARE(streamedBody, QByteArrayLiteral("partial"));
    QCOMPARE(server.requestCount(), 2);
    retryReply->deleteLater();
}

void TestQCNetworkCacheIntegration::testNoCacheResponseIsRevalidated()
{
    CacheContractServer server;
    QVERIFY(server.start());
    m_manager->setCache(m_cache);

    const QUrl url = server.url(QStringLiteral("/nocache"));
    QCNetworkRequest request(url);
    request.setCachePolicy(QCNetworkCachePolicy::PreferCache);

    auto *firstReply = m_manager->get(request);
    QVERIFY(waitForFinished(firstReply));
    const auto firstBody = firstReply->readAll();
    QVERIFY(firstBody.has_value());
    firstReply->deleteLater();

    auto *secondReply = m_manager->get(request);
    QVERIFY(waitForFinished(secondReply));
    QCOMPARE(secondReply->httpStatusCode(), 200);
    const auto secondBody = secondReply->readAll();
    QVERIFY(secondBody.has_value());
    QCOMPARE(secondBody.value(), firstBody.value());
    secondReply->deleteLater();

    QCOMPARE(server.requestCount(), 2);
}

void TestQCNetworkCacheIntegration::testRequestNoCacheBypassesFreshHit()
{
    CacheContractServer server;
    QVERIFY(server.start());
    m_manager->setCache(m_cache);

    QCNetworkRequest initial(server.url(QStringLiteral("/request-no-cache")));
    initial.setCachePolicy(QCNetworkCachePolicy::PreferCache);
    auto *initialReply = m_manager->get(initial);
    QVERIFY(waitForFinished(initialReply));
    QCOMPARE(initialReply->error(), NetworkError::NoError);
    QCOMPARE(server.requestCount(), 1);
    initialReply->deleteLater();

    QCNetworkRequest revalidate(initial.url());
    revalidate.setCachePolicy(QCNetworkCachePolicy::PreferCache);
    revalidate.setRawHeader(QByteArrayLiteral("Cache-Control"), QByteArrayLiteral("no-cache"));
    auto *revalidateReply = m_manager->get(revalidate);
    QVERIFY(waitForFinished(revalidateReply));
    QCOMPARE(revalidateReply->error(), NetworkError::NoError);
    QCOMPARE(server.requestCount(), 2);
    revalidateReply->deleteLater();
}

void TestQCNetworkCacheIntegration::testRequestNoStorePreventsResponseStorage()
{
    CacheContractServer server;
    QVERIFY(server.start());
    m_manager->setCache(m_cache);

    QCNetworkRequest networkRequest(server.url(QStringLiteral("/request-no-store")));
    networkRequest.setCachePolicy(QCNetworkCachePolicy::PreferNetwork);
    networkRequest.setRawHeader(QByteArrayLiteral("Cache-Control"), QByteArrayLiteral("no-store"));
    auto *networkReply = m_manager->get(networkRequest);
    QVERIFY(waitForFinished(networkReply));
    QCOMPARE(networkReply->error(), NetworkError::NoError);
    QCOMPARE(server.requestCount(), 1);
    networkReply->deleteLater();

    QCNetworkRequest cacheOnly(networkRequest.url());
    cacheOnly.setCachePolicy(QCNetworkCachePolicy::OnlyCache);
    auto *cacheReply = m_manager->get(cacheOnly);
    QVERIFY(waitForFinished(cacheReply));
    QCOMPARE(cacheReply->error(), NetworkError::InvalidRequest);
    QCOMPARE(server.requestCount(), 1);
    cacheReply->deleteLater();
}

void TestQCNetworkCacheIntegration::testStreamingReadDoesNotTruncateCachedBody()
{
    CacheContractServer server;
    QVERIFY(server.start());
    m_manager->setCache(m_cache);

    const QUrl url = server.url(QStringLiteral("/streaming-cache"));
    QCNetworkRequest networkRequest(url);
    networkRequest.setCachePolicy(QCNetworkCachePolicy::PreferNetwork);
    auto *networkReply = m_manager->get(networkRequest);
    QByteArray streamedBody;
    connect(networkReply, &QCNetworkReply::readyRead, this, [&streamedBody, networkReply]() {
        const auto chunk = networkReply->readAll();
        if (chunk.has_value()) {
            streamedBody.append(chunk.value());
        }
    });
    QVERIFY(waitForFinished(networkReply));
    QCOMPARE(networkReply->error(), NetworkError::NoError);
    QCOMPARE(streamedBody, QByteArrayLiteral("default"));
    QCOMPARE(server.requestCount(), 1);
    networkReply->deleteLater();

    QCNetworkRequest cacheOnly(url);
    cacheOnly.setCachePolicy(QCNetworkCachePolicy::OnlyCache);
    auto *cacheReply = m_manager->get(cacheOnly);
    QVERIFY(waitForFinished(cacheReply));
    QCOMPARE(cacheReply->error(), NetworkError::NoError);
    QCOMPARE(cacheReply->readAll().value(), QByteArrayLiteral("default"));
    QCOMPARE(server.requestCount(), 1);
    cacheReply->deleteLater();
}

void TestQCNetworkCacheIntegration::testCachedResponsePreservesOrderedRawHeaders()
{
    CacheContractServer server;
    QVERIFY(server.start());
    m_manager->setCache(m_cache);

    const QUrl url = server.url(QStringLiteral("/raw-headers"));
    QCNetworkRequest networkRequest(url);
    auto *networkReply = m_manager->get(networkRequest);
    QVERIFY(waitForFinished(networkReply));
    QCOMPARE(networkReply->error(), NetworkError::NoError);
    const QList<RawHeaderPair> networkHeaders = networkReply->rawHeaders();
    QCOMPARE(networkHeaders.count(
                 qMakePair(QByteArrayLiteral("X-Dupe"), QByteArrayLiteral("first"))),
             1);
    QCOMPARE(networkHeaders.count(
                 qMakePair(QByteArrayLiteral("x-dupe"), QByteArrayLiteral("second"))),
             1);
    networkReply->deleteLater();

    QCNetworkRequest cacheOnly(url);
    cacheOnly.setCachePolicy(QCNetworkCachePolicy::OnlyCache);
    auto *cacheReply = m_manager->get(cacheOnly);
    QVERIFY(waitForFinished(cacheReply));
    QCOMPARE(cacheReply->error(), NetworkError::NoError);
    QList<RawHeaderPair> cachedHeaders = cacheReply->rawHeaders();
    cachedHeaders.removeIf([](const RawHeaderPair &header) {
        return QByteArrayView(header.first).compare(QByteArrayView("age"), Qt::CaseInsensitive)
               == 0;
    });
    QCOMPARE(cachedHeaders, networkHeaders);
    QVERIFY(cacheReply->hasRawHeader(QByteArrayLiteral("Age")));
    QCOMPARE(server.requestCount(), 1);
    cacheReply->deleteLater();
}

void TestQCNetworkCacheIntegration::testSensitiveResponseIsNotStored()
{
    CacheContractServer server;
    QVERIFY(server.start());
    m_manager->setCache(m_cache);

    const QUrl url = server.url(QStringLiteral("/sensitive-headers"));
    QCNetworkRequest networkRequest(url);
    auto *networkReply = m_manager->get(networkRequest);
    QVERIFY(waitForFinished(networkReply));
    QCOMPARE(networkReply->error(), NetworkError::NoError);
    QCOMPARE(networkReply->rawHeader(QByteArrayLiteral("Set-Cookie")),
             QByteArrayLiteral("session=response-secret; HttpOnly"));
    QCOMPARE(server.requestCount(), 1);
    networkReply->deleteLater();

    QCNetworkRequest cacheOnly(url);
    cacheOnly.setCachePolicy(QCNetworkCachePolicy::OnlyCache);
    auto *cacheReply = m_manager->get(cacheOnly);
    QVERIFY(waitForFinished(cacheReply));
    QCOMPARE(cacheReply->error(), NetworkError::InvalidRequest);
    QCOMPARE(server.requestCount(), 1);
    cacheReply->deleteLater();
}

void TestQCNetworkCacheIntegration::testEarlierNoStoreLinePreventsStorage()
{
    CacheContractServer server;
    QVERIFY(server.start());
    m_manager->setCache(m_cache);

    QCNetworkRequest networkRequest(server.url(QStringLiteral("/multi-cache-control")));
    auto *networkReply = m_manager->get(networkRequest);
    QVERIFY(waitForFinished(networkReply));
    QCOMPARE(networkReply->error(), NetworkError::NoError);
    QCOMPARE(networkReply->readAll().value(), QByteArrayLiteral("not-stored"));
    networkReply->deleteLater();

    QCNetworkRequest cacheOnly(networkRequest.url());
    cacheOnly.setCachePolicy(QCNetworkCachePolicy::OnlyCache);
    auto *cacheReply = m_manager->get(cacheOnly);
    QVERIFY(waitForFinished(cacheReply));
    QCOMPARE(cacheReply->error(), NetworkError::InvalidRequest);
    QCOMPARE(server.requestCount(), 1);
    cacheReply->deleteLater();
}

void TestQCNetworkCacheIntegration::testDuplicateDateAgePreventsStorage()
{
    CacheContractServer server;
    QVERIFY(server.start());
    m_manager->setCache(m_cache);

    QCNetworkRequest networkRequest(server.url(QStringLiteral("/duplicate-date-age")));
    auto *networkReply = m_manager->get(networkRequest);
    QVERIFY(waitForFinished(networkReply));
    QCOMPARE(networkReply->error(), NetworkError::NoError);
    QCOMPARE(networkReply->readAll().value(), QByteArrayLiteral("conflicting-single-values"));
    networkReply->deleteLater();

    QCNetworkRequest cacheOnly(networkRequest.url());
    cacheOnly.setCachePolicy(QCNetworkCachePolicy::OnlyCache);
    auto *cacheReply = m_manager->get(cacheOnly);
    QVERIFY(waitForFinished(cacheReply));
    QCOMPARE(cacheReply->error(), NetworkError::InvalidRequest);
    QCOMPARE(server.requestCount(), 1);
    cacheReply->deleteLater();
}

void TestQCNetworkCacheIntegration::testVaryUsesConfiguredReferer()
{
    CacheContractServer server;
    QVERIFY(server.start());
    m_manager->setCache(m_cache);
    const QUrl url = server.url(QStringLiteral("/vary-referer"));

    QCNetworkRequest firstRequest(url);
    firstRequest.setCachePolicy(QCNetworkCachePolicy::PreferCache);
    firstRequest.setReferer(QStringLiteral("https://first.example/source"));
    auto *firstReply = m_manager->get(firstRequest);
    QVERIFY(waitForFinished(firstReply));
    QCOMPARE(firstReply->readAll().value(),
             QByteArrayLiteral("referer:https://first.example/source"));
    firstReply->deleteLater();

    QCNetworkRequest secondRequest(url);
    secondRequest.setCachePolicy(QCNetworkCachePolicy::PreferCache);
    secondRequest.setReferer(QStringLiteral("https://second.example/source"));
    auto *secondReply = m_manager->get(secondRequest);
    QVERIFY(waitForFinished(secondReply));
    QCOMPARE(secondReply->readAll().value(),
             QByteArrayLiteral("referer:https://second.example/source"));
    QCOMPARE(server.requestCount(), 2);
    secondReply->deleteLater();
}

void TestQCNetworkCacheIntegration::testVaryUsesConfiguredAcceptEncoding()
{
    CacheContractServer server;
    QVERIFY(server.start());
    m_manager->setCache(m_cache);
    const QUrl url = server.url(QStringLiteral("/vary-encoding"));

    QCNetworkRequest firstRequest(url);
    firstRequest.setCachePolicy(QCNetworkCachePolicy::PreferCache);
    firstRequest.setAutoDecompressionEnabled(true);
    firstRequest.setAcceptedEncodings({QStringLiteral("gzip")});
    auto *firstReply = m_manager->get(firstRequest);
    QVERIFY(waitForFinished(firstReply));
    QCOMPARE(firstReply->readAll().value(), QByteArrayLiteral("encoding:gzip"));
    firstReply->deleteLater();

    QCNetworkRequest secondRequest(url);
    secondRequest.setCachePolicy(QCNetworkCachePolicy::PreferCache);
    secondRequest.setAutoDecompressionEnabled(true);
    secondRequest.setAcceptedEncodings({QStringLiteral("deflate")});
    auto *secondReply = m_manager->get(secondRequest);
    QVERIFY(waitForFinished(secondReply));
    QCOMPARE(secondReply->readAll().value(), QByteArrayLiteral("encoding:deflate"));
    QCOMPARE(server.requestCount(), 2);
    secondReply->deleteLater();
}

void TestQCNetworkCacheIntegration::testUnknownAutoAcceptEncodingVaryIsNotStored()
{
    CacheContractServer server;
    QVERIFY(server.start());
    m_manager->setCache(m_cache);
    const QUrl url = server.url(QStringLiteral("/vary-encoding"));

    QCNetworkRequest networkRequest(url);
    networkRequest.setAutoDecompressionEnabled(true);
    auto *networkReply = m_manager->get(networkRequest);
    QVERIFY(waitForFinished(networkReply));
    QVERIFY(networkReply->readAll().value().startsWith(QByteArrayLiteral("encoding:")));
    QCOMPARE(server.requestCount(), 1);
    networkReply->deleteLater();

    QCNetworkRequest cacheOnly(networkRequest);
    cacheOnly.setCachePolicy(QCNetworkCachePolicy::OnlyCache);
    auto *cacheReply = m_manager->get(cacheOnly);
    QVERIFY(waitForFinished(cacheReply));
    QCOMPARE(cacheReply->error(), NetworkError::InvalidRequest);
    QCOMPARE(server.requestCount(), 1);
    cacheReply->deleteLater();
}

void TestQCNetworkCacheIntegration::testCookieEngineVaryIsNotStored()
{
    CacheContractServer server;
    QVERIFY(server.start());
    m_manager->setCache(m_cache);

    QCNetworkAccessManager::ShareHandleConfig shareConfig;
    shareConfig.setShareCookies(true);
    m_manager->setShareHandleConfig(shareConfig);
    const QUrl url = server.url(QStringLiteral("/vary-cookie"));
    QCCookie cookie(QByteArrayLiteral("session"), QByteArrayLiteral("engine-value"));
    QString cookieError;
    QVERIFY2(m_manager->importCookies({cookie}, url, &cookieError), qPrintable(cookieError));

    QCNetworkRequest networkRequest(url);
    networkRequest.setCachePartitionKey(QByteArrayLiteral("cookie-session"));
    auto *networkReply = m_manager->get(networkRequest);
    QVERIFY(waitForFinished(networkReply));
    QCOMPARE(networkReply->readAll().value(), QByteArrayLiteral("cookie:session=engine-value"));
    QCOMPARE(server.requestCount(), 1);
    networkReply->deleteLater();

    QCNetworkRequest cacheOnly(networkRequest);
    cacheOnly.setCachePolicy(QCNetworkCachePolicy::OnlyCache);
    auto *cacheReply = m_manager->get(cacheOnly);
    QVERIFY(waitForFinished(cacheReply));
    QCOMPARE(cacheReply->error(), NetworkError::InvalidRequest);
    QCOMPARE(server.requestCount(), 1);
    cacheReply->deleteLater();
}

void TestQCNetworkCacheIntegration::testAutoRefererVaryIsNotStored()
{
    CacheContractServer server;
    QVERIFY(server.start());
    m_manager->setCache(m_cache);
    const QUrl url = server.url(QStringLiteral("/redirect-auto-referer"));

    QCNetworkRequest networkRequest(url);
    networkRequest.setFollowLocation(true);
    networkRequest.setAutoRefererEnabled(true);
    auto *networkReply = m_manager->get(networkRequest);
    QVERIFY(waitForFinished(networkReply));
    QCOMPARE(networkReply->error(), NetworkError::NoError);
    QVERIFY(networkReply->readAll().value().startsWith(QByteArrayLiteral("referer:")));
    QCOMPARE(server.requestCount(), 2);
    networkReply->deleteLater();

    QCNetworkRequest cacheOnly(networkRequest);
    cacheOnly.setCachePolicy(QCNetworkCachePolicy::OnlyCache);
    auto *cacheReply = m_manager->get(cacheOnly);
    QVERIFY(waitForFinished(cacheReply));
    QCOMPARE(cacheReply->error(), NetworkError::InvalidRequest);
    QCOMPARE(server.requestCount(), 2);
    cacheReply->deleteLater();
}

void TestQCNetworkCacheIntegration::testRevalidationPreservesOrderedRawHeaders()
{
    CacheContractServer server;
    QVERIFY(server.start());
    m_manager->setCache(m_cache);
    const QUrl url = server.url(QStringLiteral("/revalidate-raw"));

    QCNetworkRequest request(url);
    request.setCachePolicy(QCNetworkCachePolicy::PreferCache);
    auto *firstReply = m_manager->get(request);
    QVERIFY(waitForFinished(firstReply));
    QCOMPARE(firstReply->readAll().value(), QByteArrayLiteral("revalidated-raw-body"));
    firstReply->deleteLater();

    auto *revalidatedReply = m_manager->get(request);
    QVERIFY(waitForFinished(revalidatedReply));
    QCOMPARE(revalidatedReply->error(), NetworkError::NoError);
    QCOMPARE(revalidatedReply->httpStatusCode(), 200);
    QCOMPARE(revalidatedReply->readAll().value(), QByteArrayLiteral("revalidated-raw-body"));

    const auto contractHeaders = [](const QList<RawHeaderPair> &headers) {
        QList<RawHeaderPair> result;
        for (const RawHeaderPair &header : headers) {
            const QByteArray lowerName = header.first.toLower();
            if (lowerName == QByteArrayLiteral("x-stable")
                || lowerName == QByteArrayLiteral("x-replace")
                || lowerName == QByteArrayLiteral("x-new")) {
                result.append(header);
            }
        }
        return result;
    };
    const QList<RawHeaderPair> expected{
        qMakePair(QByteArrayLiteral("X-Stable"), QByteArrayLiteral("keep")),
        qMakePair(QByteArrayLiteral("X-Replace"), QByteArrayLiteral("new-first")),
        qMakePair(QByteArrayLiteral("x-replace"), QByteArrayLiteral("new-second")),
        qMakePair(QByteArrayLiteral("X-New"), QByteArrayLiteral("appended")),
    };
    QCOMPARE(contractHeaders(revalidatedReply->rawHeaders()), expected);
    QCOMPARE(server.requestCount(), 2);
    revalidatedReply->deleteLater();

    const auto cached = m_cache->lookup(requestKeyFor(url), QCNetworkCacheReadMode::AllowStale);
    QVERIFY(cached.hit());
    QCOMPARE(contractHeaders(cached.metadata().rawHeaders()), expected);
}

// ============================================================================
// 边界条件
// ============================================================================

void TestQCNetworkCacheIntegration::testCacheExpiration()
{
    // 过期缓存不能继续满足 PreferCache。
    m_manager->setCache(m_cache);

    QUrl url(m_httpbinBaseUrl + "/get?test=expiration");

    QByteArray expiredData = "{\"expired\": true}";
    QCNetworkCacheMetadata meta;
    meta.setUrl(url);
    meta.setExpirationDate(QDateTime::currentDateTime().addSecs(-1)); // 已过期
    m_cache->insert(requestKeyFor(url), expiredData, meta);

    QCNetworkRequest request(url);
    request.setCachePolicy(QCNetworkCachePolicy::PreferCache);

    auto *reply = m_manager->get(request);
    QVERIFY(waitForFinished(reply));
    QCOMPARE(reply->error(), NetworkError::NoError);

    auto data = reply->readAll();
    QVERIFY(data.has_value());
    QVERIFY(data.value() != expiredData);

    reply->deleteLater();
}

void TestQCNetworkCacheIntegration::testNoCacheHeader()
{
    // no-cache 响应头不应被写入缓存。
    m_manager->setCache(m_cache);

    QUrl url(m_httpbinBaseUrl + "/response-headers?Cache-Control=no-cache");

    QCNetworkRequest request(url);
    request.setCachePolicy(QCNetworkCachePolicy::PreferCache);

    auto *reply = m_manager->get(request);
    QVERIFY(waitForFinished(reply));
    QCOMPARE(reply->error(), NetworkError::NoError);

    reply->deleteLater();

    const auto cached = m_cache->lookup(requestKeyFor(url), QCNetworkCacheReadMode::FreshOnly);
    QCOMPARE(cached.status(), QCNetworkCacheLookupStatus::Miss);
}

void TestQCNetworkCacheIntegration::testConcurrentRequests()
{
    // 并发请求不应破坏完成路径或响应读取。
    m_manager->setCache(m_cache);

    QList<QCNetworkReply *> replies;

    for (int i = 0; i < 5; ++i) {
        QUrl url(m_httpbinBaseUrl + QStringLiteral("/get?test=concurrent&id=%1").arg(i));
        QCNetworkRequest request(url);
        request.setCachePolicy(QCNetworkCachePolicy::PreferCache);

        replies.append(m_manager->get(request));
    }

    for (auto *reply : replies) {
        QVERIFY(waitForFinished(reply));
        QCOMPARE(reply->error(), NetworkError::NoError);

        auto data = reply->readAll();
        QVERIFY(data.has_value());

        reply->deleteLater();
    }
}

// ============================================================================
// Qt Test Main
// ============================================================================

QTEST_MAIN(TestQCNetworkCacheIntegration)
#include "tst_QCNetworkCacheIntegration.moc"
