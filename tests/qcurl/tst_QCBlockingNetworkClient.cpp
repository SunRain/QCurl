/**
 * @file
 * @brief Blocking Extras 同步客户端 raw QIODevice 上传合同测试。
 */

#include "QCBlockingCookieStore.h"
#include "QCBlockingNetworkClient.h"
#include "QCNetworkError.h"
#include "QCNetworkRequest.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QtTest/QtTest>

#include <condition_variable>
#include <cstring>
#include <mutex>
#include <optional>
#include <thread>

using namespace QCurl;

namespace {

class UploadEchoServer final
{
public:
    struct RequestRecord
    {
        QByteArray method;
        QByteArray path;
        QByteArray body;
        QByteArray cookie;
    };

    ~UploadEchoServer()
    {
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    bool start()
    {
        m_thread = std::thread([this]() { run(); });

        std::unique_lock<std::mutex> lock(m_mutex);
        m_ready.wait(lock, [this]() { return m_started.has_value(); });
        return m_started.value();
    }

    QUrl url(const QString &path) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_port).arg(path));
    }

    RequestRecord lastRequest() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_requests.isEmpty()) {
            return {};
        }
        return m_requests.constLast();
    }

private:
    struct ParsedRequest
    {
        bool complete = false;
        qint64 contentLength = -1;
        QByteArray method;
        QByteArray path;
        QByteArray body;
        QByteArray cookie;
    };

    void run()
    {
        QTcpServer server;
        const bool started = server.listen(QHostAddress::LocalHost, 0);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_started = started;
            m_port = started ? server.serverPort() : 0;
        }
        m_ready.notify_one();
        if (!started || !server.waitForNewConnection(30000)) {
            return;
        }

        QTcpSocket *socket = server.nextPendingConnection();
        if (!socket) {
            return;
        }

        const ParsedRequest request = readRequest(socket);
        if (!request.complete) {
            socket->disconnectFromHost();
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_requests.append(
                RequestRecord{request.method, request.path, request.body, request.cookie});
        }
        sendResponse(socket, request.body);
        socket->disconnectFromHost();
        if (socket->state() != QAbstractSocket::UnconnectedState) {
            socket->waitForDisconnected(3000);
        }
    }

    static QByteArray headerValue(const QByteArray &line, const QByteArray &name)
    {
        return line.mid(name.size() + 1).trimmed();
    }

    static void parseHeaders(ParsedRequest *request, const QByteArray &headerBlock)
    {
        const QList<QByteArray> lines = headerBlock.split('\n');
        if (!lines.isEmpty()) {
            const QList<QByteArray> parts = lines.first().trimmed().split(' ');
            if (parts.size() >= 2) {
                request->method = parts.at(0).trimmed();
                request->path = parts.at(1).trimmed();
            }
        }

        for (int i = 1; i < lines.size(); ++i) {
            const QByteArray line = lines.at(i).trimmed();
            if (line.toLower().startsWith(QByteArrayLiteral("cookie:"))) {
                request->cookie = headerValue(line, QByteArrayLiteral("cookie"));
                continue;
            }
            if (!line.toLower().startsWith(QByteArrayLiteral("content-length:"))) {
                continue;
            }
            bool ok = false;
            const qint64 value =
                headerValue(line, QByteArrayLiteral("content-length")).toLongLong(&ok);
            if (ok && value >= 0) {
                request->contentLength = value;
            }
        }
    }

    static ParsedRequest readRequest(QTcpSocket *socket)
    {
        QByteArray buffer;
        ParsedRequest request;
        int headerEnd = -1;
        QElapsedTimer timer;
        timer.start();

        while (timer.elapsed() < 30000) {
            if (!socket->bytesAvailable() && !socket->waitForReadyRead(100)) {
                continue;
            }
            buffer.append(socket->readAll());
            if (headerEnd < 0) {
                headerEnd = buffer.indexOf("\r\n\r\n");
                if (headerEnd >= 0) {
                    parseHeaders(&request, buffer.left(headerEnd));
                }
            }
            if (headerEnd < 0 || request.contentLength < 0) {
                continue;
            }

            const int bodyOffset = headerEnd + 4;
            const int needed = bodyOffset + static_cast<int>(request.contentLength);
            if (buffer.size() < needed) {
                continue;
            }
            request.body = buffer.mid(bodyOffset, static_cast<qsizetype>(request.contentLength));
            request.complete = true;
            return request;
        }

        return request;
    }

    static void sendResponse(QTcpSocket *socket, const QByteArray &body)
    {
        const QByteArray response = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Length: ")
            + QByteArray::number(body.size())
            + QByteArrayLiteral("\r\nSet-Cookie: session=updated; Path=/\r\nConnection: close\r\n\r\n")
            + body;
        socket->write(response);
        socket->flush();
        socket->waitForBytesWritten(30000);
    }

    mutable std::mutex m_mutex;
    std::condition_variable m_ready;
    std::optional<bool> m_started;
    std::thread m_thread;
    quint16 m_port = 0;
    QList<RequestRecord> m_requests;
};

class SequentialDevice final : public QIODevice
{
public:
    explicit SequentialDevice(QByteArray data, QObject *parent = nullptr)
        : QIODevice(parent)
        , m_data(std::move(data))
    {}

    bool isSequential() const override { return true; }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const qint64 remaining = m_data.size() - m_offset;
        const qint64 amount = qMin(maxSize, remaining);
        if (amount <= 0) {
            return 0;
        }
        std::memcpy(data, m_data.constData() + m_offset, static_cast<size_t>(amount));
        m_offset += amount;
        return amount;
    }

    qint64 writeData(const char *, qint64) override { return -1; }

private:
    QByteArray m_data;
    qint64 m_offset = 0;
};

QCBlockingNetworkClient makeClient()
{
    QCBlockingNetworkClient::Options options;
    options.setApplicationThreadPolicy(
        QCBlockingNetworkClient::ApplicationThreadPolicy::AllowForCliOrTests);
    return QCBlockingNetworkClient(options);
}

QCNetworkRequest makeRequest(const QUrl &url)
{
    QCNetworkRequest request(url);
    request.setRawHeader(QByteArrayLiteral("Expect"), QByteArray());
    return request;
}

} // namespace

class tst_QCBlockingNetworkClient : public QObject
{
    Q_OBJECT

private slots:
    void sendPostDeviceWithExplicitSize();
    void sendPutDeviceWithInferredSeekableSize();
    void appliesCookieSnapshotAndReturnsCookieDelta();
    void rejectsInvalidDevice_data();
    void rejectsInvalidDevice();
    void rejectsCrossThreadDevice();
    void rejectsSequentialDeviceWithoutExplicitSize();
};

void tst_QCBlockingNetworkClient::sendPostDeviceWithExplicitSize()
{
    UploadEchoServer server;
    QVERIFY(server.start());

    const QByteArray payload = QByteArrayLiteral("post body from device");
    QBuffer device;
    device.setData(payload);
    QVERIFY(device.open(QIODevice::ReadOnly));

    const auto result = makeClient().sendPost(
        makeRequest(server.url(QStringLiteral("/post"))), &device, payload.size());
    QVERIFY2(result.isSuccess(), qPrintable(result.errorMessage()));
    QCOMPARE(result.statusCode(), 200);
    QCOMPARE(result.body(), payload);
    QCOMPARE(server.lastRequest().method, QByteArrayLiteral("POST"));
    QCOMPARE(server.lastRequest().body, payload);
}

void tst_QCBlockingNetworkClient::sendPutDeviceWithInferredSeekableSize()
{
    UploadEchoServer server;
    QVERIFY(server.start());

    const QByteArray payload = QByteArrayLiteral("prefix:put body from device");
    QBuffer device;
    device.setData(payload);
    QVERIFY(device.open(QIODevice::ReadOnly));
    QVERIFY(device.seek(7));

    const QByteArray expected = payload.mid(7);
    const auto result =
        makeClient().sendPut(makeRequest(server.url(QStringLiteral("/put"))), &device);
    QVERIFY2(result.isSuccess(), qPrintable(result.errorMessage()));
    QCOMPARE(result.statusCode(), 200);
    QCOMPARE(result.body(), expected);
    QCOMPARE(server.lastRequest().method, QByteArrayLiteral("PUT"));
    QCOMPARE(server.lastRequest().body, expected);
}

void tst_QCBlockingNetworkClient::appliesCookieSnapshotAndReturnsCookieDelta()
{
    UploadEchoServer server;
    QVERIFY(server.start());

    const QCCookieSnapshot snapshot({QNetworkCookie(QByteArrayLiteral("session"),
                                                    QByteArrayLiteral("input"))});
    const auto result = makeClient().sendPost(
        makeRequest(server.url(QStringLiteral("/cookies"))), QByteArrayLiteral("body"), snapshot);
    QVERIFY2(result.isSuccess(), qPrintable(result.errorMessage()));
    QCOMPARE(server.lastRequest().cookie, QByteArrayLiteral("session=input"));
    QVERIFY(!result.cookieDelta().isEmpty());
    QCOMPARE(result.cookieDelta().cookies().constFirst().name(), QByteArrayLiteral("session"));
    QCOMPARE(result.cookieDelta().cookies().constFirst().value(), QByteArrayLiteral("updated"));
}

void tst_QCBlockingNetworkClient::rejectsInvalidDevice_data()
{
    QTest::addColumn<bool>("useNullDevice");
    QTest::addColumn<bool>("openDevice");
    QTest::addColumn<int>("openMode");
    QTest::addColumn<QString>("messageNeedle");

    QTest::newRow("null") << true << false
                           << int(QIODevice::NotOpen) << QStringLiteral("must not be null");
    QTest::newRow("unopened") << false << false << int(QIODevice::NotOpen)
                               << QStringLiteral("open and readable");
    QTest::newRow("write-only") << false << true << int(QIODevice::WriteOnly)
                                 << QStringLiteral("open and readable");
}

void tst_QCBlockingNetworkClient::rejectsInvalidDevice()
{
    QFETCH(bool, useNullDevice);
    QFETCH(bool, openDevice);
    QFETCH(int, openMode);
    QFETCH(QString, messageNeedle);

    QBuffer device;
    device.setData(QByteArrayLiteral("body"));
    if (openDevice) {
        QVERIFY(device.open(QIODevice::OpenMode(openMode)));
    }

    const auto result = makeClient().sendPost(
        makeRequest(QUrl(QStringLiteral("http://127.0.0.1:1/post"))),
        useNullDevice ? nullptr : &device,
        qint64(4));
    QVERIFY(!result.isSuccess());
    QCOMPARE(result.error(), NetworkError::InvalidRequest);
    QVERIFY2(result.errorMessage().contains(messageNeedle), qPrintable(result.errorMessage()));
}

void tst_QCBlockingNetworkClient::rejectsCrossThreadDevice()
{
    QThread worker;
    QThread *mainThread = QThread::currentThread();
    QBuffer device;
    device.setData(QByteArrayLiteral("body"));
    QVERIFY(device.open(QIODevice::ReadOnly));
    device.moveToThread(&worker);
    worker.start();

    const auto result = makeClient().sendPut(
        makeRequest(QUrl(QStringLiteral("http://127.0.0.1:1/put"))), &device, qint64(4));
    QVERIFY(!result.isSuccess());
    QCOMPARE(result.error(), NetworkError::InvalidRequest);
    QVERIFY(result.errorMessage().contains(QStringLiteral("calling thread")));

    QMetaObject::invokeMethod(&device, [&device, mainThread]() { device.moveToThread(mainThread); },
                              Qt::BlockingQueuedConnection);
    worker.quit();
    QVERIFY(worker.wait(30000));
}

void tst_QCBlockingNetworkClient::rejectsSequentialDeviceWithoutExplicitSize()
{
    SequentialDevice device(QByteArrayLiteral("body"));
    QVERIFY(device.open(QIODevice::ReadOnly));

    const auto result = makeClient().sendPost(
        makeRequest(QUrl(QStringLiteral("http://127.0.0.1:1/post"))), &device);
    QVERIFY(!result.isSuccess());
    QCOMPARE(result.error(), NetworkError::InvalidRequest);
    QVERIFY(result.errorMessage().contains(QStringLiteral("requires explicit size")));
}

QTEST_MAIN(tst_QCBlockingNetworkClient)
#include "tst_QCBlockingNetworkClient.moc"
