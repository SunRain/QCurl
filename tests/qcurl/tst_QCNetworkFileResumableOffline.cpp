/**
 * @file tst_QCNetworkFileResumableOffline.cpp
 * @brief QCNetworkResumableDownloadJob 离线门禁
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkCache.h"
#include "QCNetworkMiddleware.h"
#include "QCNetworkMockHandler.h"
#include "QCNetworkRequestScheduler.h"
#include "QCNetworkResumableDownloadJob.h"
#include "QCNetworkReply.h"

#include <QFile>
#include <QEventLoop>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace QCurl;

namespace {

class ResumableRangeServer final : public QObject
{
public:
    enum class PartialResponseMode {
        MatchingRange,
        MismatchedRangeStart,
    };

    explicit ResumableRangeServer(qint64 totalBytes, QObject *parent = nullptr)
        : QObject(parent)
        , m_totalBytes(totalBytes)
        , m_payload(static_cast<int>(totalBytes), Qt::Uninitialized)
    {
        for (int i = 0; i < m_payload.size(); ++i) {
            m_payload[i] = static_cast<char>(i % 251);
        }

        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (m_server.hasPendingConnections()) {
                if (QTcpSocket *socket = m_server.nextPendingConnection()) {
                    handleSocket(socket);
                }
            }
        });
    }

    bool start() { return m_server.listen(QHostAddress::LocalHost, 0); }

    QUrl url(const QString &path) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_server.serverPort()).arg(path));
    }

    QByteArray expectedPayload() const { return m_payload; }

    void setPartialResponseMode(PartialResponseMode mode) { m_partialResponseMode = mode; }

private:
    static qint64 parseRangeStart(const QByteArray &headerValue)
    {
        const QByteArray value = headerValue.trimmed();
        if (!value.startsWith("bytes=")) {
            return 0;
        }

        const int dashPos = value.indexOf('-');
        if (dashPos <= 6) {
            return 0;
        }

        bool ok = false;
        const qint64 start = value.mid(6, dashPos - 6).toLongLong(&ok);
        return ok ? qMax<qint64>(0, start) : 0;
    }

    void handleSocket(QTcpSocket *socket)
    {
        auto requestBuffer = QSharedPointer<QByteArray>::create();

        QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, requestBuffer]() {
            requestBuffer->append(socket->readAll());
            const int headerEnd = requestBuffer->indexOf("\r\n\r\n");
            if (headerEnd < 0) {
                return;
            }

            const QByteArray headerBlock = requestBuffer->left(headerEnd);
            const QList<QByteArray> lines = headerBlock.split('\n');

            qint64 rangeStart = 0;
            for (QByteArray line : lines) {
                line = line.trimmed();
                if (line.toLower().startsWith("range:")) {
                    const int colonPos = line.indexOf(':');
                    if (colonPos >= 0) {
                        rangeStart = parseRangeStart(line.mid(colonPos + 1));
                    }
                }
            }

            if (rangeStart >= m_totalBytes) {
                QByteArray resp("HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */");
                resp += QByteArray::number(m_totalBytes);
                resp += QByteArrayLiteral("\r\nConnection: close\r\n\r\n");
                socket->write(resp);
                socket->disconnectFromHost();
                return;
            }

            const bool isPartial = rangeStart > 0;
            qint64 responseStart = rangeStart;
            if (isPartial && m_partialResponseMode == PartialResponseMode::MismatchedRangeStart) {
                responseStart = qMin(rangeStart + 1, m_totalBytes - 1);
            }
            const qint64 responseBytes = m_totalBytes - responseStart;

            QByteArray resp;
            resp += isPartial ? QByteArrayLiteral("HTTP/1.1 206 Partial Content\r\n")
                              : QByteArrayLiteral("HTTP/1.1 200 OK\r\n");
            resp += QByteArrayLiteral("Content-Type: application/octet-stream\r\n");
            resp += QByteArrayLiteral("Accept-Ranges: bytes\r\n");
            resp += QByteArrayLiteral("Content-Length: ");
            resp += QByteArray::number(responseBytes);
            resp += QByteArrayLiteral("\r\n");
            if (isPartial) {
                resp += QByteArrayLiteral("Content-Range: bytes ");
                resp += QByteArray::number(responseStart);
                resp += QByteArrayLiteral("-");
                resp += QByteArray::number(m_totalBytes - 1);
                resp += QByteArrayLiteral("/");
                resp += QByteArray::number(m_totalBytes);
                resp += QByteArrayLiteral("\r\n");
            }
            resp += QByteArrayLiteral("Connection: close\r\n\r\n");

            socket->write(resp);
            socket->write(m_payload.constData() + responseStart, responseBytes);
            socket->flush();
            socket->disconnectFromHost();
        });
    }

    qint64 m_totalBytes = 0;
    QByteArray m_payload;
    QTcpServer m_server;
    PartialResponseMode m_partialResponseMode = PartialResponseMode::MatchingRange;
};

static bool waitForFinished(QCNetworkReply *reply, int timeoutMs = 10000)
{
    if (!reply) {
        return false;
    }
    if (reply->isFinished()) {
        return true;
    }
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
    return finishedSpy.wait(timeoutMs);
}

void processQueuedEvents()
{
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

class CountingMiddleware final : public QCNetworkMiddleware
{
public:
    void onRequestPreSend(QCNetworkRequest &request) override
    {
        Q_UNUSED(request);
        ++requestPreSendCount;
    }

    void onReplyCreated(QCNetworkReply *reply) override
    {
        Q_UNUSED(reply);
        ++replyCreatedCount;
    }

    void onResponseReceived(QCNetworkReply *reply) override
    {
        Q_UNUSED(reply);
        ++responseReceivedCount;
    }

    int requestPreSendCount = 0;
    int replyCreatedCount = 0;
    int responseReceivedCount = 0;
};

class CountingCache final : public QCNetworkCache
{
public:
    QCNetworkCacheLookupResult lookup(const QUrl &url, QCNetworkCacheReadMode mode) override
    {
        Q_UNUSED(url);
        Q_UNUSED(mode);
        ++lookupCount;
        return {};
    }

    void insert(const QUrl &url, const QByteArray &data, const QCNetworkCacheMetadata &meta) override
    {
        Q_UNUSED(url);
        Q_UNUSED(data);
        Q_UNUSED(meta);
        ++insertCount;
    }

    bool remove(const QUrl &url) override
    {
        Q_UNUSED(url);
        return false;
    }

    void clear() override {}
    qint64 cacheSize() const override { return 0; }
    qint64 maxCacheSize() const override { return 0; }
    void setMaxCacheSize(qint64 size) override { Q_UNUSED(size); }

    int lookupCount = 0;
    int insertCount = 0;
};

struct ResumableSideEffectHarness
{
    QCNetworkAccessManager manager;
    CountingMiddleware middleware;
    CountingCache cache;
    QCNetworkRequestScheduler *scheduler = nullptr;
    QSignalSpy queuedSpy;
    QSignalSpy startedSpy;

    ResumableSideEffectHarness()
        : scheduler(manager.scheduler())
        , queuedSpy(scheduler, &QCNetworkRequestScheduler::requestQueued)
        , startedSpy(scheduler, &QCNetworkRequestScheduler::requestStarted)
    {
        manager.addMiddleware(&middleware);
        manager.setCache(&cache);
    }

    void verifyNoSideEffects() const
    {
        QCOMPARE(middleware.requestPreSendCount, 0);
        QCOMPARE(middleware.replyCreatedCount, 0);
        QCOMPARE(middleware.responseReceivedCount, 0);
        QCOMPARE(cache.lookupCount, 0);
        QCOMPARE(cache.insertCount, 0);
        QCOMPARE(queuedSpy.count(), 0);
        QCOMPARE(startedSpy.count(), 0);
    }
};

} // namespace

class TestQCNetworkFileResumableOffline : public QObject
{
    Q_OBJECT

private slots:
    void testConstructorDoesNotStartRequestOrReadTargetState();
    void testStartCreatesReplyAndRunsManagedPipeline();
    void testManagerThreadMismatchFailsBeforeReplyCreation();
    void testNoEventDispatcherFailsSynchronously();
    void testTreats416MatchingContentRangeAsAlreadyCompleteWithoutErrorSignal();
    void testFailsWhenContentRangeStartDoesNotMatch();
    void testDestroyedReplyBeforeJobFinishedFailsJobOnce();
    void testDestroyedReplyAfterJobFinishedClearsWeakReferenceWithoutSecondTerminalSignal();
};

void TestQCNetworkFileResumableOffline::testConstructorDoesNotStartRequestOrReadTargetState()
{
    ResumableSideEffectHarness harness;
    harness.manager.enableRequestScheduler(true);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString savePath = dir.filePath(QStringLiteral("partial.bin"));
    QFile partial(savePath);
    QVERIFY(partial.open(QIODevice::WriteOnly));
    partial.write(QByteArray(4096, 'p'));
    partial.close();

    QCNetworkResumableDownloadJob job(&harness.manager,
                                      QUrl(QStringLiteral("http://127.0.0.1:1/range.bin")),
                                      savePath);

    QCOMPARE(job.reply(), nullptr);
    QCOMPARE(job.existingSize(), qint64(0));
    QVERIFY(!job.isFinished());
    harness.verifyNoSideEffects();

    processQueuedEvents();
    QCOMPARE(job.reply(), nullptr);
    QCOMPARE(job.existingSize(), qint64(0));
    QVERIFY(!job.isFinished());
    harness.verifyNoSideEffects();
}

void TestQCNetworkFileResumableOffline::testStartCreatesReplyAndRunsManagedPipeline()
{
    ResumableSideEffectHarness harness;

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const qint64 totalBytes = 32 * 1024;
    ResumableRangeServer server(totalBytes);
    QVERIFY2(server.start(), "Cannot bind local port for resumable offline test server");

    const QString savePath = dir.filePath(QStringLiteral("resumable.bin"));
    const QByteArray prefix = server.expectedPayload().left(2048);
    QFile partial(savePath);
    QVERIFY(partial.open(QIODevice::WriteOnly));
    partial.write(prefix);
    partial.close();

    QCNetworkResumableDownloadJob job(&harness.manager,
                                      server.url(QStringLiteral("/range.bin")),
                                      savePath);
    QSignalSpy finishedSpy(&job, &QCNetworkTransferJob::finished);

    job.start();
    job.start();
    QCOMPARE(job.reply(), nullptr);
    QCOMPARE(finishedSpy.count(), 0);

    QTRY_VERIFY_WITH_TIMEOUT(job.reply() != nullptr, 1000);
    QCOMPARE(job.existingSize(), static_cast<qint64>(prefix.size()));
    QVERIFY(waitForFinished(job.reply()));
    QVERIFY(job.isFinished());
    QCOMPARE(job.error(), NetworkError::NoError);
    QCOMPARE(harness.middleware.requestPreSendCount, 1);
    QCOMPARE(harness.middleware.replyCreatedCount, 1);
    QCOMPARE(harness.middleware.responseReceivedCount, 1);

    QFile finalFile(savePath);
    QVERIFY(finalFile.open(QIODevice::ReadOnly));
    QCOMPARE(finalFile.readAll(), server.expectedPayload());

    job.reply()->deleteLater();
}

void TestQCNetworkFileResumableOffline::testManagerThreadMismatchFailsBeforeReplyCreation()
{
    QThread workerThread;
    workerThread.start();

    auto *manager = new QCNetworkAccessManager;
    CountingMiddleware middleware;
    manager->addMiddleware(&middleware);
    manager->moveToThread(&workerThread);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QCNetworkResumableDownloadJob job(manager,
                                      QUrl(QStringLiteral("http://127.0.0.1:1/range.bin")),
                                      dir.filePath(QStringLiteral("target.bin")));
    QSignalSpy failedSpy(&job, &QCNetworkTransferJob::failed);
    QSignalSpy finishedSpy(&job, &QCNetworkTransferJob::finished);

    job.start();

    QCOMPARE(job.reply(), nullptr);
    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(finishedSpy.count(), 0);

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(job.error(), NetworkError::InvalidRequest);
    QCOMPARE(job.reply(), nullptr);
    QCOMPARE(middleware.requestPreSendCount, 0);
    QCOMPARE(middleware.replyCreatedCount, 0);
    QCOMPARE(middleware.responseReceivedCount, 0);

    manager->deleteLater();
    workerThread.quit();
    QVERIFY(workerThread.wait(1000));
}

void TestQCNetworkFileResumableOffline::testNoEventDispatcherFailsSynchronously()
{
    ResumableSideEffectHarness harness;
    harness.manager.enableRequestScheduler(true);

    QThread noLoopThread;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString savePath = dir.filePath(QStringLiteral("target.bin"));
    QFile partial(savePath);
    QVERIFY(partial.open(QIODevice::WriteOnly));
    partial.write(QByteArray(4096, 'p'));
    partial.close();

    QCNetworkResumableDownloadJob job(&harness.manager,
                                      QUrl(QStringLiteral("http://127.0.0.1:1/no-event-loop.bin")),
                                      savePath);
    job.moveToThread(&noLoopThread);

    QSignalSpy failedSpy(&job, &QCNetworkTransferJob::failed);
    QSignalSpy finishedSpy(&job, &QCNetworkTransferJob::finished);

    job.start();

    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(job.error(), NetworkError::InvalidRequest);
    QVERIFY(job.errorString().contains(QStringLiteral("事件循环")));
    QCOMPARE(job.reply(), nullptr);
    QCOMPARE(job.existingSize(), qint64(0));
    harness.verifyNoSideEffects();
}

void TestQCNetworkFileResumableOffline::
    testTreats416MatchingContentRangeAsAlreadyCompleteWithoutErrorSignal()
{
    QCNetworkAccessManager manager;

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const qint64 totalBytes = 64 * 1024;
    ResumableRangeServer server(totalBytes);
    QVERIFY2(server.start(), "Cannot bind local port for resumable offline test server");

    const QString savePath = dir.filePath(QStringLiteral("already-complete.bin"));
    QFile complete(savePath);
    QVERIFY(complete.open(QIODevice::WriteOnly));
    complete.write(server.expectedPayload());
    complete.close();

    QCNetworkResumableDownloadJob job(&manager,
                                      server.url(QStringLiteral("/range.bin")),
                                      savePath);
    QCOMPARE(job.reply(), nullptr);
    job.start();
    QTRY_VERIFY_WITH_TIMEOUT(job.reply() != nullptr, 1000);
    auto *reply = job.reply();
    QVERIFY(reply);

    QSignalSpy errorSpy(reply,
                        static_cast<void (QCNetworkReply::*)(NetworkError)>(
                            &QCNetworkReply::error));
    QVERIFY(waitForFinished(reply));
    QVERIFY(job.isFinished());
    QCOMPARE(job.error(), NetworkError::NoError);
    QCOMPARE(errorSpy.count(), 0);
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->httpStatusCode(), 416);
    QCOMPARE(reply->rawHeader(QByteArrayLiteral("Content-Range")),
             QByteArray("bytes */" + QByteArray::number(totalBytes)));

    reply->deleteLater();
}

void TestQCNetworkFileResumableOffline::testFailsWhenContentRangeStartDoesNotMatch()
{
    QCNetworkAccessManager manager;

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const qint64 totalBytes = 96 * 1024;
    ResumableRangeServer server(totalBytes);
    server.setPartialResponseMode(ResumableRangeServer::PartialResponseMode::MismatchedRangeStart);
    QVERIFY2(server.start(), "Cannot bind local port for resumable offline test server");

    const QString savePath = dir.filePath(QStringLiteral("mismatched-range.bin"));
    const QByteArray stalePrefix(4096, 'z');
    QFile partial(savePath);
    QVERIFY(partial.open(QIODevice::WriteOnly));
    partial.write(stalePrefix);
    partial.close();

    QCNetworkResumableDownloadJob job(&manager,
                                      server.url(QStringLiteral("/range.bin")),
                                      savePath);
    QCOMPARE(job.reply(), nullptr);
    job.start();
    QTRY_VERIFY_WITH_TIMEOUT(job.reply() != nullptr, 1000);
    auto *reply = job.reply();
    QVERIFY(reply);
    QVERIFY(waitForFinished(reply));
    QVERIFY(job.isFinished());
    QCOMPARE(job.error(), NetworkError::InvalidRequest);
    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QVERIFY(reply->errorString().contains(QStringLiteral("Content-Range.start")));

    QFile finalFile(savePath);
    QVERIFY(finalFile.open(QIODevice::ReadOnly));
    const QByteArray finalData = finalFile.readAll();
    QCOMPARE(finalData, stalePrefix);

    reply->deleteLater();
}

void TestQCNetworkFileResumableOffline::testDestroyedReplyBeforeJobFinishedFailsJobOnce()
{
    QCNetworkAccessManager manager;
    QCNetworkMockHandler mock;
    mock.setGlobalDelay(60000);
    const QUrl url(QStringLiteral("http://resumable.test/destroy-before-finished"));
    mock.mockResponse(HttpMethod::Get, url, QByteArrayLiteral("late-body"));
    manager.setMockHandler(&mock);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QCNetworkResumableDownloadJob job(&manager,
                                      url,
                                      dir.filePath(QStringLiteral("target.bin")));
    QSignalSpy failedSpy(&job, &QCNetworkTransferJob::failed);
    QSignalSpy finishedSpy(&job, &QCNetworkTransferJob::finished);

    job.start();
    QTRY_VERIFY_WITH_TIMEOUT(job.reply() != nullptr, 1000);
    job.reply()->deleteLater();

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(job.error(), NetworkError::OperationCancelled);
    QVERIFY(job.errorString().contains(QStringLiteral("reply")));
    QCOMPARE(job.reply(), nullptr);

    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(failedSpy.count(), 1);
}

void TestQCNetworkFileResumableOffline::
    testDestroyedReplyAfterJobFinishedClearsWeakReferenceWithoutSecondTerminalSignal()
{
    QCNetworkAccessManager manager;

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const qint64 totalBytes = 16 * 1024;
    ResumableRangeServer server(totalBytes);
    QVERIFY2(server.start(), "Cannot bind local port for resumable offline test server");

    const QString savePath = dir.filePath(QStringLiteral("complete.bin"));
    QCNetworkResumableDownloadJob job(&manager,
                                      server.url(QStringLiteral("/range.bin")),
                                      savePath);
    QSignalSpy failedSpy(&job, &QCNetworkTransferJob::failed);
    QSignalSpy finishedSpy(&job, &QCNetworkTransferJob::finished);

    job.start();
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 2000);
    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(job.error(), NetworkError::NoError);
    QVERIFY(job.reply() != nullptr);

    job.reply()->deleteLater();
    QTRY_COMPARE_WITH_TIMEOUT(job.reply(), nullptr, 1000);
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(job.error(), NetworkError::NoError);
}

QTEST_MAIN(TestQCNetworkFileResumableOffline)

#include "tst_QCNetworkFileResumableOffline.moc"
