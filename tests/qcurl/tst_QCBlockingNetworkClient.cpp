/**
 * @file
 * @brief Blocking Extras 同步客户端 raw QIODevice 上传合同测试。
 */

#include "QCBlockingCookieStore.h"
#include "QCBlockingNetworkClient.h"
#include "QCNetworkError.h"
#include "QCNetworkRequest.h"
#include "qcblocking_upload_echo_server.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryFile>
#include <QThread>
#include <QtTest/QtTest>

#include <cstring>
#include <utility>

using namespace QCurl;

namespace {

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

struct ProgressProbe
{
    int calls = 0;
    qint64 maxBytesReceived = 0;
    bool cancelAfterFirstDownloadProgress = false;
};

bool recordProgress(const QCTransferProgress &progress, void *userData)
{
    auto *probe = static_cast<ProgressProbe *>(userData);
    if (!probe) {
        return false;
    }

    ++probe->calls;
    probe->maxBytesReceived = qMax(probe->maxBytesReceived, progress.bytesReceived());
    return !probe->cancelAfterFirstDownloadProgress || progress.bytesReceived() <= 0;
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
    void getRejectsBodyOverMemoryLimit();
    void getReportsProgress();
    void getCanBeCancelledFromProgressCallback();
    void rawHeaderListPreservesDuplicateSetCookieOrder();
    void downloadToDeviceWritesLargeResponse();
    void downloadToDeviceRejectsInvalidOutput_data();
    void downloadToDeviceRejectsInvalidOutput();
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
    QCOMPARE(result.rawHeaders().value(QByteArrayLiteral("Set-Cookie")),
             QByteArrayLiteral("session=updated; Path=/"));
    QCOMPARE(result.bytesReceived(), qint64(4));
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
    QCOMPARE(result.error(), NetworkError::InputDeviceError);
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
    QCOMPARE(result.error(), NetworkError::InputDeviceError);
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
    QCOMPARE(result.error(), NetworkError::ReplayNotSupported);
    QVERIFY(result.errorMessage().contains(QStringLiteral("requires explicit size")));
}

void tst_QCBlockingNetworkClient::getRejectsBodyOverMemoryLimit()
{
    UploadEchoServer::ResponsePlan plan;
    plan.body = QByteArrayLiteral("larger-than-limit");
    UploadEchoServer server(plan);
    QVERIFY(server.start());

    QCBlockingRequestOptions requestOptions;
    requestOptions.setMaxInMemoryBodyBytes(4);

    const auto result =
        makeClient().get(makeRequest(server.url(QStringLiteral("/large"))), requestOptions);
    QVERIFY(!result.isSuccess());
    QCOMPARE(result.error(), NetworkError::BodyTooLarge);
    QVERIFY(result.errorMessage().contains(QStringLiteral("maxInMemoryBodyBytes")));
}

void tst_QCBlockingNetworkClient::getReportsProgress()
{
    UploadEchoServer::ResponsePlan plan;
    plan.body = QByteArray(64 * 1024, 'p');
    UploadEchoServer server(plan);
    QVERIFY(server.start());

    ProgressProbe probe;
    QCBlockingRequestOptions requestOptions;
    requestOptions.setProgressCallback(recordProgress, &probe);

    const auto result =
        makeClient().get(makeRequest(server.url(QStringLiteral("/progress"))), requestOptions);
    QVERIFY2(result.isSuccess(), qPrintable(result.errorMessage()));
    QVERIFY(probe.calls > 0);
    QCOMPARE(probe.maxBytesReceived, qint64(plan.body.size()));
}

void tst_QCBlockingNetworkClient::getCanBeCancelledFromProgressCallback()
{
    UploadEchoServer::ResponsePlan plan;
    plan.body = QByteArray(64 * 1024, 'c');
    UploadEchoServer server(plan);
    QVERIFY(server.start());

    ProgressProbe probe;
    probe.cancelAfterFirstDownloadProgress = true;
    QCBlockingRequestOptions requestOptions;
    requestOptions.setProgressCallback(recordProgress, &probe);

    const auto result =
        makeClient().get(makeRequest(server.url(QStringLiteral("/cancel"))), requestOptions);
    QVERIFY(!result.isSuccess());
    QCOMPARE(result.error(), NetworkError::OperationCancelled);
    QVERIFY(result.errorMessage().contains(QStringLiteral("progress callback")));
    QVERIFY(probe.calls > 0);
    QVERIFY(probe.maxBytesReceived > 0);
}

void tst_QCBlockingNetworkClient::rawHeaderListPreservesDuplicateSetCookieOrder()
{
    UploadEchoServer::ResponsePlan plan;
    plan.extraHeaders = {
        QByteArrayLiteral("Set-Cookie: first=1; Path=/"),
        QByteArrayLiteral("X-Trace: a"),
        QByteArrayLiteral("Set-Cookie: second=2; Path=/"),
    };
    UploadEchoServer server(plan);
    QVERIFY(server.start());

    const auto result = makeClient().get(makeRequest(server.url(QStringLiteral("/cookies"))));
    QVERIFY2(result.isSuccess(), qPrintable(result.errorMessage()));

    const auto headers = result.rawHeaderList();
    QList<QByteArray> setCookies;
    for (const auto &header : headers) {
        if (header.first.compare(QByteArrayLiteral("Set-Cookie"), Qt::CaseInsensitive) == 0) {
            setCookies.append(header.second);
        }
    }
    QCOMPARE(setCookies.size(), 2);
    QCOMPARE(setCookies.at(0), QByteArrayLiteral("first=1; Path=/"));
    QCOMPARE(setCookies.at(1), QByteArrayLiteral("second=2; Path=/"));
    QCOMPARE(result.cookieDelta().cookies().size(), 2);
    QCOMPARE(result.cookieDelta().cookies().at(0).name(), QByteArrayLiteral("first"));
    QCOMPARE(result.cookieDelta().cookies().at(1).name(), QByteArrayLiteral("second"));
}

void tst_QCBlockingNetworkClient::downloadToDeviceWritesLargeResponse()
{
    UploadEchoServer::ResponsePlan plan;
    plan.body = QByteArray(64 * 1024, 'x');
    UploadEchoServer server(plan);
    QVERIFY(server.start());

    QTemporaryFile output;
    QVERIFY(output.open());

    QCBlockingRequestOptions requestOptions;
    requestOptions.setMaxInMemoryBodyBytes(4);
    const auto result = makeClient().downloadToDevice(
        makeRequest(server.url(QStringLiteral("/download"))), &output, requestOptions);

    QVERIFY2(result.isSuccess(), qPrintable(result.errorMessage()));
    QCOMPARE(result.statusCode(), 200);
    QVERIFY(result.body().isEmpty());
    QCOMPARE(result.bytesReceived(), qint64(plan.body.size()));
    QCOMPARE(output.size(), qint64(plan.body.size()));
    QVERIFY(output.seek(0));
    QCOMPARE(output.readAll(), plan.body);
}

void tst_QCBlockingNetworkClient::downloadToDeviceRejectsInvalidOutput_data()
{
    QTest::addColumn<bool>("useNullDevice");
    QTest::addColumn<bool>("openDevice");
    QTest::addColumn<int>("openMode");
    QTest::addColumn<QString>("messageNeedle");

    QTest::newRow("null") << true << false
                           << int(QIODevice::NotOpen) << QStringLiteral("must not be null");
    QTest::newRow("unopened") << false << false << int(QIODevice::NotOpen)
                               << QStringLiteral("open and writable");
    QTest::newRow("read-only") << false << true << int(QIODevice::ReadOnly)
                                << QStringLiteral("open and writable");
}

void tst_QCBlockingNetworkClient::downloadToDeviceRejectsInvalidOutput()
{
    QFETCH(bool, useNullDevice);
    QFETCH(bool, openDevice);
    QFETCH(int, openMode);
    QFETCH(QString, messageNeedle);

    QBuffer output;
    if (openDevice) {
        QVERIFY(output.open(QIODevice::OpenMode(openMode)));
    }

    const auto result = makeClient().downloadToDevice(
        makeRequest(QUrl(QStringLiteral("http://127.0.0.1:1/download"))),
        useNullDevice ? nullptr : &output);
    QVERIFY(!result.isSuccess());
    QCOMPARE(result.error(), NetworkError::OutputDeviceError);
    QVERIFY2(result.errorMessage().contains(messageNeedle), qPrintable(result.errorMessage()));
}

QTEST_MAIN(tst_QCBlockingNetworkClient)
#include "tst_QCBlockingNetworkClient.moc"
