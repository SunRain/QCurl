/**
 * @file tst_QCNetworkFileTransfer.cpp
 * @brief 流式下载/上传与断点续传 API 测试
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkDownloadToDeviceJob.h"
#include "QCNetworkError.h"
#include "QCNetworkMultipartBody.h"
#include "QCNetworkResumableDownloadJob.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "test_httpbin_env.h"

#include <QBuffer>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace QCurl;

namespace {

class ThrottledRangeServer final : public QObject
{
public:
    enum class PartialResponseMode {
        MatchingRange,
        MismatchedRangeStart,
    };

    explicit ThrottledRangeServer(qint64 totalBytes, QObject *parent = nullptr)
        : QObject(parent)
        , m_totalBytes(totalBytes)
        , m_payload(static_cast<int>(totalBytes), Qt::Uninitialized)
    {
        for (int i = 0; i < m_payload.size(); ++i) {
            m_payload[i] = static_cast<char>(i % 256);
        }

        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (m_server.hasPendingConnections()) {
                QTcpSocket *socket = m_server.nextPendingConnection();
                if (!socket) {
                    continue;
                }
                setupSocket(socket);
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
        QByteArray value = headerValue.trimmed();
        if (!value.startsWith("bytes=")) {
            return 0;
        }

        const int dashPos = value.indexOf('-');
        if (dashPos <= 6) {
            return 0;
        }

        bool ok            = false;
        const qint64 start = value.mid(6, dashPos - 6).toLongLong(&ok);
        return ok ? qMax<qint64>(0, start) : 0;
    }

    void setupSocket(QTcpSocket *socket)
    {
        auto requestBuffer  = QSharedPointer<QByteArray>::create();
        auto requestHandled = QSharedPointer<bool>::create(false);

        QObject::connect(socket,
                         &QTcpSocket::readyRead,
                         socket,
                         [this, socket, requestBuffer, requestHandled]() {
                             if (*requestHandled) {
                                 return;
                             }

                             requestBuffer->append(socket->readAll());
                             const int headerEnd = requestBuffer->indexOf("\r\n\r\n");
                             if (headerEnd < 0) {
                                 return;
                             }

                             *requestHandled = true;

                             const QByteArray headerBlock  = requestBuffer->left(headerEnd);
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
                                 QByteArray resp = QByteArray(
                                     "HTTP/1.1 416 Range Not Satisfiable\r\n"
                                     "Content-Range: bytes */");
                                 resp += QByteArray::number(m_totalBytes);
                                 resp += QByteArrayLiteral("\r\nConnection: close\r\n\r\n");
                                 socket->write(resp);
                                 socket->disconnectFromHost();
                                 return;
                             }

                             const bool isPartial   = rangeStart > 0;
                             const qint64 remaining = m_totalBytes - rangeStart;

                             QByteArray resp;
                             resp += isPartial ? "HTTP/1.1 206 Partial Content\r\n"
                                               : "HTTP/1.1 200 OK\r\n";
                             resp += "Content-Type: application/octet-stream\r\n";
                             resp += "Accept-Ranges: bytes\r\n";
                             resp += "Content-Length: " + QByteArray::number(remaining) + "\r\n";
                             if (isPartial) {
                                 qint64 responseRangeStart = rangeStart;
                                 if (m_partialResponseMode
                                     == PartialResponseMode::MismatchedRangeStart) {
                                     responseRangeStart = qMin(rangeStart + 1, m_totalBytes - 1);
                                 }
                                 const qint64 responseRemaining = m_totalBytes - responseRangeStart;
                                 resp.replace(resp.indexOf("Content-Length: "),
                                              QByteArray("Content-Length: ").size()
                                                  + QByteArray::number(remaining).size(),
                                              QByteArray("Content-Length: ")
                                                  + QByteArray::number(responseRemaining));
                                 resp += "Content-Range: bytes ";
                                 resp += QByteArray::number(responseRangeStart);
                                 resp += "-";
                                 resp += QByteArray::number(m_totalBytes - 1);
                                 resp += "/";
                                 resp += QByteArray::number(m_totalBytes);
                                 resp += "\r\n";
                             }
                             resp += "Connection: close\r\n\r\n";

                             socket->write(resp);
                             socket->flush();

                             if (!isPartial && m_dropFirstFullResponse) {
                                 // 故意只发送部分数据并关闭连接，制造“中断下载”以验证断点续传逻辑（避免依赖 cancel 的时序不确定性）。
                                 const qint64 sendBytes = qMin<qint64>(m_dropBytes, remaining);
                                 socket->write(m_payload.constData() + rangeStart, sendBytes);
                                 socket->flush();
                                 m_dropFirstFullResponse = false;
                                 socket->disconnectFromHost();
                                 socket->close();
                                 return;
                             }

                             const qint64 responseStart = (isPartial
                                                               && m_partialResponseMode
                                                                      == PartialResponseMode::MismatchedRangeStart)
                                                             ? qMin(rangeStart + 1, m_totalBytes - 1)
                                                             : rangeStart;
                             const qint64 responseBytes = m_totalBytes - responseStart;
                             socket->write(m_payload.constData() + responseStart, responseBytes);
                             socket->flush();
                             socket->disconnectFromHost();
                         });
    }

    qint64 m_totalBytes = 0;
    QByteArray m_payload;
    QTcpServer m_server;
    bool m_dropFirstFullResponse = true;
    qint64 m_dropBytes           = 64 * 1024;
    PartialResponseMode m_partialResponseMode = PartialResponseMode::MatchingRange;
};

} // namespace

class TestQCNetworkFileTransfer : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void testDownloadToDeviceWritesExpectedBytes();
    void testPostMultipartDeviceSendsPayload();
    void testPostMultipartDeviceReplyOwnsStreamingBody();
    void testDownloadFileResumableContinuesFromPartialFile();
    void testDownloadFileResumableTreats416MatchingContentRangeAsAlreadyComplete();
    void testDownloadFileResumableTreats416MatchingContentRangeAsAlreadyCompleteWithoutErrorSignal();
    void testDownloadFileResumableFailsWhenContentRangeStartDoesNotMatch();

private:
    QCNetworkAccessManager *m_manager = nullptr;
    QString m_httpbinBaseUrl;

    // 下载/上传类用例在慢环境下使用更宽松的默认等待窗口。
    bool waitForFinished(QCNetworkReply *reply, int timeout = 40000);
    QJsonObject parseJson(const QByteArray &data) const;
    QCNetworkReply *sendMultipartDevice(const QUrl &url,
                                        QIODevice *device,
                                        const QString &fileName,
                                        qint64 sizeBytes);
};

void TestQCNetworkFileTransfer::initTestCase()
{
    m_manager = new QCNetworkAccessManager(this);

    m_httpbinBaseUrl = TestEnv::httpbinBaseUrl();
    QVERIFY2(!m_httpbinBaseUrl.isEmpty(), qPrintable(TestEnv::httpbinMissingReason()));
    qDebug() << "httpbin base URL:" << m_httpbinBaseUrl;

    QCNetworkRequest healthCheck(QUrl(m_httpbinBaseUrl + "/status/200"));
    auto *reply = m_manager->sendGet(healthCheck);
    QVERIFY(waitForFinished(reply, 5000));
    QVERIFY2(reply->error() == NetworkError::NoError,
             qPrintable(TestEnv::httpbinUnavailableReason(m_httpbinBaseUrl, reply->errorString())));
    reply->deleteLater();
}

void TestQCNetworkFileTransfer::cleanupTestCase()
{
    m_manager = nullptr;
}

bool TestQCNetworkFileTransfer::waitForFinished(QCNetworkReply *reply, int timeout)
{
    if (!reply) {
        return false;
    }

    QSignalSpy spy(reply, &QCNetworkReply::finished);
    return spy.wait(timeout);
}

QJsonObject TestQCNetworkFileTransfer::parseJson(const QByteArray &data) const
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    return doc.object();
}

QCNetworkReply *TestQCNetworkFileTransfer::sendMultipartDevice(const QUrl &url,
                                                               QIODevice *device,
                                                               const QString &fileName,
                                                               qint64 sizeBytes)
{
    QString error;
    auto body = QCNetworkMultipartBody::fromSingleFileDevice(
        device,
        QStringLiteral("file"),
        fileName,
        QStringLiteral("application/octet-stream"),
        sizeBytes,
        &error);
    if (!body.has_value()) {
        return nullptr;
    }

    QCNetworkRequest request(url);
    request.setRawHeader(QByteArrayLiteral("Content-Type"), body->contentType());

    auto *bodyDevice = body->takeDevice(nullptr, &error);
    if (!bodyDevice) {
        return nullptr;
    }
    auto *reply = m_manager->sendPost(request, bodyDevice, body->sizeBytes());
    bodyDevice->setParent(reply);
    return reply;
}

void TestQCNetworkFileTransfer::testDownloadToDeviceWritesExpectedBytes()
{
    QBuffer buffer;
    QVERIFY(buffer.open(QIODevice::ReadWrite));

    const int expectedBytes = 4096;
    QUrl url(m_httpbinBaseUrl + QStringLiteral("/bytes/%1?seed=42").arg(expectedBytes));

    auto *job = new QCNetworkDownloadToDeviceJob(m_manager, url, &buffer, this);
    QSignalSpy jobFinishedSpy(job, &QCNetworkTransferJob::finished);
    QCOMPARE(job->reply(), nullptr);
    job->start();
    QTRY_VERIFY_WITH_TIMEOUT(job->reply() != nullptr, 1000);
    auto *reply = job->reply();
    QVERIFY(reply);
    QVERIFY(jobFinishedSpy.wait(40000));
    QVERIFY(job->isFinished());
    QCOMPARE(job->error(), NetworkError::NoError);
    QCOMPARE(reply->error(), NetworkError::NoError);

    QCOMPARE(static_cast<int>(buffer.size()), expectedBytes);
    buffer.seek(0);
    QByteArray downloaded = buffer.readAll();
    QCOMPARE(downloaded.size(), expectedBytes);

    reply->deleteLater();
    job->deleteLater();
}

void TestQCNetworkFileTransfer::testPostMultipartDeviceSendsPayload()
{
    QByteArray payload(2048, Qt::Uninitialized);
    for (int i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<char>(i % 256);
    }

    QBuffer buffer(&payload);
    QVERIFY(buffer.open(QIODevice::ReadOnly));

    QUrl url(m_httpbinBaseUrl + "/post");
    auto *reply = sendMultipartDevice(url, &buffer, QStringLiteral("payload.bin"), payload.size());

    QVERIFY(waitForFinished(reply));
    QCOMPARE(reply->error(), NetworkError::NoError);

    auto body = reply->readAll();
    QVERIFY(body.has_value());

    QJsonObject json     = parseJson(body.value());
    QJsonObject filesObj = json.value(QStringLiteral("files")).toObject();
    QString echoedStr    = filesObj.value(QStringLiteral("file")).toString();

    // httpbin 可能回显为 base64 data URL，需要先提取并解码 payload。
    // 格式: "data:application/octet-stream;base64,AAECAwQF..."
    QByteArray echoed;
    if (echoedStr.startsWith("data:")) {
        // 提取 base64 负载部分。
        int commaPos = echoedStr.indexOf(',');
        if (commaPos != -1) {
            QString base64Part = echoedStr.mid(commaPos + 1);
            echoed             = QByteArray::fromBase64(base64Part.toUtf8());
        } else {
            echoed = echoedStr.toUtf8();
        }
    } else {
        echoed = echoedStr.toUtf8();
    }

    QCOMPARE(echoed, payload);

    reply->deleteLater();
}

void TestQCNetworkFileTransfer::testPostMultipartDeviceReplyOwnsStreamingBody()
{
    QByteArray payload(1024, 'm');
    QBuffer buffer(&payload);
    QVERIFY(buffer.open(QIODevice::ReadOnly));

    QUrl url(m_httpbinBaseUrl + "/post");
    auto *reply = sendMultipartDevice(url, &buffer, QStringLiteral("payload.bin"), payload.size());

    QVERIFY(reply != nullptr);
    const auto children = reply->children();
    bool foundMultipartDevice = false;
    for (QObject *child : children) {
        if (child && child->metaObject()
                         && QByteArray(child->metaObject()->className())
                                .contains("QCSingleFileMultipartBodyDevice")) {
            foundMultipartDevice = true;
            break;
        }
    }
    QVERIFY(foundMultipartDevice);

    QVERIFY(waitForFinished(reply));
    QCOMPARE(reply->error(), NetworkError::NoError);
    reply->deleteLater();
}

void TestQCNetworkFileTransfer::testDownloadFileResumableContinuesFromPartialFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString savePath  = dir.filePath(QStringLiteral("resumable.bin"));
    const qint64 totalBytes = 256 * 1024;

    ThrottledRangeServer server(totalBytes);
    QVERIFY2(server.start(), "Cannot start local throttled HTTP server (port binding failed)");

    const QUrl url = server.url(QStringLiteral("/range.bin"));

    auto *firstJob = new QCNetworkResumableDownloadJob(m_manager, url, savePath, false, this);
    QCOMPARE(firstJob->reply(), nullptr);
    firstJob->start();
    QTRY_VERIFY_WITH_TIMEOUT(firstJob->reply() != nullptr, 1000);
    auto *firstAttempt = firstJob->reply();

    // 该测试依赖服务端主动断连制造部分下载，以验证 resumable 续传语义。
    QVERIFY(waitForFinished(firstAttempt, 10000));
    QVERIFY(firstAttempt->error() != NetworkError::NoError);
    QVERIFY(firstJob->isFinished());
    QVERIFY(firstJob->error() != NetworkError::NoError);
    firstAttempt->deleteLater();
    firstJob->deleteLater();

    QFile partial(savePath);
    QVERIFY(partial.exists());
    qint64 partialSize = partial.size();
    QVERIFY(partialSize > 0);
    QVERIFY(partialSize < totalBytes);

    auto *resumeJob = new QCNetworkResumableDownloadJob(m_manager, url, savePath, false, this);
    resumeJob->start();
    QTRY_VERIFY_WITH_TIMEOUT(resumeJob->reply() != nullptr, 1000);
    auto *resumeReply = resumeJob->reply();
    QVERIFY(waitForFinished(resumeReply, 10000));
    QVERIFY(resumeJob->isFinished());
    QCOMPARE(resumeJob->error(), NetworkError::NoError);
    QCOMPARE(resumeReply->error(), NetworkError::NoError);
    resumeReply->deleteLater();
    resumeJob->deleteLater();

    QFile finalFile(savePath);
    QVERIFY(finalFile.open(QIODevice::ReadOnly));
    QByteArray finalData = finalFile.readAll();
    QCOMPARE(static_cast<qint64>(finalData.size()), totalBytes);
    QCOMPARE(finalData, server.expectedPayload());
}

void TestQCNetworkFileTransfer::testDownloadFileResumableTreats416MatchingContentRangeAsAlreadyComplete()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString savePath  = dir.filePath(QStringLiteral("already-complete.bin"));
    const qint64 totalBytes = 128 * 1024;

    ThrottledRangeServer server(totalBytes);
    QVERIFY2(server.start(), "Cannot start local throttled HTTP server (port binding failed)");

    QFile complete(savePath);
    QVERIFY(complete.open(QIODevice::WriteOnly));
    complete.write(server.expectedPayload());
    complete.close();

    const QUrl url = server.url(QStringLiteral("/range.bin"));

    auto *job = new QCNetworkResumableDownloadJob(m_manager, url, savePath, false, this);
    QCOMPARE(job->reply(), nullptr);
    job->start();
    QTRY_VERIFY_WITH_TIMEOUT(job->reply() != nullptr, 1000);
    auto *reply = job->reply();
    QVERIFY(reply);
    QVERIFY(waitForFinished(reply, 10000));

    QVERIFY(job->isFinished());
    QCOMPARE(job->error(), NetworkError::NoError);
    QCOMPARE(reply->error(), NetworkError::NoError);
    QCOMPARE(reply->httpStatusCode(), 416);
    QCOMPARE(reply->rawHeader(QByteArrayLiteral("Content-Range")),
             QByteArray("bytes */" + QByteArray::number(totalBytes)));

    QFile finalFile(savePath);
    QVERIFY(finalFile.open(QIODevice::ReadOnly));
    const QByteArray finalData = finalFile.readAll();
    QCOMPARE(static_cast<qint64>(finalData.size()), totalBytes);
    QCOMPARE(finalData, server.expectedPayload());

    reply->deleteLater();
    job->deleteLater();
}

void TestQCNetworkFileTransfer::testDownloadFileResumableTreats416MatchingContentRangeAsAlreadyCompleteWithoutErrorSignal()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString savePath  = dir.filePath(QStringLiteral("already-complete-no-error.bin"));
    const qint64 totalBytes = 64 * 1024;

    ThrottledRangeServer server(totalBytes);
    QVERIFY2(server.start(), "Cannot start local throttled HTTP server (port binding failed)");

    QFile complete(savePath);
    QVERIFY(complete.open(QIODevice::WriteOnly));
    complete.write(server.expectedPayload());
    complete.close();

    const QUrl url = server.url(QStringLiteral("/range.bin"));
    auto *job = new QCNetworkResumableDownloadJob(m_manager, url, savePath, false, this);
    QCOMPARE(job->reply(), nullptr);
    job->start();
    QTRY_VERIFY_WITH_TIMEOUT(job->reply() != nullptr, 1000);
    auto *reply = job->reply();

    QSignalSpy errorSpy(reply,
                        static_cast<void (QCNetworkReply::*)(NetworkError)>(
                            &QCNetworkReply::error));
    QVERIFY(waitForFinished(reply, 10000));
    QCOMPARE(errorSpy.count(), 0);
    QVERIFY(job->isFinished());
    QCOMPARE(job->error(), NetworkError::NoError);
    QCOMPARE(reply->error(), NetworkError::NoError);

    reply->deleteLater();
    job->deleteLater();
}

void TestQCNetworkFileTransfer::testDownloadFileResumableFailsWhenContentRangeStartDoesNotMatch()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString savePath  = dir.filePath(QStringLiteral("mismatched-range.bin"));
    const qint64 totalBytes = 96 * 1024;

    ThrottledRangeServer server(totalBytes);
    server.setPartialResponseMode(ThrottledRangeServer::PartialResponseMode::MismatchedRangeStart);
    QVERIFY2(server.start(), "Cannot start local throttled HTTP server (port binding failed)");

    QFile partial(savePath);
    QVERIFY(partial.open(QIODevice::WriteOnly));
    const QByteArray stalePrefix(4096, 'z');
    partial.write(stalePrefix);
    partial.close();

    const QUrl url = server.url(QStringLiteral("/range.bin"));
    auto *job = new QCNetworkResumableDownloadJob(m_manager, url, savePath, false, this);
    QCOMPARE(job->reply(), nullptr);
    job->start();
    QTRY_VERIFY_WITH_TIMEOUT(job->reply() != nullptr, 1000);
    auto *reply = job->reply();
    QVERIFY(waitForFinished(reply, 10000));
    QVERIFY(job->isFinished());
    QCOMPARE(job->error(), NetworkError::InvalidRequest);
    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QVERIFY(reply->errorString().contains(QStringLiteral("Content-Range.start")));

    QFile finalFile(savePath);
    QVERIFY(finalFile.open(QIODevice::ReadOnly));
    const QByteArray finalData = finalFile.readAll();
    QCOMPARE(finalData, stalePrefix);

    reply->deleteLater();
    job->deleteLater();
}

QTEST_MAIN(TestQCNetworkFileTransfer)

#include "tst_QCNetworkFileTransfer.moc"
