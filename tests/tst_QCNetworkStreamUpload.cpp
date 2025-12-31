/**
 * @file tst_QCNetworkStreamUpload.cpp
 * @brief 流式上传（uploadDevice/uploadFile）契约与分支覆盖测试
 *
 * 约束：
 * - 仅使用本进程内的 QTcpServer（listen 端口 0），不依赖外网/本地 httpbin。
 * - 端口绑定失败时使用 QSKIP（避免在受限环境阻断 ctest）。
 */

#include <QtTest/QtTest>

#include <QBuffer>
#include <QEventLoop>
#include <QHash>
#include <QHostAddress>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

#include <chrono>

#include "QCNetworkAccessManager.h"
#include "QCNetworkError.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "QCNetworkRetryPolicy.h"

using namespace QCurl;

namespace {

class UploadEchoServer final : public QObject
{
    Q_OBJECT

public:
    struct RequestRecord {
        QByteArray method;
        QByteArray path;
        QByteArray body;
    };

    explicit UploadEchoServer(QObject *parent = nullptr)
        : QObject(parent)
    {
        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (m_server.hasPendingConnections()) {
                QTcpSocket *socket = m_server.nextPendingConnection();
                if (!socket) {
                    continue;
                }
                handleSocket(socket);
            }
        });
    }

    bool start()
    {
        return m_server.listen(QHostAddress::LocalHost, 0);
    }

    QUrl url(const QString &path) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_server.serverPort()).arg(path));
    }

    bool requestHandled() const { return m_requestHandled; }
    int requestCount() const { return m_requests.size(); }
    RequestRecord request(int index) const
    {
        if (m_requests.isEmpty()) {
            return {};
        }
        if (index < 0) {
            index = requestCount() - 1;
        }
        if (index >= requestCount()) {
            return {};
        }
        return m_requests.at(index);
    }

    QByteArray method() const { return request(-1).method; }
    QByteArray path() const { return request(-1).path; }
    QByteArray body() const { return request(-1).body; }

    void setRespondEnabled(bool enabled) { m_respondEnabled = enabled; }
    void setReadBodyEnabled(bool enabled) { m_readBodyEnabled = enabled; }

private:
    struct ConnState {
        QByteArray recvBuf;
        int headerEnd = -1;
        qint64 contentLength = 0;
        bool hasContentLength = false;
        bool continueSent = false;
        bool requestRecorded = false;
        QByteArray method;
        QByteArray path;
    };

    static QByteArray normalizePath(QByteArray path)
    {
        const int q = path.indexOf('?');
        if (q >= 0) {
            path = path.left(q);
        }
        return path;
    }

    void handleSocket(QTcpSocket *socket)
    {
        QPointer<QTcpSocket> safeSocket(socket);
        socket->setParent(this);
        QObject::connect(socket, &QTcpSocket::readyRead, this, [this, safeSocket]() {
            if (!safeSocket) {
                return;
            }

            ConnState &state = m_states[safeSocket];

            // hang 模式：已解析头后不再读取 socket，形成 backpressure（避免测试依赖时序）
            if (state.headerEnd >= 0 && state.requestRecorded && !m_readBodyEnabled) {
                return;
            }

            state.recvBuf.append(safeSocket->readAll());

            if (state.headerEnd < 0) {
                state.headerEnd = state.recvBuf.indexOf("\r\n\r\n");
                if (state.headerEnd < 0) {
                    return;
                }

                const QByteArray headerBlock = state.recvBuf.left(state.headerEnd);
                const QList<QByteArray> lines = headerBlock.split('\n');
                if (!lines.isEmpty()) {
                    const QList<QByteArray> parts = lines[0].trimmed().split(' ');
                    if (parts.size() >= 2) {
                        state.method = parts[0].trimmed();
                        state.path = parts[1].trimmed();
                    }
                }

                bool expectContinue = false;
                for (int i = 1; i < lines.size(); ++i) {
                    const QByteArray line = lines[i].trimmed();
                    const QByteArray lower = line.toLower();
                    if (lower.startsWith("content-length:")) {
                        const QByteArray v = line.mid(static_cast<int>(QByteArray("Content-Length:").size())).trimmed();
                        bool ok = false;
                        const qint64 parsed = v.toLongLong(&ok);
                        if (ok && parsed >= 0) {
                            state.contentLength = parsed;
                            state.hasContentLength = true;
                        }
                    } else if (lower.startsWith("expect:") && lower.contains("100-continue")) {
                        expectContinue = true;
                    }
                }

                if (expectContinue && !state.continueSent) {
                    state.continueSent = true;
                    safeSocket->write("HTTP/1.1 100 Continue\r\n\r\n");
                    safeSocket->flush();
                }

                // hang 模式：只解析头，不继续读取 body，不响应
                if (!m_readBodyEnabled) {
                    m_requestHandled = true;
                    m_requests.append(RequestRecord{state.method, state.path, QByteArray()});
                    state.requestRecorded = true;
                    return;
                }
            }

            if (!state.hasContentLength) {
                return;
            }

            const int bodyOffset = state.headerEnd + 4;
            const int need = bodyOffset + static_cast<int>(state.contentLength);
            if (state.recvBuf.size() < need) {
                return;
            }

            const QByteArray body = state.recvBuf.mid(bodyOffset, state.contentLength);
            if (!state.requestRecorded) {
                m_requests.append(RequestRecord{state.method, state.path, body});
                state.requestRecorded = true;
                m_requestHandled = true;
            }

            if (!m_respondEnabled) {
                return;
            }

            const QByteArray path = normalizePath(state.path);
            if (path == QByteArrayLiteral("/redir_307_put") || path == QByteArrayLiteral("/redir_307_post")) {
                const QByteArray location =
                    (path.endsWith(QByteArrayLiteral("_put")) ? QByteArrayLiteral("/echo_put") : QByteArrayLiteral("/echo_post"));
                const QByteArray resp =
                    "HTTP/1.1 307 Temporary Redirect\r\n"
                    "Location: " + location + "\r\n"
                    "Content-Length: 0\r\n"
                    "Connection: close\r\n"
                    "\r\n";
                safeSocket->write(resp);
                safeSocket->flush();
                safeSocket->disconnectFromHost();
                return;
            }

            const QByteArray resp =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/octet-stream\r\n"
                "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
                "Connection: close\r\n"
                "\r\n" + body;

            safeSocket->write(resp);
            safeSocket->flush();
            safeSocket->disconnectFromHost();
        });
        QObject::connect(socket, &QObject::destroyed, this, [this, socket]() { m_states.remove(socket); });
    }

    QTcpServer m_server;
    bool m_requestHandled = false;
    QHash<QTcpSocket *, ConnState> m_states;
    QVector<RequestRecord> m_requests;
    bool m_respondEnabled = true;
    bool m_readBodyEnabled = true;
};

class SequentialUploadDevice final : public QIODevice
{
    Q_OBJECT

public:
    explicit SequentialUploadDevice(QByteArray data, QObject *parent = nullptr)
        : QIODevice(parent)
        , m_data(std::move(data))
    {
    }

    bool isSequential() const override { return true; }

    bool open(OpenMode mode) override
    {
        return QIODevice::open(mode);
    }

protected:
    qint64 readData(char *data, qint64 maxlen) override
    {
        if (m_pos >= m_data.size()) {
            return 0;
        }
        const qint64 n = qMin<qint64>(maxlen, m_data.size() - m_pos);
        memcpy(data, m_data.constData() + m_pos, static_cast<size_t>(n));
        m_pos += static_cast<int>(n);
        return n;
    }

    qint64 writeData(const char *, qint64) override
    {
        return -1;
    }

private:
    QByteArray m_data;
    int m_pos = 0;
};

class ReadFailUploadDevice final : public QIODevice
{
    Q_OBJECT

public:
    explicit ReadFailUploadDevice(QString error, QObject *parent = nullptr)
        : QIODevice(parent)
        , m_error(std::move(error))
    {
    }

    bool isSequential() const override { return true; }

protected:
    qint64 readData(char *, qint64) override
    {
        setErrorString(m_error);
        return -1;
    }

    qint64 writeData(const char *, qint64) override
    {
        return -1;
    }

private:
    QString m_error;
};

class CountingBuffer final : public QBuffer
{
    Q_OBJECT

public:
    explicit CountingBuffer(const QByteArray &data, QObject *parent = nullptr)
        : QBuffer(parent)
    {
        setData(data);
        open(QIODevice::ReadOnly);
    }

    void setMaxChunkBytes(qint64 bytes) { m_maxChunkBytes = bytes; }

    int readCalls() const { return m_readCalls; }
    qint64 bytesReadTotal() const { return m_bytesReadTotal; }

protected:
    qint64 readData(char *data, qint64 maxlen) override
    {
        ++m_readCalls;

        qint64 cap = maxlen;
        if (m_maxChunkBytes > 0) {
            cap = qMin(m_maxChunkBytes, maxlen);
        }

        const qint64 n = QBuffer::readData(data, cap);
        if (n > 0) {
            m_bytesReadTotal += n;
        }
        return n;
    }

private:
    int m_readCalls = 0;
    qint64 m_bytesReadTotal = 0;
    qint64 m_maxChunkBytes = 0;
};

class SeekFailBuffer final : public QBuffer
{
    Q_OBJECT

public:
    explicit SeekFailBuffer(const QByteArray &data, QObject *parent = nullptr)
        : QBuffer(parent)
    {
        setData(data);
        open(QIODevice::ReadOnly);
    }

    bool seek(qint64 pos) override
    {
        Q_UNUSED(pos);
        setErrorString(QStringLiteral("seek denied"));
        return false;
    }
};

static bool waitForFinished(QCNetworkReply *reply, int timeoutMs = 20000)
{
    if (!reply) {
        return false;
    }
    QSignalSpy spy(reply, &QCNetworkReply::finished);
    if (reply->isFinished()) {
        return true;
    }
    return spy.wait(timeoutMs);
}

} // namespace

class TestQCNetworkStreamUpload : public QObject
{
    Q_OBJECT

private slots:
    void testSeekablePutEchoAndOwnershipPreserved();
    void testSeekablePostEchoAndOwnershipPreserved();

    void testUploadDeviceThreadMismatchFailsFast();
    void testNonSeekableFollowLocationFailsFast();
    void testNonSeekableRetryPolicyFailsFast();
    void testSeekableRetryPreSeekFailureIsDiagnosable();
    void testSeekableFollowLocationSeekFailureIsDiagnosable();

    void testUploadDeviceCloseDuringTransferFails();
    void testUploadDeviceDestroyedDuringTransferFails();
    void testUploadDeviceReadFailureFails();
    void testCancelStopsFurtherDeviceReads();
};

void TestQCNetworkStreamUpload::testSeekablePutEchoAndOwnershipPreserved()
{
    UploadEchoServer server;
    if (!server.start()) {
        QSKIP("Cannot bind local port for stream upload test server");
    }

    QCNetworkAccessManager manager;

    QByteArray payload(64 * 1024, 'p');
    QBuffer buffer;
    buffer.setData(payload);
    QVERIFY(buffer.open(QIODevice::ReadOnly));

    QCNetworkRequest request(server.url(QStringLiteral("/echo_put")));
    request.setUploadDevice(&buffer, payload.size());
    request.setConnectTimeout(std::chrono::milliseconds(2000));
    request.setTimeout(std::chrono::milliseconds(8000));

    QCNetworkReply *reply = manager.sendPut(request, QByteArray());
    QVERIFY(reply);
    QVERIFY(waitForFinished(reply, 20000));

    QCOMPARE(reply->error(), NetworkError::NoError);
    QVERIFY(server.requestHandled());
    QCOMPARE(server.method(), QByteArrayLiteral("PUT"));
    QCOMPARE(server.body(), payload);

    const auto dataOpt = reply->readAll();
    QVERIFY(dataOpt.has_value());
    QCOMPARE(*dataOpt, payload);

    QVERIFY(buffer.isOpen());
    QVERIFY(buffer.seek(0));
    QCOMPARE(buffer.readAll(), payload);

    reply->deleteLater();
}

void TestQCNetworkStreamUpload::testSeekablePostEchoAndOwnershipPreserved()
{
    UploadEchoServer server;
    if (!server.start()) {
        QSKIP("Cannot bind local port for stream upload test server");
    }

    QCNetworkAccessManager manager;

    QByteArray payload(32 * 1024, 'q');
    QBuffer buffer;
    buffer.setData(payload);
    QVERIFY(buffer.open(QIODevice::ReadOnly));

    QCNetworkRequest request(server.url(QStringLiteral("/echo_post")));
    request.setUploadDevice(&buffer, payload.size());
    request.setConnectTimeout(std::chrono::milliseconds(2000));
    request.setTimeout(std::chrono::milliseconds(8000));

    QCNetworkReply *reply = manager.sendPost(request, QByteArray());
    QVERIFY(reply);
    QVERIFY(waitForFinished(reply, 20000));

    QCOMPARE(reply->error(), NetworkError::NoError);
    QVERIFY(server.requestHandled());
    QCOMPARE(server.method(), QByteArrayLiteral("POST"));
    QCOMPARE(server.body(), payload);

    const auto dataOpt = reply->readAll();
    QVERIFY(dataOpt.has_value());
    QCOMPARE(*dataOpt, payload);

    QVERIFY(buffer.isOpen());
    QVERIFY(buffer.seek(0));
    QCOMPARE(buffer.readAll(), payload);

    reply->deleteLater();
}

void TestQCNetworkStreamUpload::testUploadDeviceThreadMismatchFailsFast()
{
    QThread worker;
    worker.start();

    QObject ctx;
    ctx.moveToThread(&worker);

    QByteArray payload(1024, 't');
    QBuffer *device = nullptr;

    QMetaObject::invokeMethod(
        &ctx,
        [&]() {
            device = new QBuffer();
            device->setData(payload);
            device->open(QIODevice::ReadOnly);
        },
        Qt::BlockingQueuedConnection);

    QVERIFY(device != nullptr);
    QPointer<QBuffer> safeDevice(device);
    QVERIFY(device->isReadable());
    QCOMPARE(device->thread(), &worker);

    QCNetworkAccessManager manager;
    QCNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:1/")));
    request.setFollowLocation(false);
    request.setUploadDevice(device, payload.size());

    QCNetworkReply *reply = manager.sendPut(request, QByteArray());
    QVERIFY(reply);

    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QVERIFY2(reply->errorString().contains(QStringLiteral("同一线程")),
             "thread mismatch should be diagnosable");

    reply->deleteLater();

    QMetaObject::invokeMethod(&ctx, [device]() { device->deleteLater(); }, Qt::QueuedConnection);
    QTRY_VERIFY_WITH_TIMEOUT(!safeDevice, 2000);
    worker.quit();
    QVERIFY(worker.wait(2000));
}

void TestQCNetworkStreamUpload::testNonSeekableFollowLocationFailsFast()
{
    UploadEchoServer server;
    if (!server.start()) {
        QSKIP("Cannot bind local port for stream upload test server");
    }

    QCNetworkAccessManager manager;

    QByteArray payload(4096, 'n');
    SequentialUploadDevice device(payload);
    QVERIFY(device.open(QIODevice::ReadOnly));

    // followLocation 默认 true：当遇到需要重发 body 的 307 重定向时，non-seekable 应明确失败
    QCNetworkRequest request(server.url(QStringLiteral("/redir_307_put")));
    request.setUploadDevice(&device, payload.size());
    request.setConnectTimeout(std::chrono::milliseconds(2000));
    request.setTimeout(std::chrono::milliseconds(20000));

    QCNetworkReply *reply = manager.sendPut(request, QByteArray());
    QVERIFY(reply);
    QVERIFY(waitForFinished(reply, 20000));

    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QVERIFY2(reply->errorString().contains(QStringLiteral("重发 body")),
             "error should mention body replay");
    QVERIFY2(reply->errorString().contains(QStringLiteral("seek")),
             "error should mention seek/rewind");
    QCOMPARE(server.requestCount(), 1);
    QCOMPARE(server.request(0).path, QByteArrayLiteral("/redir_307_put"));

    reply->deleteLater();
}

void TestQCNetworkStreamUpload::testNonSeekableRetryPolicyFailsFast()
{
    QCNetworkAccessManager manager;

    QByteArray payload(4096, 'r');
    SequentialUploadDevice device(payload);
    QVERIFY(device.open(QIODevice::ReadOnly));

    QCNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:1/")));
    request.setFollowLocation(false);  // 避免被“自动重定向”约束抢先命中
    request.setRetryPolicy(QCNetworkRetryPolicy(1, 1));
    request.setUploadDevice(&device, payload.size());

    QCNetworkReply *reply = manager.sendPut(request, QByteArray());
    QVERIFY(reply);

    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QVERIFY2(reply->errorString().contains(QStringLiteral("重试")),
             "non-seekable body with retryPolicy must fail fast");

    reply->deleteLater();
}

void TestQCNetworkStreamUpload::testSeekableRetryPreSeekFailureIsDiagnosable()
{
    QCNetworkAccessManager manager;

    QByteArray payload(4096, 's');
    SeekFailBuffer buffer(payload, this);

    QCNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:1/")));
    request.setFollowLocation(false);
    request.setRetryPolicy(QCNetworkRetryPolicy(1, 1));
    request.setUploadDevice(&buffer, payload.size());
    request.setConnectTimeout(std::chrono::milliseconds(1000));
    request.setTimeout(std::chrono::milliseconds(5000));

    QCNetworkReply *reply = manager.sendPut(request, QByteArray());
    QVERIFY(reply);
    QVERIFY(waitForFinished(reply, 20000));

    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QVERIFY2(reply->errorString().contains(QStringLiteral("重试")),
             "pre-seek failure should mention retry");
    QVERIFY2(reply->errorString().contains(QStringLiteral("重发 body")),
             "pre-seek failure should mention body replay");
    QVERIFY2(reply->errorString().contains(QStringLiteral("seek")),
             "pre-seek failure should mention seek/rewind");

    reply->deleteLater();
}

void TestQCNetworkStreamUpload::testSeekableFollowLocationSeekFailureIsDiagnosable()
{
    UploadEchoServer server;
    if (!server.start()) {
        QSKIP("Cannot bind local port for stream upload test server");
    }

    QCNetworkAccessManager manager;

    QByteArray payload(4096, 'f');
    SeekFailBuffer buffer(payload, this);

    QCNetworkRequest request(server.url(QStringLiteral("/redir_307_put")));
    request.setUploadDevice(&buffer, payload.size());
    request.setConnectTimeout(std::chrono::milliseconds(2000));
    request.setTimeout(std::chrono::milliseconds(15000));

    QCNetworkReply *reply = manager.sendPut(request, QByteArray());
    QVERIFY(reply);
    QVERIFY(waitForFinished(reply, 20000));

    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QVERIFY2(reply->errorString().contains(QStringLiteral("重发 body")),
             "seek failure should mention body replay");
    QVERIFY2(reply->errorString().contains(QStringLiteral("seek")),
             "seek failure should mention seek/rewind");
    QCOMPARE(server.requestCount(), 1);
    QCOMPARE(server.request(0).path, QByteArrayLiteral("/redir_307_put"));

    reply->deleteLater();
}

void TestQCNetworkStreamUpload::testUploadDeviceCloseDuringTransferFails()
{
    UploadEchoServer server;
    if (!server.start()) {
        QSKIP("Cannot bind local port for stream upload test server");
    }

    QCNetworkAccessManager manager;

    QByteArray payload(512 * 1024, 'c');
    auto *buffer = new CountingBuffer(payload, this);
    buffer->setMaxChunkBytes(1024);

    QCNetworkRequest request(server.url(QStringLiteral("/close_during_upload")));
    request.setFollowLocation(false);
    request.setUploadDevice(buffer, payload.size());
    request.setConnectTimeout(std::chrono::milliseconds(2000));
    request.setTimeout(std::chrono::milliseconds(15000));

    QCNetworkReply *reply = manager.sendPut(request, QByteArray());
    QVERIFY(reply);

    bool closed = false;
    QObject::connect(reply, &QCNetworkReply::uploadProgress, this, [&](qint64 sent, qint64 /*total*/) {
        if (!closed && sent > 0) {
            closed = true;
            buffer->close();
        }
    });

    QVERIFY(waitForFinished(reply, 20000));
    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QVERIFY2(reply->errorString().contains(QStringLiteral("不可读")),
             "closing device during upload should be diagnosable");

    reply->deleteLater();
}

void TestQCNetworkStreamUpload::testUploadDeviceDestroyedDuringTransferFails()
{
    UploadEchoServer server;
    if (!server.start()) {
        QSKIP("Cannot bind local port for stream upload test server");
    }

    QCNetworkAccessManager manager;

    QByteArray payload(512 * 1024, 'd');
    auto *buffer = new CountingBuffer(payload, this);
    buffer->setMaxChunkBytes(1024);
    QPointer<CountingBuffer> safeBuffer(buffer);

    QCNetworkRequest request(server.url(QStringLiteral("/destroy_during_upload")));
    request.setFollowLocation(false);
    request.setUploadDevice(buffer, payload.size());
    request.setConnectTimeout(std::chrono::milliseconds(2000));
    request.setTimeout(std::chrono::milliseconds(15000));

    QCNetworkReply *reply = manager.sendPut(request, QByteArray());
    QVERIFY(reply);

    bool destroyed = false;
    QObject::connect(reply, &QCNetworkReply::uploadProgress, this, [&](qint64 sent, qint64 /*total*/) {
        if (!destroyed && sent > 0 && safeBuffer) {
            destroyed = true;
            delete safeBuffer.data();
        }
    });

    QVERIFY(waitForFinished(reply, 20000));
    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QVERIFY2(reply->errorString().contains(QStringLiteral("被销毁")),
             "destroying device during upload should be diagnosable");
    QVERIFY(!safeBuffer);

    reply->deleteLater();
}

void TestQCNetworkStreamUpload::testUploadDeviceReadFailureFails()
{
    UploadEchoServer server;
    if (!server.start()) {
        QSKIP("Cannot bind local port for stream upload test server");
    }

    QCNetworkAccessManager manager;

    ReadFailUploadDevice device(QStringLiteral("boom"));
    QVERIFY(device.open(QIODevice::ReadOnly));

    QCNetworkRequest request(server.url(QStringLiteral("/read_fail")));
    request.setFollowLocation(false);
    request.setUploadDevice(&device, 4096);
    request.setConnectTimeout(std::chrono::milliseconds(2000));
    request.setTimeout(std::chrono::milliseconds(15000));

    QCNetworkReply *reply = manager.sendPut(request, QByteArray());
    QVERIFY(reply);
    QVERIFY(waitForFinished(reply, 20000));

    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QVERIFY2(reply->errorString().contains(QStringLiteral("读取失败")),
             "read failure should be diagnosable");
    QVERIFY2(reply->errorString().contains(QStringLiteral("boom")),
             "should include device errorString");

    reply->deleteLater();
}

void TestQCNetworkStreamUpload::testCancelStopsFurtherDeviceReads()
{
    UploadEchoServer server;
    if (!server.start()) {
        QSKIP("Cannot bind local port for stream upload test server");
    }

    // hang：不读 body、不响应 → 形成 backpressure，便于触发 cancel
    server.setReadBodyEnabled(false);
    server.setRespondEnabled(false);

    QCNetworkAccessManager manager;

    QByteArray payload(1024 * 1024, 'x');
    auto *buffer = new CountingBuffer(payload, this);
    buffer->setMaxChunkBytes(1024);

    QCNetworkRequest request(server.url(QStringLiteral("/hang")));
    request.setFollowLocation(false);
    request.setUploadDevice(buffer, payload.size());
    request.setConnectTimeout(std::chrono::milliseconds(2000));
    request.setTimeout(std::chrono::milliseconds(30000));

    QCNetworkReply *reply = manager.sendPut(request, QByteArray());
    QVERIFY(reply);

    QTRY_VERIFY_WITH_TIMEOUT(buffer->readCalls() > 0, 2000);

    const int readsBefore = buffer->readCalls();
    const qint64 bytesBefore = buffer->bytesReadTotal();

    reply->cancel();
    QCOMPARE(reply->state(), ReplyState::Cancelled);
    QCOMPARE(reply->error(), NetworkError::OperationCancelled);

    // 等待 queued removeReply 生效 + 确保不会继续读取 device
    QTest::qWait(200);
    QCOMPARE(buffer->readCalls(), readsBefore);
    QCOMPARE(buffer->bytesReadTotal(), bytesBefore);

    QVERIFY(waitForFinished(reply, 20000));

    reply->deleteLater();
}

QTEST_MAIN(TestQCNetworkStreamUpload)
#include "tst_QCNetworkStreamUpload.moc"
