#include "private/QCWebSocketSendQueue_p.h"

#include <QtTest>

using QCurl::Internal::QCWebSocketSendQueue;

class TestQCWebSocketSendQueue : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void partialSendAndAgainPreserveOffset();
    void noProgressAndInvalidSentFail();
    void closePrioritizesUnsentFrames();
    void closeWaitsForPartiallySentFrame();
    void pendingLimitPreservesExistingQueue();
    void emptyFrameStillInvokesSender();
};

void TestQCWebSocketSendQueue::partialSendAndAgainPreserveOffset()
{
    QCWebSocketSendQueue queue;
    QVERIFY(queue.enqueue(QByteArrayLiteral("abcdef"), CURLWS_TEXT, false, 64));

    int callCount = 0;
    QList<QByteArray> payloads;
    QList<unsigned int> sentFlags;
    const auto sender = [&callCount, &payloads, &sentFlags](const char *data,
                                                            size_t size,
                                                            size_t *sent,
                                                            unsigned int flags) {
        payloads.append(QByteArray(data, static_cast<qsizetype>(size)));
        sentFlags.append(flags);
        if (callCount == 0) {
            *sent = 2;
            ++callCount;
            return CURLE_OK;
        }
        if (callCount == 1) {
            *sent = 1;
            ++callCount;
            return CURLE_AGAIN;
        }
        *sent = size;
        ++callCount;
        return CURLE_OK;
    };

    QCOMPARE(queue.flushOne(sender).status, QCWebSocketSendQueue::FlushStatus::Progress);
    QCOMPARE(payloads.last(), QByteArrayLiteral("abcdef"));
    QCOMPARE(queue.pendingBytes(), 4);
    QCOMPARE(queue.flushOne(sender).status, QCWebSocketSendQueue::FlushStatus::WouldBlock);
    QCOMPARE(payloads.last(), QByteArrayLiteral("cdef"));
    QCOMPARE(queue.pendingBytes(), 3);
    QCOMPARE(queue.flushOne(sender).status, QCWebSocketSendQueue::FlushStatus::FrameCompleted);
    QCOMPARE(payloads.last(), QByteArrayLiteral("def"));
    QCOMPARE(callCount, 3);
    QCOMPARE(sentFlags, QList<unsigned int>({CURLWS_TEXT, CURLWS_TEXT, CURLWS_TEXT}));
    QVERIFY(queue.isEmpty());
    QCOMPARE(queue.pendingBytes(), 0);
}

void TestQCWebSocketSendQueue::noProgressAndInvalidSentFail()
{
    QCWebSocketSendQueue queue;
    QVERIFY(queue.enqueue(QByteArrayLiteral("abc"), CURLWS_BINARY, false, 64));

    auto result = queue.flushOne([](const char *, size_t, size_t *sent, unsigned int) {
        *sent = 0;
        return CURLE_OK;
    });
    QCOMPARE(result.status, QCWebSocketSendQueue::FlushStatus::Error);
    QVERIFY(result.error.contains(QStringLiteral("没有推进")));
    QCOMPARE(queue.pendingBytes(), 3);

    queue.clear();
    QVERIFY(queue.enqueue(QByteArrayLiteral("abc"), CURLWS_BINARY, false, 64));
    result = queue.flushOne([](const char *, size_t size, size_t *sent, unsigned int) {
        *sent = size + 1;
        return CURLE_OK;
    });
    QCOMPARE(result.status, QCWebSocketSendQueue::FlushStatus::Error);
    QVERIFY(result.error.contains(QStringLiteral("超出请求范围")));
    QCOMPARE(queue.pendingBytes(), 3);
}

void TestQCWebSocketSendQueue::closePrioritizesUnsentFrames()
{
    QCWebSocketSendQueue queue;
    QVERIFY(queue.enqueue(QByteArrayLiteral("first"), CURLWS_TEXT, false, 64));
    QVERIFY(queue.enqueue(QByteArrayLiteral("second"), CURLWS_TEXT, false, 64));

    const QByteArray closePayload = QByteArray::fromHex("03e8");
    QVERIFY(queue.enqueue(closePayload, CURLWS_CLOSE, true, 1));
    QCOMPARE(queue.size(), 1);
    QCOMPARE(queue.pendingBytes(), closePayload.size());
    QVERIFY(queue.hasCloseFrame());

    QByteArray sentPayload;
    unsigned int sentFlag = 0;
    const auto result     = queue.flushOne(
        [&sentPayload, &sentFlag](const char *data, size_t size, size_t *sent, unsigned int flags) {
            sentPayload = QByteArray(data, static_cast<qsizetype>(size));
            sentFlag    = flags;
            *sent       = size;
            return CURLE_OK;
        });
    QCOMPARE(result.status, QCWebSocketSendQueue::FlushStatus::CloseCompleted);
    QCOMPARE(sentFlag, static_cast<unsigned int>(CURLWS_CLOSE));
    QCOMPARE(sentPayload, closePayload);
    QVERIFY(queue.isEmpty());
}

void TestQCWebSocketSendQueue::closeWaitsForPartiallySentFrame()
{
    QCWebSocketSendQueue queue;
    QVERIFY(queue.enqueue(QByteArrayLiteral("abcdef"), CURLWS_TEXT, false, 64));
    QCOMPARE(queue
                 .flushOne([](const char *, size_t, size_t *sent, unsigned int) {
                     *sent = 2;
                     return CURLE_OK;
                 })
                 .status,
             QCWebSocketSendQueue::FlushStatus::Progress);

    QVERIFY(queue.enqueue(QByteArrayLiteral("discard"), CURLWS_TEXT, false, 64));
    const QByteArray closePayload = QByteArray::fromHex("03e8");
    QVERIFY(queue.enqueue(closePayload, CURLWS_CLOSE, true, 64));
    QCOMPARE(queue.size(), 2);
    QCOMPARE(queue.pendingBytes(), 4 + closePayload.size());

    QByteArray sentPayload;
    unsigned int sentFlag = 0;
    auto result           = queue.flushOne(
        [&sentPayload, &sentFlag](const char *data, size_t size, size_t *sent, unsigned int flags) {
            sentPayload = QByteArray(data, static_cast<qsizetype>(size));
            sentFlag    = flags;
            *sent       = size;
            return CURLE_OK;
        });
    QCOMPARE(result.status, QCWebSocketSendQueue::FlushStatus::FrameCompleted);
    QCOMPARE(sentFlag, static_cast<unsigned int>(CURLWS_TEXT));
    QCOMPARE(sentPayload, QByteArrayLiteral("cdef"));

    result = queue.flushOne(
        [&sentPayload, &sentFlag](const char *data, size_t size, size_t *sent, unsigned int flags) {
            sentPayload = QByteArray(data, static_cast<qsizetype>(size));
            sentFlag    = flags;
            *sent       = size;
            return CURLE_OK;
        });
    QCOMPARE(result.status, QCWebSocketSendQueue::FlushStatus::CloseCompleted);
    QCOMPARE(sentFlag, static_cast<unsigned int>(CURLWS_CLOSE));
    QCOMPARE(sentPayload, closePayload);
    QVERIFY(queue.isEmpty());
}

void TestQCWebSocketSendQueue::pendingLimitPreservesExistingQueue()
{
    QCWebSocketSendQueue queue;
    QVERIFY(queue.enqueue(QByteArrayLiteral("1234"), CURLWS_BINARY, false, 4));

    QString error;
    QVERIFY(!queue.enqueue(QByteArrayLiteral("5"), CURLWS_BINARY, false, 4, &error));
    QVERIFY(error.contains(QStringLiteral("超过配置上限")));
    QCOMPARE(queue.size(), 1);
    QCOMPARE(queue.pendingBytes(), 4);
}

void TestQCWebSocketSendQueue::emptyFrameStillInvokesSender()
{
    QCWebSocketSendQueue queue;
    QVERIFY(queue.enqueue({}, CURLWS_TEXT, false, 4));

    bool invoked          = false;
    size_t requestedSize  = 1;
    unsigned int sentFlag = 0;
    const auto result = queue.flushOne([&invoked, &requestedSize, &sentFlag](const char *,
                                                                             size_t size,
                                                                             size_t *sent,
                                                                             unsigned int flags) {
        invoked       = true;
        requestedSize = size;
        sentFlag      = flags;
        *sent         = 0;
        return CURLE_OK;
    });
    QVERIFY(invoked);
    QCOMPARE(requestedSize, size_t{0});
    QCOMPARE(sentFlag, static_cast<unsigned int>(CURLWS_TEXT));
    QCOMPARE(result.status, QCWebSocketSendQueue::FlushStatus::FrameCompleted);
    QVERIFY(queue.isEmpty());
}

QTEST_GUILESS_MAIN(TestQCWebSocketSendQueue)

#include "tst_QCWebSocketSendQueue.moc"
