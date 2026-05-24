/** @file tst_QCNetworkMultipartUpload.cpp
 *  @brief Multipart streaming body contract regression tests. */

#include "QCNetworkAccessManager.h"
#include "QCNetworkError.h"
#include "QCMultipartFormData.h"
#include "QCNetworkMultipartBody.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#define protected public
#include "private/QCSingleFileMultipartBodyDevice.h"
#undef protected

#include <QBuffer>
#include <QHostAddress>
#include <QPointer>
#include <QSignalSpy>
#include <QThread>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest/QtTest>

#include <cstring>

using namespace QCurl;

namespace {

class MultipartCaptureServer final : public QObject
{
public:
    struct RequestRecord
    {
        QByteArray method;
        QByteArray path;
        QByteArray body;
    };

    explicit MultipartCaptureServer(QObject *parent = nullptr)
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

    bool start() { return m_server.listen(QHostAddress::LocalHost, 0); }
    QUrl url(const QString &path) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_server.serverPort()).arg(path));
    }

    bool requestHandled() const { return m_requestHandled; }
    QByteArray body() const { return m_request.body; }

private:
    struct ConnState
    {
        QByteArray recvBuf;
        int headerEnd         = -1;
        qint64 contentLength  = 0;
        bool hasContentLength = false;
        bool requestRecorded  = false;
        QByteArray method;
        QByteArray path;
    };

    void handleSocket(QTcpSocket *socket)
    {
        QPointer<QTcpSocket> safeSocket(socket);
        socket->setParent(this);
        QObject::connect(socket, &QTcpSocket::readyRead, this, [this, safeSocket]() {
            if (!safeSocket) {
                return;
            }

            ConnState &state = m_states[safeSocket];

            state.recvBuf.append(safeSocket->readAll());

            if (state.headerEnd < 0) {
                state.headerEnd = state.recvBuf.indexOf("\r\n\r\n");
                if (state.headerEnd < 0) {
                    return;
                }

                const QByteArray headerBlock  = state.recvBuf.left(state.headerEnd);
                const QList<QByteArray> lines = headerBlock.split('\n');
                if (!lines.isEmpty()) {
                    const QList<QByteArray> parts = lines[0].trimmed().split(' ');
                    if (parts.size() >= 2) {
                        state.method = parts[0].trimmed();
                        state.path   = parts[1].trimmed();
                    }
                }

                for (int i = 1; i < lines.size(); ++i) {
                    const QByteArray line  = lines[i].trimmed();
                    const QByteArray lower = line.toLower();
                    if (!lower.startsWith("content-length:")) {
                        continue;
                    }

                    const QByteArray value
                        = line.mid(static_cast<int>(QByteArray("Content-Length:").size()))
                              .trimmed();
                    bool ok = false;
                    const qint64 parsed = value.toLongLong(&ok);
                    if (ok && parsed >= 0) {
                        state.contentLength  = parsed;
                        state.hasContentLength = true;
                    }
                }

            }

            if (!state.hasContentLength) {
                return;
            }

            const int bodyOffset = state.headerEnd + 4;
            const int totalSize  = bodyOffset + static_cast<int>(state.contentLength);
            if (state.recvBuf.size() < totalSize) {
                return;
            }

            if (!state.requestRecorded) {
                m_requestHandled = true;
                m_request = RequestRecord{
                    state.method,
                    state.path,
                    state.recvBuf.mid(bodyOffset, state.contentLength),
                };
                state.requestRecorded = true;
            }

            const QByteArray response = "HTTP/1.1 200 OK\r\n"
                                        "Content-Type: application/octet-stream\r\n"
                                        "Content-Length: "
                                        + QByteArray::number(m_request.body.size())
                                        + "\r\n"
                                          "Connection: close\r\n"
                                          "\r\n"
                                        + m_request.body;
            safeSocket->write(response);
            safeSocket->flush();
            safeSocket->disconnectFromHost();
        });
        QObject::connect(socket, &QObject::destroyed, this, [this, socket]() {
            m_states.remove(socket);
        });
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, ConnState> m_states;
    RequestRecord m_request;
    bool m_requestHandled = false;
};

class SequentialUploadDevice final : public QIODevice
{
public:
    explicit SequentialUploadDevice(QByteArray data, QObject *parent = nullptr)
        : QIODevice(parent)
        , m_data(std::move(data))
    {}

    bool isSequential() const override { return true; }
    bool open(OpenMode mode) override { return QIODevice::open(mode); }

protected:
    qint64 readData(char *data, qint64 maxlen) override
    {
        if (m_pos >= m_data.size()) {
            return 0;
        }
        const qint64 chunk = qMin<qint64>(maxlen, m_data.size() - m_pos);
        std::memcpy(data, m_data.constData() + m_pos, static_cast<size_t>(chunk));
        m_pos += static_cast<int>(chunk);
        return chunk;
    }

    qint64 writeData(const char *, qint64) override { return -1; }

private:
    QByteArray m_data;
    int m_pos = 0;
};

class CountingSeekableDevice final : public QIODevice
{
public:
    explicit CountingSeekableDevice(QByteArray data, QObject *parent = nullptr)
        : QIODevice(parent)
        , m_data(std::move(data))
    {
        open(QIODevice::ReadOnly);
    }

    void setMaxChunkBytes(qint64 bytes) { m_maxChunkBytes = bytes; }

    int readCalls() const { return m_readCalls; }
    qint64 bytesReadTotal() const { return m_bytesReadTotal; }
    qint64 pos() const override { return m_pos; }
    qint64 size() const override { return m_data.size(); }
    bool isSequential() const override { return false; }

    bool seek(qint64 pos) override
    {
        if (pos < 0 || pos > m_data.size()) {
            return false;
        }
        m_pos = pos;
        return QIODevice::seek(pos);
    }

protected:
    qint64 readData(char *data, qint64 maxlen) override
    {
        ++m_readCalls;

        if (m_pos >= m_data.size()) {
            return 0;
        }

        qint64 chunk = qMin(maxlen, m_data.size() - m_pos);
        if (m_maxChunkBytes > 0) {
            chunk = qMin(chunk, m_maxChunkBytes);
        }

        std::memcpy(data, m_data.constData() + m_pos, static_cast<size_t>(chunk));
        m_pos += chunk;
        m_bytesReadTotal += chunk;
        return chunk;
    }

    qint64 writeData(const char *, qint64) override { return -1; }

private:
    QByteArray m_data;
    int m_readCalls = 0;
    qint64 m_bytesReadTotal = 0;
    qint64 m_maxChunkBytes = 0;
    qint64 m_pos = 0;
};

static bool waitForFinished(QCNetworkReply *reply, int timeoutMs = 20000)
{
    if (!reply) {
        return false;
    }
    if (reply->isFinished()) {
        return true;
    }

    QSignalSpy spy(reply, &QCNetworkReply::finished);
    return spy.wait(timeoutMs);
}

static QCNetworkReply *sendMultipartDevice(QCNetworkAccessManager &manager,
                                           const QUrl &url,
                                           const QString &fieldName,
                                           QIODevice *device,
                                           const QString &fileName,
                                           const QString &mimeType,
                                           std::optional<qint64> sizeBytes)
{
    QCNetworkRequest request(url);
    if (!sizeBytes.has_value()) {
        auto *reply = manager.post(request, QByteArray());
        reply->abortWithError(
            NetworkError::InvalidRequest,
            QStringLiteral("QCNetworkMultipartBody: 单文件 multipart 要求已知长度且设备可 seek"));
        return reply;
    }

    QString error;
    auto body = QCNetworkMultipartBody::fromSingleFileDevice(device,
                                                            fieldName,
                                                            fileName,
                                                            mimeType,
                                                            sizeBytes,
                                                            &error);
    if (!body.has_value()) {
        auto *reply = manager.post(request, QByteArray());
        reply->abortWithError(NetworkError::InvalidRequest, error);
        return reply;
    }

    request.setRawHeader(QByteArrayLiteral("Content-Type"), body->contentType());
    auto *bodyDevice = body->takeDevice(nullptr, &error);
    if (!bodyDevice) {
        auto *reply = manager.post(request, QByteArray());
        reply->abortWithError(NetworkError::InvalidRequest, error);
        return reply;
    }
    auto *reply = manager.post(request, bodyDevice, body->sizeBytes());
    bodyDevice->setParent(reply);
    return reply;
}

} // namespace

class TestQCNetworkMultipartUpload : public QObject
{
    Q_OBJECT

private slots:
    void testMultipartDeviceSequentialFailsFast();
    void testMultipartDeviceUnknownSizeFailsFast();
    void testMultipartBodyRejectsNullDevice();
    void testMultipartBodyRejectsUnreadableDevice();
    void testMultipartBodyRejectsNegativeSize();
    void testMultipartBodyTakeDeviceRejectsCrossThreadParent();
    void testMultipartBodyTakeDeviceReportsNonStreamingBody();
    void testMultipartBodyTakeDeviceReportsDuplicateTake();
    void testMultipartDeviceStartsReadingFromCurrentPosition();
    void testMultipartBodyDeviceDoesNotPrebufferWholeSource();
    void testMultipartBodyDeviceFailsWhenSourceEndsEarly();
    void testMultipartBodyDeviceEscapesHeaderParameters();
};
void TestQCNetworkMultipartUpload::testMultipartDeviceSequentialFailsFast()
{
    QCNetworkAccessManager manager;

    SequentialUploadDevice device(QByteArray(4096, 's'));
    QVERIFY(device.open(QIODevice::ReadOnly));

    auto *reply = sendMultipartDevice(manager,
                                      QUrl(QStringLiteral("http://127.0.0.1:1/upload")),
                                      QStringLiteral("file"),
                                      &device,
                                      QStringLiteral("seq.bin"),
                                      QStringLiteral("application/octet-stream"),
                                      4096);
    QVERIFY(reply);
    QVERIFY(waitForFinished(reply));

    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QVERIFY(reply->errorString().contains(QStringLiteral("已知长度且设备可 seek")));

    reply->deleteLater();
}

void TestQCNetworkMultipartUpload::testMultipartDeviceUnknownSizeFailsFast()
{
    QCNetworkAccessManager manager;

    QByteArray payload(4096, 'u');
    QBuffer buffer(&payload);
    QVERIFY(buffer.open(QIODevice::ReadOnly));

    auto *reply = sendMultipartDevice(manager,
                                      QUrl(QStringLiteral("http://127.0.0.1:1/upload")),
                                      QStringLiteral("file"),
                                      &buffer,
                                      QStringLiteral("unknown.bin"),
                                      QStringLiteral("application/octet-stream"),
                                      std::nullopt);
    QVERIFY(reply);
    QVERIFY(waitForFinished(reply));

    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QVERIFY(reply->errorString().contains(QStringLiteral("已知长度且设备可 seek")));

    reply->deleteLater();
}

void TestQCNetworkMultipartUpload::testMultipartBodyRejectsNullDevice()
{
    QString error;
    const auto body = QCNetworkMultipartBody::fromSingleFileDevice(nullptr,
                                                                   QStringLiteral("file"),
                                                                   QStringLiteral("payload.bin"),
                                                                   QStringLiteral("text/plain"),
                                                                   qint64(7),
                                                                   &error);

    QVERIFY(!body.has_value());
    QVERIFY(error.contains(QStringLiteral("QIODevice 为空")));
}

void TestQCNetworkMultipartUpload::testMultipartBodyRejectsUnreadableDevice()
{
    QByteArray payload("payload");
    QBuffer buffer(&payload);

    QString error;
    const auto body = QCNetworkMultipartBody::fromSingleFileDevice(&buffer,
                                                                   QStringLiteral("file"),
                                                                   QStringLiteral("payload.bin"),
                                                                   QStringLiteral("text/plain"),
                                                                   payload.size(),
                                                                   &error);

    QVERIFY(!body.has_value());
    QVERIFY(error.contains(QStringLiteral("不可读")));
}

void TestQCNetworkMultipartUpload::testMultipartBodyRejectsNegativeSize()
{
    QByteArray payload("payload");
    QBuffer buffer(&payload);
    QVERIFY(buffer.open(QIODevice::ReadOnly));

    QString error;
    const auto body = QCNetworkMultipartBody::fromSingleFileDevice(&buffer,
                                                                   QStringLiteral("file"),
                                                                   QStringLiteral("payload.bin"),
                                                                   QStringLiteral("text/plain"),
                                                                   qint64(-1),
                                                                   &error);

    QVERIFY(!body.has_value());
    QVERIFY(error.contains(QStringLiteral("不能为负数")));
}

void TestQCNetworkMultipartUpload::testMultipartBodyTakeDeviceRejectsCrossThreadParent()
{
    QByteArray payload("payload");
    QBuffer buffer(&payload);
    QVERIFY(buffer.open(QIODevice::ReadOnly));

    QString error;
    auto body = QCNetworkMultipartBody::fromSingleFileDevice(&buffer,
                                                            QStringLiteral("file"),
                                                            QStringLiteral("payload.bin"),
                                                            QStringLiteral("text/plain"),
                                                            payload.size(),
                                                            &error);
    QVERIFY2(body.has_value(), qPrintable(error));

    QThread parentThread;
    parentThread.start();
    QObject parent;
    parent.moveToThread(&parentThread);

    QCOMPARE(body->takeDevice(&parent), nullptr);

    auto *device = body->takeDevice(this);
    QVERIFY(device != nullptr);
    QCOMPARE(device->parent(), this);

    QMetaObject::invokeMethod(&parent,
                              [&parent]() { parent.moveToThread(QThread::currentThread()); },
                              Qt::BlockingQueuedConnection);
    parentThread.quit();
    QVERIFY(parentThread.wait(1000));
}

void TestQCNetworkMultipartUpload::testMultipartBodyTakeDeviceReportsNonStreamingBody()
{
    QCMultipartFormData formData;
    formData.addTextField(QStringLiteral("name"), QStringLiteral("value"));
    auto body = QCNetworkMultipartBody::fromFormData(formData);

    QString error;
    QCOMPARE(body.takeDevice(this, &error), nullptr);
    QVERIFY(error.contains(QStringLiteral("非流式")));
}

void TestQCNetworkMultipartUpload::testMultipartBodyTakeDeviceReportsDuplicateTake()
{
    QByteArray payload("payload");
    QBuffer buffer(&payload);
    QVERIFY(buffer.open(QIODevice::ReadOnly));

    QString error;
    auto body = QCNetworkMultipartBody::fromSingleFileDevice(&buffer,
                                                            QStringLiteral("file"),
                                                            QStringLiteral("payload.bin"),
                                                            QStringLiteral("text/plain"),
                                                            payload.size(),
                                                            &error);
    QVERIFY2(body.has_value(), qPrintable(error));

    auto *device = body->takeDevice(this, &error);
    QVERIFY2(device != nullptr, qPrintable(error));
    error.clear();

    QCOMPARE(body->takeDevice(this, &error), nullptr);
    QVERIFY(error.contains(QStringLiteral("已转移")));
}

void TestQCNetworkMultipartUpload::testMultipartDeviceStartsReadingFromCurrentPosition()
{
    MultipartCaptureServer server;
    QVERIFY2(server.start(), "Cannot bind local port for multipart upload test server");

    QCNetworkAccessManager manager;

    const QByteArray skippedPrefix("prefix-that-must-stay-unread");
    const QByteArray payload = skippedPrefix + QByteArray(8192, 'p');

    QBuffer buffer;
    buffer.setData(payload);
    QVERIFY(buffer.open(QIODevice::ReadOnly));
    QVERIFY(buffer.seek(skippedPrefix.size()));

    const qint64 remainingBytes = payload.size() - skippedPrefix.size();

    auto *reply = sendMultipartDevice(manager,
                                      server.url(QStringLiteral("/upload")),
                                      QStringLiteral("file"),
                                      &buffer,
                                      QStringLiteral("tail.bin"),
                                      QStringLiteral("application/octet-stream"),
                                      remainingBytes);
    QVERIFY(reply);
    QVERIFY(waitForFinished(reply));

    QCOMPARE(reply->error(), NetworkError::NoError);
    QVERIFY(server.requestHandled());
    QVERIFY(server.body().contains(payload.mid(skippedPrefix.size())));
    QVERIFY(!server.body().contains(skippedPrefix));

    reply->deleteLater();
}

void TestQCNetworkMultipartUpload::testMultipartBodyDeviceDoesNotPrebufferWholeSource()
{
    const QString boundary = QStringLiteral("----QCurlBoundarySmoke0123456789");
    const QString fieldName = QStringLiteral("file");
    const QString fileName = QStringLiteral("large.bin");
    const QString mimeType = QStringLiteral("application/octet-stream");

    QByteArray payload(4 * 1024 * 1024, 'm');
    const qint64 sourceReadWindow = 4096;
    auto *source = new CountingSeekableDevice(payload, this);
    source->setMaxChunkBytes(sourceReadWindow);

    const qint64 sourceBasePos = 1536;
    QVERIFY(source->seek(sourceBasePos));

    const qint64 sourceBytes = payload.size() - sourceBasePos;
    Internal::QCSingleFileMultipartBodyDevice bodyDevice(boundary,
                                                         fieldName,
                                                         source,
                                                         fileName,
                                                         mimeType,
                                                         sourceBytes,
                                                         this);

    const QByteArray prefix = QStringLiteral("--%1\r\n"
                                             "Content-Disposition: form-data; name=\"%2\"; "
                                             "filename=\"%3\"\r\n"
                                             "Content-Type: %4\r\n"
                                             "\r\n")
                                  .arg(boundary, fieldName, fileName, mimeType)
                                  .toUtf8();
    const qint64 requestedBodyBytes = 512;
    QByteArray firstChunk(static_cast<int>(prefix.size() + requestedBodyBytes), Qt::Uninitialized);

    const qint64 n = bodyDevice.readData(firstChunk.data(), firstChunk.size());
    QCOMPARE(n, static_cast<qint64>(firstChunk.size()));
    QCOMPARE(firstChunk.left(prefix.size()), prefix);
    QCOMPARE(firstChunk.mid(prefix.size()), payload.mid(sourceBasePos, requestedBodyBytes));

    QVERIFY(source->bytesReadTotal() >= requestedBodyBytes);
    QVERIFY(source->bytesReadTotal() <= sourceReadWindow);
    QVERIFY(source->bytesReadTotal() < sourceBytes);
    QCOMPARE(source->pos(), sourceBasePos + source->bytesReadTotal());
    QVERIFY(source->readCalls() >= 1);
}

void TestQCNetworkMultipartUpload::testMultipartBodyDeviceFailsWhenSourceEndsEarly()
{
    const QString boundary = QStringLiteral("----QCurlBoundaryShortSource");
    QByteArray payload(128, 's');
    CountingSeekableDevice source(payload, this);

    Internal::QCSingleFileMultipartBodyDevice bodyDevice(boundary,
                                                         QStringLiteral("file"),
                                                         &source,
                                                         QStringLiteral("short.bin"),
                                                         QStringLiteral("application/octet-stream"),
                                                         payload.size() + 64,
                                                         this);

    QByteArray buffer(static_cast<int>(bodyDevice.size()), Qt::Uninitialized);
    QVERIFY(bodyDevice.readData(buffer.data(), buffer.size()) > 0);
    QCOMPARE(bodyDevice.readData(buffer.data(), buffer.size()), qint64(-1));
    QVERIFY(bodyDevice.errorString().contains(QStringLiteral("declared size")));
}

void TestQCNetworkMultipartUpload::testMultipartBodyDeviceEscapesHeaderParameters()
{
    const QString boundary = QStringLiteral("----QCurlBoundaryEscapedHeaders");
    QByteArray payload("abc");
    CountingSeekableDevice source(payload, this);

    Internal::QCSingleFileMultipartBodyDevice bodyDevice(
        boundary,
        QStringLiteral("file\"\r\nX-Bad: yes"),
        &source,
        QStringLiteral("evil\\\"\r\nX-Bad: yes.bin"),
        QStringLiteral("application/octet-stream\r\nX-Bad: yes"),
        payload.size(),
        this);

    QByteArray buffer(512, Qt::Uninitialized);
    const qint64 n = bodyDevice.readData(buffer.data(), buffer.size());
    QVERIFY(n > 0);

    const QByteArray prefix = buffer.left(static_cast<int>(n));
    QVERIFY(!prefix.contains("\r\nX-Bad:"));
    QVERIFY(prefix.contains("name=\"file\\\"  X-Bad: yes\""));
    QVERIFY(prefix.contains("filename=\"evil\\\\\\\"  X-Bad: yes.bin\""));
    QVERIFY(prefix.contains("Content-Type: application/octet-stream\r\n"));
}

QTEST_MAIN(TestQCNetworkMultipartUpload)
#include "tst_QCNetworkMultipartUpload.moc"
