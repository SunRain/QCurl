#ifndef QCNETWORK_RECEIVE_IDLE_SERVER_H
#define QCNETWORK_RECEIVE_IDLE_SERVER_H

#include "qcurl_tls_test_server.h"

#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <atomic>
#include <memory>

struct IdleResponse
{
    int handshakeDelayMs  = 0;
    int headerDelayMs     = 0;
    int chunkDelayMs      = 0;
    int headerLineDelayMs = 0;
    int closeDelayMs      = -1;
    QList<QByteArray> chunks{QByteArrayLiteral("ok")};
    int status = 200;
    QByteArray extraHeaders;
    bool keepAlive = false;
};

struct IdleServerStats
{
    std::atomic<int> connections{0};
    std::atomic<int> requests{0};
    std::atomic<qint64> uploaded{0};
};

/// 独立事件循环服务真实 TCP/TLS；延迟只阻塞服务器协议阶段，不阻塞客户端。
class IdleServerWorker final : public QObject
{
    Q_OBJECT
public:
    IdleServerWorker(QList<IdleResponse> responses, bool tls, std::shared_ptr<IdleServerStats> stats)
        : m_responses(std::move(responses))
        , m_tls(tls)
        , m_stats(std::move(stats))
    {
        connect(&m_server, &TlsTestTcpServer::socketAccepted, this, &IdleServerWorker::acceptSocket);
    }
    quint16 start()
    {
        if (m_tls) {
            QFile cert(
                QStringLiteral(QCURL_TEST_SOURCE_ROOT "/tests/qcurl/testdata/http2/localhost.crt"));
            QFile key(
                QStringLiteral(QCURL_TEST_SOURCE_ROOT "/tests/qcurl/testdata/http2/localhost.key"));
            if (!QSslSocket::supportsSsl() || !cert.open(QIODevice::ReadOnly)
                || !key.open(QIODevice::ReadOnly)) {
                return 0;
            }
            m_config = QSslConfiguration::defaultConfiguration();
            m_config.setLocalCertificate(QSslCertificate(cert.readAll(), QSsl::Pem));
            m_config.setPrivateKey(QSslKey(key.readAll(), QSsl::Rsa, QSsl::Pem));
            m_config.setPeerVerifyMode(QSslSocket::VerifyNone);
        }
        return m_server.listen(QHostAddress::LocalHost, 0) ? m_server.serverPort() : 0;
    }
    void stop()
    {
        m_server.close();
        for (auto *socket : findChildren<QSslSocket *>()) {
            socket->abort();
        }
    }

private:
    Q_DISABLE_COPY_MOVE(IdleServerWorker)
    struct Session
    {
        QByteArray pending;
        qint64 remainingBody = 0;
    };
    void acceptSocket(qintptr descriptor)
    {
        auto *socket = new QSslSocket(this);
        if (!socket->setSocketDescriptor(descriptor)) {
            socket->deleteLater();
            return;
        }
        const int index = m_stats->connections.fetch_add(1);
        connect(socket, &QAbstractSocket::disconnected, socket, &QObject::deleteLater);
        auto session = std::make_shared<Session>();
        connect(socket, &QIODevice::readyRead, socket, [this, socket, session]() {
            readRequest(socket, session);
        });
        if (m_tls) {
            socket->setSslConfiguration(m_config);
            const int delay = m_responses.at(qMin(index, int(m_responses.size()) - 1))
                                  .handshakeDelayMs;
            QTimer::singleShot(delay, socket, [socket]() { socket->startServerEncryption(); });
        }
    }
    void readRequest(QSslSocket *socket, const std::shared_ptr<Session> &session)
    {
        if (m_tls && !socket->isEncrypted()) {
            return;
        }
        session->pending += socket->readAll();
        if (session->remainingBody > 0) {
            const qint64 consumed = qMin(session->remainingBody, qint64(session->pending.size()));
            session->remainingBody -= consumed;
            session->pending.remove(0, consumed);
            m_stats->uploaded.fetch_add(consumed);
        }
        const auto end = session->pending.indexOf("\r\n\r\n");
        if (session->remainingBody > 0 || end < 0) {
            return;
        }
        const auto headers = session->pending.left(end);
        session->pending.remove(0, end + 4);
        for (auto line : headers.split('\n')) {
            if (line.toLower().startsWith("content-length:")) {
                session->remainingBody = line.mid(15).trimmed().toLongLong();
            }
        }
        const auto consumed = qMin(session->remainingBody, qint64(session->pending.size()));
        m_stats->uploaded.fetch_add(consumed);
        session->remainingBody -= consumed;
        session->pending.remove(0, consumed);
        const int index     = m_stats->requests.fetch_add(1);
        const auto response = m_responses.at(qMin(index, int(m_responses.size()) - 1));
        QTimer::singleShot(response.headerDelayMs, socket, [socket, response]() {
            sendHeaders(socket, response);
        });
    }
    static void sendHeaders(QSslSocket *socket, const IdleResponse &response)
    {
        if (socket->state() != QAbstractSocket::ConnectedState) {
            return;
        }
        qint64 length = 0;
        for (const auto &chunk : response.chunks) {
            length += chunk.size();
        }
        const QList<QByteArray> lines{"HTTP/1.1 " + QByteArray::number(response.status)
                                          + " Response\r\n",
                                      "Content-Length: " + QByteArray::number(length) + "\r\n",
                                      response.keepAlive ? "Connection: keep-alive\r\n"
                                                         : "Connection: close\r\n",
                                      response.extraHeaders + "\r\n"};
        sendHeaderLine(socket, response, lines, 0);
    }
    static void sendHeaderLine(QSslSocket *socket,
                               const IdleResponse &response,
                               const QList<QByteArray> &lines,
                               qsizetype index)
    {
        if (socket->state() != QAbstractSocket::ConnectedState) {
            return;
        }
        socket->write(lines.at(index));
        if (index + 1 < lines.size()) {
            QTimer::singleShot(response.headerLineDelayMs,
                               socket,
                               [socket, response, lines, index]() {
                                   sendHeaderLine(socket, response, lines, index + 1);
                               });
        } else {
            sendChunk(socket, response, 0);
        }
    }
    static void sendChunk(QSslSocket *socket, const IdleResponse &response, qsizetype index)
    {
        if (socket->state() != QAbstractSocket::ConnectedState) {
            return;
        }
        if (index < response.chunks.size()) {
            socket->write(response.chunks.at(index));
        }
        if (index + 1 < response.chunks.size()) {
            QTimer::singleShot(response.chunkDelayMs, socket, [socket, response, index]() {
                sendChunk(socket, response, index + 1);
            });
        } else if (!response.keepAlive) {
            socket->disconnectFromHost();
        } else if (response.closeDelayMs >= 0) {
            QTimer::singleShot(response.closeDelayMs, socket, [socket]() {
                socket->disconnectFromHost();
            });
        }
    }
    TlsTestTcpServer m_server{this};
    QList<IdleResponse> m_responses;
    bool m_tls;
    std::shared_ptr<IdleServerStats> m_stats;
    QSslConfiguration m_config;
};

class IdleServer final : public QObject
{
    Q_OBJECT
public:
    explicit IdleServer(QList<IdleResponse> responses = {IdleResponse{}}, bool tls = false)
        : m_tls(tls)
    {
        auto *worker = new IdleServerWorker(std::move(responses), tls, m_stats);
        m_worker     = worker;
        worker->moveToThread(&m_thread);
        connect(&m_thread, &QThread::finished, worker, &QObject::deleteLater);
        m_thread.start();
        // 仅夹具启动/关闭同步；worker 不等待客户端，锁序不存在相互等待。
        QMetaObject::invokeMethod(
            worker, [this, worker]() { m_port = worker->start(); }, Qt::BlockingQueuedConnection);
    }
    ~IdleServer() override
    {
        QMetaObject::invokeMethod(
            m_worker, [worker = m_worker]() { worker->stop(); }, Qt::BlockingQueuedConnection);
        m_thread.quit();
        m_thread.wait();
    }
    bool started() const { return m_port != 0; }
    QUrl url() const
    {
        return QUrl(QStringLiteral("%1://127.0.0.1:%2/test")
                        .arg(m_tls ? QStringLiteral("https") : QStringLiteral("http"))
                        .arg(m_port));
    }
    int requests() const { return m_stats->requests.load(); }
    int connections() const { return m_stats->connections.load(); }
    qint64 uploaded() const { return m_stats->uploaded.load(); }

private:
    Q_DISABLE_COPY_MOVE(IdleServer)
    QThread m_thread{this};
    IdleServerWorker *m_worker               = nullptr;
    std::shared_ptr<IdleServerStats> m_stats = std::make_shared<IdleServerStats>();
    quint16 m_port                           = 0;
    bool m_tls;
};

#endif
