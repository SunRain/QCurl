#ifndef QCURL_RESOLVE_TEST_SERVER_H
#define QCURL_RESOLVE_TEST_SERVER_H

#include <QFile>
#include <QPointer>
#include <QSharedPointer>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslServer>
#include <QTcpSocket>

#include <utility>

// 同端口不同 loopback 地址提供不同正文，保留连接以观察解析配置与复用隔离。
class ResolveTestServer final : public QSslServer
{
    Q_OBJECT

public:
    explicit ResolveTestServer(QByteArray body,
                               bool tls            = false,
                               QString certificate = QStringLiteral("localhost.crt"))
        : m_body(std::move(body))
        , m_tls(tls)
        , m_certificate(std::move(certificate))
    {
        connect(this, &QTcpServer::pendingConnectionAvailable, this, [this]() {
            while (auto *socket = nextPendingConnection()) {
                auto pending = QSharedPointer<QByteArray>::create();
                ++m_connections;
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                const auto readPending = [this, socket, pending]() {
                    pending->append(socket->readAll());
                    const auto end = pending->indexOf("\r\n\r\n");
                    if (end < 0) {
                        return;
                    }
                    m_requests.append(pending->left(end + 4));
                    pending->remove(0, end + 4);
                    if (m_requests.last().startsWith("CONNECT ")) {
                        socket->write(
                            m_acceptTunnels
                                ? QByteArrayLiteral("HTTP/1.1 200 Connection Established\r\n\r\n")
                                : QByteArrayLiteral("HTTP/1.1 403 Forbidden\r\nContent-Length: "
                                                    "0\r\nConnection: close\r\n\r\n"));
                        if (!m_acceptTunnels) {
                            socket->disconnectFromHost();
                        }
                        return;
                    }
                    if (m_holdResponses) {
                        m_pending.append(socket);
                    } else {
                        writeResponse(socket);
                    }
                };
                connect(socket, &QTcpSocket::readyRead, this, readPending);
                // TLS 握手完成前可能已经收到了应用数据，不能丢失先于 newConnection 的通知。
                if (socket->bytesAvailable()) {
                    readPending();
                }
            }
        });
    }

    [[nodiscard]] bool start(const QString &ip, quint16 port = 0)
    {
        if (m_tls) {
            const QString dir = QStringLiteral(QCURL_TEST_SOURCE_ROOT
                                               "/tests/qcurl/testdata/http2/");
            QFile cert(dir + m_certificate);
            QFile key(dir + QStringLiteral("localhost.key"));
            if (!cert.open(QIODevice::ReadOnly) || !key.open(QIODevice::ReadOnly)) {
                return false;
            }
            auto config = QSslConfiguration::defaultConfiguration();
            config.setLocalCertificate(QSslCertificate(cert.readAll()));
            config.setPrivateKey(QSslKey(key.readAll(), QSsl::Rsa));
            config.setPeerVerifyMode(QSslSocket::VerifyNone);
            setSslConfiguration(config);
        }
        return listen(QHostAddress(ip), port);
    }
    int connections() const { return m_connections; }
    const QList<QByteArray> &requests() const { return m_requests; }
    void setResponseHeaders(QByteArray headers) { m_responseHeaders = std::move(headers); }
    void setKeepAlive(bool keepAlive) { m_keepAlive = keepAlive; }
    void setResponseStatus(int status) { m_status = status; }
    void setAcceptTunnels(bool accept) { m_acceptTunnels = accept; }
    void setHoldResponses(bool hold) { m_holdResponses = hold; }
    void releaseResponses()
    {
        m_holdResponses    = false;
        const auto pending = std::exchange(m_pending, {});
        for (const auto &socket : pending) {
            if (socket) {
                writeResponse(socket.data());
            }
        }
    }

protected:
    void incomingConnection(qintptr descriptor) override
    {
        if (m_tls) {
            QSslServer::incomingConnection(descriptor);
        } else {
            QTcpServer::incomingConnection(descriptor);
        }
    }

private:
    Q_DISABLE_COPY_MOVE(ResolveTestServer)

    void writeResponse(QTcpSocket *socket)
    {
        socket->write(QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(m_status)
                      + QByteArrayLiteral(" Response\r\nContent-Length: ")
                      + QByteArray::number(m_body.size())
                      + (m_keepAlive ? QByteArrayLiteral("\r\nConnection: keep-alive\r\n")
                                     : QByteArrayLiteral("\r\nConnection: close\r\n"))
                      + m_responseHeaders + QByteArrayLiteral("\r\n") + m_body);
        if (!m_keepAlive) {
            socket->disconnectFromHost();
        }
    }

    QByteArray m_body;
    bool m_tls;
    QString m_certificate;
    int m_connections = 0;
    QList<QByteArray> m_requests;
    QByteArray m_responseHeaders;
    bool m_keepAlive     = true;
    int m_status         = 200;
    bool m_acceptTunnels = false;
    bool m_holdResponses = false;
    QList<QPointer<QTcpSocket>> m_pending;
};

#endif
