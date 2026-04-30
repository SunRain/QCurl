/**
 * @file tst_QCNetworkFileResumableOffline.cpp
 * @brief downloadFileResumable 离线门禁
 */

#include "QCNetworkAccessManager.h"
#include "QCNetworkReply.h"

#include <QFile>
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

} // namespace

class TestQCNetworkFileResumableOffline : public QObject
{
    Q_OBJECT

private slots:
    void testTreats416MatchingContentRangeAsAlreadyCompleteWithoutErrorSignal();
    void testFailsWhenContentRangeStartDoesNotMatch();
};

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

    auto *reply = manager.downloadFileResumable(server.url(QStringLiteral("/range.bin")), savePath);
    QVERIFY(reply);

    QSignalSpy errorSpy(reply,
                        static_cast<void (QCNetworkReply::*)(NetworkError)>(
                            &QCNetworkReply::error));
    QVERIFY(waitForFinished(reply));
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

    auto *reply = manager.downloadFileResumable(server.url(QStringLiteral("/range.bin")), savePath);
    QVERIFY(reply);
    QVERIFY(waitForFinished(reply));
    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QVERIFY(reply->errorString().contains(QStringLiteral("Content-Range.start")));

    QFile finalFile(savePath);
    QVERIFY(finalFile.open(QIODevice::ReadOnly));
    const QByteArray finalData = finalFile.readAll();
    QCOMPARE(finalData, stalePrefix);

    reply->deleteLater();
}

QTEST_MAIN(TestQCNetworkFileResumableOffline)

#include "tst_QCNetworkFileResumableOffline.moc"
