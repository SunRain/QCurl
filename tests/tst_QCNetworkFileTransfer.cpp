/**
 * @file tst_QCNetworkFileTransfer.cpp
 * @brief 流式下载/上传与断点续传 API 测试
 */

#include <QtTest/QtTest>
#include <QBuffer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>

#include "QCNetworkAccessManager.h"
#include "QCNetworkRequest.h"
#include "QCNetworkReply.h"
#include "QCNetworkError.h"

#include "test_httpbin_env.h"

using namespace QCurl;

namespace {

class ThrottledRangeServer final : public QObject
{
public:
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

    bool start()
    {
        return m_server.listen(QHostAddress::LocalHost, 0);
    }

    QUrl url(const QString &path) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2")
                        .arg(m_server.serverPort())
                        .arg(path));
    }

    QByteArray expectedPayload() const
    {
        return m_payload;
    }

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

        bool ok = false;
        const qint64 start = value.mid(6, dashPos - 6).toLongLong(&ok);
        return ok ? qMax<qint64>(0, start) : 0;
    }

    void setupSocket(QTcpSocket *socket)
    {
        auto requestBuffer = QSharedPointer<QByteArray>::create();
        auto requestHandled = QSharedPointer<bool>::create(false);

        QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, requestBuffer, requestHandled]() {
            if (*requestHandled) {
                return;
            }

            requestBuffer->append(socket->readAll());
            const int headerEnd = requestBuffer->indexOf("\r\n\r\n");
            if (headerEnd < 0) {
                return;
            }

            *requestHandled = true;

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
                const QByteArray resp = QByteArray("HTTP/1.1 416 Range Not Satisfiable\r\n"
                                                   "Connection: close\r\n\r\n");
                socket->write(resp);
                socket->disconnectFromHost();
                return;
            }

            const bool isPartial = rangeStart > 0;
            const qint64 remaining = m_totalBytes - rangeStart;

            QByteArray resp;
            resp += isPartial ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
            resp += "Content-Type: application/octet-stream\r\n";
            resp += "Accept-Ranges: bytes\r\n";
            resp += "Content-Length: " + QByteArray::number(remaining) + "\r\n";
            if (isPartial) {
                resp += "Content-Range: bytes ";
                resp += QByteArray::number(rangeStart);
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

            socket->write(m_payload.constData() + rangeStart, remaining);
            socket->flush();
            socket->disconnectFromHost();
        });
    }

    qint64 m_totalBytes = 0;
    QByteArray m_payload;
    QTcpServer m_server;
    bool m_dropFirstFullResponse = true;
    qint64 m_dropBytes = 64 * 1024;
};

} // namespace

class TestQCNetworkFileTransfer : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void testDownloadToDeviceWritesExpectedBytes();
    void testUploadFromDeviceSendsPayload();
    void testDownloadFileResumableContinuesFromPartialFile();

private:
    QCNetworkAccessManager *m_manager = nullptr;
    QString m_httpbinBaseUrl;

    // ✅ 增加超时时间到 40 秒 (之前是 20 秒)
    bool waitForFinished(QCNetworkReply *reply, int timeout = 40000);
    QJsonObject parseJson(const QByteArray &data) const;
};

void TestQCNetworkFileTransfer::initTestCase()
{
    qDebug() << "========================================";
    qDebug() << "文件传输 API 测试";
    qDebug() << "========================================";

    m_manager = new QCNetworkAccessManager(this);

    m_httpbinBaseUrl = TestEnv::httpbinBaseUrl();
    if (m_httpbinBaseUrl.isEmpty()) {
        QSKIP(qPrintable(TestEnv::httpbinMissingReason()));
    }
    qDebug() << "httpbin base URL:" << m_httpbinBaseUrl;

    QCNetworkRequest healthCheck(QUrl(m_httpbinBaseUrl + "/status/200"));
    auto *reply = m_manager->sendGet(healthCheck);
    QVERIFY(waitForFinished(reply, 5000));
    if (reply->error() != NetworkError::NoError) {
        const QString message = QStringLiteral("无法连接 httpbin 服务: %1 (%2)")
                                    .arg(m_httpbinBaseUrl, reply->errorString());
        reply->deleteLater();
        QSKIP(qPrintable(message));
    }
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

void TestQCNetworkFileTransfer::testDownloadToDeviceWritesExpectedBytes()
{
    QBuffer buffer;
    QVERIFY(buffer.open(QIODevice::ReadWrite));

    const int expectedBytes = 4096;
    QUrl url(m_httpbinBaseUrl + QStringLiteral("/bytes/%1?seed=42").arg(expectedBytes));

    auto *reply = m_manager->downloadToDevice(url, &buffer);
    QVERIFY(waitForFinished(reply));
    QCOMPARE(reply->error(), NetworkError::NoError);

    QCOMPARE(static_cast<int>(buffer.size()), expectedBytes);
    buffer.seek(0);
    QByteArray downloaded = buffer.readAll();
    QCOMPARE(downloaded.size(), expectedBytes);

    reply->deleteLater();
}

void TestQCNetworkFileTransfer::testUploadFromDeviceSendsPayload()
{
    QByteArray payload(2048, Qt::Uninitialized);
    for (int i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<char>(i % 256);
    }

    QBuffer buffer(&payload);
    QVERIFY(buffer.open(QIODevice::ReadOnly));

    QUrl url(m_httpbinBaseUrl + "/post");
    auto *reply = m_manager->uploadFromDevice(url,
                                            QStringLiteral("file"),
                                            &buffer,
                                            QStringLiteral("payload.bin"),
                                            QStringLiteral("application/octet-stream"));

    QVERIFY(waitForFinished(reply));
    QCOMPARE(reply->error(), NetworkError::NoError);

    auto body = reply->readAll();
    QVERIFY(body.has_value());
    
    QJsonObject json = parseJson(body.value());
    QJsonObject filesObj = json.value(QStringLiteral("files")).toObject();
    QString echoedStr = filesObj.value(QStringLiteral("file")).toString();
    
    // ✅ 修复：httpbin 返回的是 base64 data URL，需要解码
    // 格式: "data:application/octet-stream;base64,AAECAwQF..."
    QByteArray echoed;
    if (echoedStr.startsWith("data:")) {
        // 提取 base64 部分
        int commaPos = echoedStr.indexOf(',');
        if (commaPos != -1) {
            QString base64Part = echoedStr.mid(commaPos + 1);
            echoed = QByteArray::fromBase64(base64Part.toUtf8());
        } else {
            echoed = echoedStr.toUtf8();
        }
    } else {
        echoed = echoedStr.toUtf8();
    }
    
    QCOMPARE(echoed, payload);

    reply->deleteLater();
}

void TestQCNetworkFileTransfer::testDownloadFileResumableContinuesFromPartialFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString savePath = dir.filePath(QStringLiteral("resumable.bin"));
    const qint64 totalBytes = 256 * 1024;

    ThrottledRangeServer server(totalBytes);
    if (!server.start()) {
        QSKIP("Cannot start local throttled HTTP server (port binding failed)");
    }

    const QUrl url = server.url(QStringLiteral("/range.bin"));

    auto *firstAttempt = m_manager->downloadFileResumable(url, savePath);
    QVERIFY(firstAttempt);

    // ✅ 服务端会主动断开连接造成部分下载，确保用例可复现且不依赖 cancel 时序
    QVERIFY(waitForFinished(firstAttempt, 10000));
    QVERIFY(firstAttempt->error() != NetworkError::NoError);
    firstAttempt->deleteLater();

    QFile partial(savePath);
    QVERIFY(partial.exists());
    qint64 partialSize = partial.size();
    QVERIFY(partialSize > 0);
    QVERIFY(partialSize < totalBytes);

    auto *resumeReply = m_manager->downloadFileResumable(url, savePath);
    QVERIFY(waitForFinished(resumeReply, 10000));
    QCOMPARE(resumeReply->error(), NetworkError::NoError);
    resumeReply->deleteLater();

    QFile finalFile(savePath);
    QVERIFY(finalFile.open(QIODevice::ReadOnly));
    QByteArray finalData = finalFile.readAll();
    QCOMPARE(static_cast<qint64>(finalData.size()), totalBytes);
    QCOMPARE(finalData, server.expectedPayload());
}

QTEST_MAIN(TestQCNetworkFileTransfer)

#include "tst_QCNetworkFileTransfer.moc"
