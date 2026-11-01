#ifndef QCNETWORK_CONNECT_TEST_PROXY_H
#define QCNETWORK_CONNECT_TEST_PROXY_H

#include <QHostAddress>
#include <QSharedPointer>
#include <QTcpServer>
#include <QTcpSocket>

/// 固定转发到本地 TLS 夹具的 CONNECT 代理；首次 407 用于验证原生认证重发与 trace 资格。
class ConnectTestProxy final : public QObject
{
    Q_OBJECT

public:
    explicit ConnectTestProxy(quint16 upstreamPort)
        : m_upstreamPort(upstreamPort)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (auto *client = m_server.nextPendingConnection()) {
                acceptSocket(client);
            }
        });
    }

    bool start() { return m_server.listen(QHostAddress::LocalHost, 0); }
    quint16 port() const { return m_server.serverPort(); }
    QList<QByteArray> requests() const { return m_requests; }
    QString errorString() const { return m_errorString; }

private:
    Q_DISABLE_COPY_MOVE(ConnectTestProxy)

    void acceptSocket(QTcpSocket *client)
    {
        auto pending = QSharedPointer<QByteArray>::create();
        connect(client, &QTcpSocket::disconnected, client, &QObject::deleteLater);
        connect(client, &QTcpSocket::readyRead, this, [this, client, pending] {
            pending->append(client->readAll());
            const qsizetype end = pending->indexOf("\r\n\r\n");
            if (end < 0) {
                if (pending->size() > 16384) {
                    m_errorString = QStringLiteral("CONNECT 请求头超过夹具上限");
                    client->disconnectFromHost();
                }
                return;
            }
            disconnect(client, &QTcpSocket::readyRead, this, nullptr);
            const QByteArray request = pending->first(end + 4);
            m_requests.append(request);
            if (!request.startsWith("CONNECT ")) {
                m_errorString = QStringLiteral("代理未收到 CONNECT 请求");
                client->disconnectFromHost();
                return;
            }
            if (!request.toLower().contains("\r\nproxy-authorization:")) {
                client->write("HTTP/1.1 407 Proxy Authentication Required\r\n"
                              "Proxy-Authenticate: Basic realm=\"qcurl-fixture\"\r\n"
                              "Content-Length: 0\r\nConnection: close\r\n\r\n");
                client->disconnectFromHost();
                return;
            }
            auto *upstream = new QTcpSocket(client);
            connect(upstream, &QTcpSocket::connected, client, [client, upstream, pending, end] {
                client->write("HTTP/1.1 200 Connection established\r\n\r\n");
                upstream->write(pending->sliced(end + 4));
                upstream->write(client->readAll());
                connect(client, &QTcpSocket::readyRead, upstream, [client, upstream] {
                    upstream->write(client->readAll());
                });
            });
            connect(upstream, &QTcpSocket::readyRead, client, [client, upstream] {
                client->write(upstream->readAll());
            });
            connect(upstream, &QTcpSocket::disconnected, client, &QTcpSocket::disconnectFromHost);
            connect(upstream, &QTcpSocket::errorOccurred, client, [this, client, upstream] {
                m_errorString = upstream->errorString();
                client->disconnectFromHost();
            });
            upstream->connectToHost(QHostAddress::LocalHost, m_upstreamPort);
        });
    }

    QTcpServer m_server{this};
    quint16 m_upstreamPort;
    QList<QByteArray> m_requests;
    QString m_errorString;
};

#endif // QCNETWORK_CONNECT_TEST_PROXY_H
