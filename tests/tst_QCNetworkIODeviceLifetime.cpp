/**
 * @file tst_QCNetworkIODeviceLifetime.cpp
 * @brief QIODevice 所有权与生命周期契约测试
 */

#include <QtTest/QtTest>

#include <QBuffer>
#include <QHostAddress>
#include <QPointer>
#include <QSharedPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include "QCNetworkAccessManager.h"
#include "QCNetworkReply.h"
#include "QCNetworkError.h"

using namespace QCurl;

namespace {

class SlowBodyServer final : public QObject
{
    Q_OBJECT

public:
    explicit SlowBodyServer(QByteArray payload,
                            qint64 chunkSize = 4096,
                            int intervalMs = 10,
                            QObject *parent = nullptr)
        : QObject(parent)
        , m_payload(std::move(payload))
        , m_chunkSize(chunkSize)
        , m_intervalMs(intervalMs)
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

    QUrl url() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/slow-body")
                        .arg(m_server.serverPort()));
    }

private:
    void handleSocket(QTcpSocket *socket)
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

            const QByteArray responseHeader = QByteArray("HTTP/1.1 200 OK\r\n"
                                                         "Content-Type: application/octet-stream\r\n"
                                                         "Content-Length: ")
                + QByteArray::number(m_payload.size())
                + QByteArray("\r\nConnection: close\r\n\r\n");

            socket->write(responseHeader);
            socket->flush();

            auto offset = QSharedPointer<qint64>::create(0);
            auto *timer = new QTimer(socket);
            timer->setInterval(m_intervalMs);
            timer->setSingleShot(false);

            QObject::connect(timer, &QTimer::timeout, socket, [this, socket, timer, offset]() {
                if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
                    timer->stop();
                    return;
                }

                const qint64 remaining = m_payload.size() - *offset;
                if (remaining <= 0) {
                    timer->stop();
                    socket->disconnectFromHost();
                    return;
                }

                const qint64 toSend = qMin(m_chunkSize, remaining);
                socket->write(m_payload.constData() + *offset, toSend);
                socket->flush();
                *offset += toSend;
            });

            timer->start();
        });
    }

    QByteArray m_payload;
    qint64 m_chunkSize = 4096;
    int m_intervalMs = 10;
    QTcpServer m_server;
};

} // namespace

class TestQCNetworkIODeviceLifetime : public QObject
{
    Q_OBJECT

private slots:
    void testDownloadToDeviceDeviceDestroyedDuringTransfer();
};

void TestQCNetworkIODeviceLifetime::testDownloadToDeviceDeviceDestroyedDuringTransfer()
{
    QByteArray payload(512 * 1024, Qt::Uninitialized);
    for (int i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<char>(i % 256);
    }

    SlowBodyServer server(payload, 4096, 10);
    if (!server.start()) {
        QSKIP("Cannot start local slow HTTP server (port binding failed)");
    }

    QCNetworkAccessManager manager;

    auto *buffer = new QBuffer();
    QVERIFY(buffer->open(QIODevice::ReadWrite));
    QPointer<QBuffer> safeBuffer(buffer);

    QCNetworkReply *reply = manager.downloadToDevice(server.url(), buffer);
    QVERIFY(reply);

    QSignalSpy errorSpy(reply, QMetaMethod::fromSignal(
                                  static_cast<void (QCNetworkReply::*)(NetworkError)>(&QCNetworkReply::error)));
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
    QSignalSpy cancelledSpy(reply, &QCNetworkReply::cancelled);

    int readyReadCount = 0;
    int progressCount = 0;
    bool deleteScheduled = false;
    const qint64 deleteAtBytes = 16 * 1024;

    QObject::connect(reply, &QCNetworkReply::readyRead, reply, [&]() { ++readyReadCount; });
    QObject::connect(reply, &QCNetworkReply::downloadProgress, reply,
                     [&](qint64 received, qint64 /*total*/) {
                         ++progressCount;
                         if (!deleteScheduled && safeBuffer && received >= deleteAtBytes) {
                             deleteScheduled = true;
                             safeBuffer->deleteLater();
                         }
                     });

    QVERIFY(finishedSpy.wait(20000));

    QCOMPARE(cancelledSpy.count(), 0);
    QCOMPARE(errorSpy.count(), 1);
    QCOMPARE(finishedSpy.count(), 1);

    QCOMPARE(reply->error(), NetworkError::InvalidRequest);
    QVERIFY2(reply->errorString().contains(QStringLiteral("QIODevice")),
             "errorString should mention QIODevice for diagnosability");

    // 额外回归：完成后不应继续产生可观测数据事件（尽量避免 flaky，仅做短窗口断言）
    const int readyReadAfterFinished = readyReadCount;
    const int progressAfterFinished = progressCount;
    QTest::qWait(200);
    QCOMPARE(readyReadCount, readyReadAfterFinished);
    QCOMPARE(progressCount, progressAfterFinished);

    reply->deleteLater();
}

QTEST_MAIN(TestQCNetworkIODeviceLifetime)
#include "tst_QCNetworkIODeviceLifetime.moc"
